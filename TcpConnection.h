#pragma once

#include "nocopyable.h"
#include "Buffer.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

/*
    TcpServer => Acceptor => 有一个新用户连接，通过accept拿到connfd =>
        TcpConnection设置回调 => Channel => Poller => Channel的回调操作
*/

class TcpConnection: nocopyable, public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop,
                    const std::string& name,
                    int sockfd,
                    const InetAddress& localAddr,
                    const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; };
    const std::string& name() const { return name_; };
    const InetAddress& loaclAddress() const { return localAddr_; };
    const InetAddress& peerAddress() const { return peerAddr_; };

    bool connected() const { return state_ == kConnected; };

    // 发送数据
    void send(const std::string& buf);
    // 关闭连接
    void shutdown();

    void setConnectionCallback(const ConnectionCallback& cb) {
        connectionCallback_ = cb;
    }

    void setMessageCallback(const MessageCallback& cb) {
        messageCallback_ = cb;
    }

    void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
        writeCompleteCallback_ = cb;
    }

    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark) {
        highWaterMarkCallback_ = cb;
        highWaterMark_ = highWaterMark;
    }
    
    void setCloseCallback(const CloseCallback& cb) {
        closeCallback_ = cb;
    }
    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestory();
private:
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting};
    void setState(StateE state) { state_ = state; };

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* message, size_t len);
    void shutdownInLoop();

    EventLoop* loop_; // 绝不是baseloop，因为TCP Connection都是在subloop中被管理的
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_; 

    size_t highWaterMark_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};