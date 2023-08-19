#pragma once

#include <functional>
#include <memory>
#include "nocopyable.h"
#include "Timestamp.h"

class EventLoop;

// EventLoop\Channel\Poller 对应的是 Reactor模型上的Demultiplex
// channel 理解为通道，封装了sockfd和其感兴趣的event，如EPOLLIN、EPOLLOUT事件
// 还绑定了poller具体返回的事件
class Channel: nocopyable {
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // fd得到poller通知之后，用于处理事件
    void handlEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { 
        readCallback_ = std::move(cb);
    }
    void setWriteCallback(EventCallback cb) {
        writeCallback_ = std::move(cb);
    }
    void setCloseCallback(EventCallback cb) {
        closeCallback_ = std::move(cb);
    }
    void setErrorCallback(EventCallback cb) {
        errorCallback_ = std::move(cb);
    }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void>&);

    int fd() const {return fd_;} ;
    int events() const { return events_; };
    void set_revents(int revt) { revents_ = revt; }; 

    // 设置fd相应的事件状态
    void enableReading() { events_ |= kReadEvent; update(); };    // update就是在调用epoll_ctl，把fd感兴趣的事件添加到poller里面
    void disableReading() { events_ &= ~kReadEvent; update(); };
    void enableWriting() { events_ |= kWriteEvent; update(); };
    void disableWriting() { events_ &= ~kWriteEvent; update(); };
    void disableAll() { events_ = kNoneEvent; update(); };

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; };
    bool isReading() const { return events_ & kReadEvent; };
    bool isWriting() const { return events_ & kWriteEvent; };

    int index() { return index_; };
    void set_index(int idx) { index_ = idx; }; 

    // one loop per thread
    EventLoop* onwerLoop() { return loop_; };
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;   // 事件循环
    const int fd_;  // poller监听的对象 
    int events_;    // 注册fd感兴趣的事件
    int revents_;   // poller返回的实际发生的事件
    int index_;

    std::weak_ptr<void> tie_;   // 用于观察一个强智能指针
    bool tied_;
    
    // channel 通道里能够获知fd最终发生的具体事件 revents
    // 所以channel也要负责调用具体事件的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};