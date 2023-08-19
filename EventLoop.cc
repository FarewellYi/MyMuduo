#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>


// 防止一个线程创建多个Eventloop
// 在一个线程中创建EventLoop对象时调用；当再次创建时，由于该指针不为空，无法被创建
__thread EventLoop* t_loopInThisThread = 0;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000; 

int createEventfd() {
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0) {
        LOG_FATAL("Failed in eventfd:%d \n", errno);
    }
    return evtfd;
}


EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , currentActiveChannel_(nullptr){

    LOG_DEBUG("EventLoop created %p in thread % d \n", this, threadId_);
    if (t_loopInThisThread) {
        LOG_FATAL("Another EventLoop %p exicts in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else {
        // 第一次创建
        t_loopInThisThread = this;
    }

    // 设置wakeupFd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupChannel的EPOLLIN读事件
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    wakeupChannel_->disableAll(); // 对所有事件都不感兴趣
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop() {
    looping_ = true;
    quit_ = false;

    LOG_INFO("Eventloop %p start looping !\n", this);

    while (!quit_) {
        activeChannels_.clear();
        // 监听两类fd，一种是client的fd，另一种是wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

        for (Channel* channel: activeChannels_) {
            // Poller负责监听哪些channel发生事件，上报给EventLoop，通知channel进行处理
            channel->handlEvent(pollReturnTime_);
        }

        // 执行当前EventLoop事件循环需要处理的回调操作
        /*
            mainloop事先注册一个回调cb 需要subloop执行
            当subloop被wakeup后，执行此前mainloop注册的若干cb操作
        */
        dePendingFunctors();
    }
    LOG_INFO("Eventloop %p stop looping! \n", this);
    looping_ = false;
}
// 退出事件循环
// 1.loop在自己的线程中调用quit：直接退出
// 2.在非loop的线程中，调用loop的quit：调用wakeup
void EventLoop::quit() {
    quit_ = true;
    // 在其他线程中，调用quit，即在一个subloop(woker)中，调用了mainloop(IO)的quit
    if (!isInLoopThread()) {
        wakeup();
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb) {
    // 在当前的loop线程中执行callback
    if (!isInLoopThread()) {
        cb();
    }
    // 在非当前的loop线程中执行callback，需要唤醒对应线程，再执行cb
    else {
        queueInLoop(std::move(cb));
    }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
    // 唤醒相应的需要执行上述回调操作的loop
    // || callingPendingFunctors_ = true: 当前loop正在执行回调，但是loop又有了新的回调
    if(!isInLoopThread() || callingPendingFunctors_) {
        wakeup();
    }
}


// 用于唤醒subReactor
void EventLoop::handleRead() {
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
    }
}

// 用于唤醒loop所在的线程
// 向wakeupFd写一个数据，wakeupChannel就发生读事件，当前Loop线程就会被唤醒
void EventLoop::wakeup() {
     uint64_t one = 1;
     ssize_t n = write(wakeupFd_, &one, sizeof one);
     if (n != sizeof one) {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
     }
}

// Eventloop中 调用Poller的方法
void EventLoop::updateChannel(Channel* channel) {
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel) {
    return poller_->hasChannel(channel);
}

// 执行回调 
void EventLoop::dePendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true; // 开始执行回调
    {
       std::unique_lock<std::mutex> lock(mutex_);
       functors.swap(pendingFunctors_); 
    }
    
    for (const Functor &functor : functors) {
        functor();
    }

    callingPendingFunctors_ = false;
}