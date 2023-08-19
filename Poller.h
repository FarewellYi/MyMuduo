#pragma once

#include "nocopyable.h"
#include "Timestamp.h"
#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

// muduo库中多路事件分发器的核心IO复用模块的底层类
class Poller: nocopyable {
public:
    using ChannelList = std::vector<Channel*>;  // poller监听到的发生事件的fd，以及每个fd都发生了什么事件（保存在channel中）

    Poller(EventLoop* loop);
    virtual ~Poller();

    // 给所有IO复用保留同意的接口
    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;

    // 判断所查询channel是否在当前Poller中
    bool hasChannel(Channel* channel) const;

    // EventLoop可以通过该接口获取默认的IO复用的具体实现
    static Poller* newDefaultPoller(EventLoop* loop);

protected:
    // channelmap的key：sockfd，value：sockfd所属的channel通道
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;

private:
    EventLoop* ownerLoop_;
};