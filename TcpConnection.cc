#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <errno.h>
#include <string>

static EventLoop* CheckLoopNotNull(EventLoop* loop) {
    if (loop == nullptr) {
        LOG_FATAL("%s%s%d: mainLoop is null \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop* loop,
                    const std::string& nameArg,
                    int sockfd,
                    const InetAddress& localAddr,
                    const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64 * 1024 * 1024) {
    
    // 给channel设置相应的回调函数， poller给channel通知感兴趣的事件发生，channel会回调相应的操作函数
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnetion::ctor[%s] at fd = %d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection() {
    LOG_INFO("TcpConnection::dtor[%s] at fd = %d state = %d \n",
            name_.c_str(), channel_->fd(), (int)state_);
}

void TcpConnection::send(const std::string& buf) {
    if (state_ == kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(buf.c_str(), buf.size());
        }
        else {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}

void TcpConnection::shutdown() {
    if (state_ == kConnected) {
        setState(kDisconnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this)
        );
    }
}

/*
    发送数据时，如果应用写的快，而内核发送数据慢，则需要把待发送数据写入缓冲区，并且设置了水位回调
*/ 
void TcpConnection::sendInLoop(const void* data, size_t len) {
    ssize_t nwrote = 0;         // 
    size_t remaining = len;     // 未发送完的数据
    bool faultError = false;    // 是否产生错误 

    // 此前调用过该connection的shutdown，无法再发送
    if (state_ == kDisconnected) {
        LOG_ERROR("Disconnected, give up writing!");
        return;
    }

    // channel_第一次开始写数据，且缓冲区中没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0) {  // 发送成功
            remaining = len - nwrote;   
            if (remaining == 0 && writeCompleteCallback_) {
                // 在这里数据已全部发送完成，无须给channel设置epollout事件
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else { // nwrote < 0
            nwrote = 0;
            if (errno != EWOULDBLOCK) { // EWOULDBLOCK表示非阻塞却没有发送数据
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到output缓冲区当中，
    // 然后给channel注册epollout事件，poller发现tcp的发送缓冲区有空间，会通知相应的sock - channel，调用writeCallback_回调方法
    // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
    if (!faultError && remaining > 0) 
    {
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining)
            );
        }
        outputBuffer_.append((char*)data + nwrote, remaining);  // 将未发送数据写到缓冲区中
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件，否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::shutdownInLoop() {
    if (!channel_->isWriting()) {   // 说明outputBuffer的数据已经全部发送完成
        socket_->shutdownWrite(); // 关闭写端
    }
}


// 连接建立
void TcpConnection::connectEstablished() {
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向poller注册channel的读事件

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this()); 
}

// 连接销毁
void TcpConnection::connectDestory() {
    if (state_ == kConnected) {
        setState(kDisconnected);
        channel_->disableAll();
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); // 把channel 从 poller中删除
}


void TcpConnection::handleRead(Timestamp receiveTime) {
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0) {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0) {
        handleClose();
    }
    else {
        errno = savedErrno;
        LOG_ERROR("TcpConnection:: handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite() {
    if (channel_->isWriting()) {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0) {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0) {
                channel_->disableWriting();
                if (writeCompleteCallback_) {
                    // 唤醒loop_对应的线程，执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting) {
                    shutdownInLoop();
                }
            }
        }
        else {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else {
        LOG_ERROR("TcpConnection fd = %d is down, no more writing \n", channel_->fd());
    }
}

// 底层的channel poller=>channel::closeCallback_ => TcpConnection::handleClose
void TcpConnection::handleClose() {
    LOG_INFO("fd = %d state = %d \n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调
    closeCallback_(connPtr); // 关闭连接的回调, 由TcpServer执行TcpServer::removeConnectiond
}

void TcpConnection::handleError() {
    int optVal;
    socklen_t optlen = sizeof optVal;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optVal, &optlen) < 0) {
        err = errno;
    }
    else {
        err = optVal;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR: %d \n", name_.c_str(), err);
}