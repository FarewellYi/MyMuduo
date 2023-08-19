#include "Channel.h"
#include "EventLoop.h"
#include "sys/epoll.h"
#include "Logger.h"

 const int Channel::kNoneEvent = 0;
 const int Channel::kReadEvent = EPOLLIN |EPOLLPRI;
 const int Channel::kWriteEvent = EPOLLOUT;

 Channel::Channel(EventLoop* loop, int fd)
    :loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false){

}

Channel::~Channel() {

}

void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

// 当改变chanel所表示fd的events事件后， 
// update负责向poller中更新fd相应的事件 -> epoll_ctl
// EventLoop -> Channel / Poller
void Channel::update() {
    // 通过channel所属的EventLoop, 调用poller相应的方法，注册fd的events事件
    loop_->updateChannel(this);
}


void Channel::remove() {
    // 通过channel所属的EventLoop, 删除当前channel
    loop_->removeChannel(this);
}

void Channel::handlEvent(Timestamp receiveTime) {
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard) {
            handleEventWithGuard(receiveTime);
        }
        else {
            handleEventWithGuard(receiveTime);
        }
    }
}

// 根据poller通知channel发生的具体事件，由channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime) {
    LOG_INFO("channel handleEvent revents:%d\n", revents_);

    // EPOLLHUP：表示对应的文件描述符被挂断
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) {
            closeCallback_();
        }
    }

    if (revents_ & EPOLLERR) {
        if (errorCallback_) {
            errorCallback_();
        }
    }

    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) {
            readCallback_(receiveTime);
        }
    }

    if (revents_ & EPOLLOUT) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}