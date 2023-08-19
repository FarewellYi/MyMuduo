#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <iostream>
#include <functional>
#include <string>

using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace placeholders;

/*
1、 组合TCPServerlei
2、创建EventLoop事件循环对象的指针
3、明确TcpServer的构造函数需要的参数，创建ChatServer的构造函数
4、在ChatServer的构造函数中，设置服务器的用户连接创建和断开的回调函数，以及读写事件的回调函数
*/

class ChatServer {
public:
    ChatServer(EventLoop* loop, // 事件循环
            const InetAddress& listenAddr, // IP + Port
            const string& nameArg)  // 服务器名称
        : _server(loop, listenAddr, nameArg)
        , _loop(loop) {
            // 服务器设置用户连接创建和断开的回调函数
            _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
            // 服务器设置用户读写事件的回调函数
            _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
            // 设置服务器端的线程数量， 1个IO线程， 3个工作线程
            _server.setThreadNum(4);
        }
    // 开启事件循环
    void start() { 
        _server.start();
    }
private:
    // 用户连接创建的回调函数 
    void onConnection(const TcpConnectionPtr& conn) {
        //cout << "onConnection(): new connection" << endl;
        
        if (conn->connected()) {
            cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << endl;
            cout << "state: online" << endl;
        }
        else {
            cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << endl;
            conn->shutdown();
            cout << "state: offline" << endl;
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time) {
        string buf = buffer->retrieveAllAsString();
        cout << "recv daa: " << buf << " time: " << time.toString() << endl;
        conn->send(buf);
    }

    TcpServer _server;
    EventLoop* _loop;

};

int main () {

    EventLoop loop; // epoll
    InetAddress addr("127.0.0.1", 8888);
    ChatServer server(&loop, addr, "ChatServer");

    server.start();
    loop.loop();    // epoll_wait 以阻塞的方式等待用户连接，已连接用户的读写事件等

    return 0;
}