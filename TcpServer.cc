#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"
#include <string.h>

#include <functional>

static EventLoop* CheckLoopNotNull(EventLoop* loop) {
    if (loop == nullptr) {
        LOG_FATAL("%s%s%d: mainLoop is null \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop* loop, 
                const InetAddress &ListenAddr,
                const std::string &nameArg,
                Option option)
                : loop_(CheckLoopNotNull(loop))
                , ipPort_(ListenAddr.toIpPort())
                , name_(nameArg)
                , acceptor_(new Acceptor(loop, ListenAddr, option == kReusePort))
                , threadPool_(new EventLoopThreadPool(loop, name_))
                , connectionCallback_()
                , messageCallback_()
                , nextConnId_(1) {
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, 
                                        std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer() {
    for (auto &item: connections_) {
        // 出函数体即可自动释放new出来的TcpConnection对象
        TcpConnectionPtr conn(item.second);
        item.second.reset();    // 将智能指针指向的对象置空

        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestory, conn));
    }
}

void TcpServer::setThreadNum(int numThreads) {
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start() {
    if (started_++ == 0) {  // 防止一个TcpServer被start多次
        threadPool_->start(threadInitCallback_);    // 启动底层的线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));    // 启动循环，用于listen
    }
}

// 有一个新的客户端连接，acceptor会执行回调操作
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
    EventLoop* ioLoop = threadPool_->getNextLoop(); // 使用轮询算法
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_; // 仅在mainloop的线程中使用
    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
                name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0) {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);

    // 通过连接成功的sockfd，创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(
                                ioLoop,
                                connName,
                                sockfd,
                                localAddr,
                                peerAddr

    ));
    connections_[connName] = conn;
    
    // 下面的回调皆是用户设置 顺序为：TcpServer => TcpConnection => Channel => Poller => notify channel
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置如何关闭连接的回调
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );

    /* 直接调用 */ 
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}


void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s \n",
        name_.c_str(), conn->name().c_str());
    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestory, conn)
    );
}
