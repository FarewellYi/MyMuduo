#pragma once

#include <vector>
#include <string>

/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode

class Buffer {
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + kInitialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend) {

    }

    // 当前缓冲区可读字长
    size_t readableBytes() const {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const {
        return buffer_.size() - writerIndex_;
    }
    
    size_t prependableBytes() const {
        return readerIndex_; 
    }

    // 返回缓冲区中可读数据的起始地址
    const char* peek() const {
        return begin() + readerIndex_;
    }

    void retrieve(size_t len) {
        // 还有数据没读取
        if (len < readableBytes()) {
            readerIndex_ += len;
        }
        else {
            retrieveAll();
        }
    }

    // 进行复位
    void retrieveAll() {
        readerIndex_ = writerIndex_  = kCheapPrepend;
    }

    // 把onMessage上报的Buffer数据，转成string类型的数据再返回
    std::string retrieveAllAsString() {
        return retriveAsString(readableBytes()); // 应用可读取数据的长度
    }

    std::string retriveAsString(size_t len) {
        std::string result(peek(), len);
        retrieve(len);  // 上一句把缓冲区中的数据
        return result;
    }

    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
    }

    // 把【data，data + len】内存上的数据，添加到writable缓冲区中
    void append(const char *data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite() {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const {
        return begin() + writerIndex_;
    }

    // 从fd上读取数据
    ssize_t readFd(int fd, int* saveErrno);

    ssize_t writeFd(int fd, int* saveErrno);

private:
    char* begin() {
        return &*buffer_.begin(); // 返回首元素的地址，也是数组的起始地址
    }

    const char* begin() const {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len) {
        // 可读的 + 空闲的仍小于要读取的长度
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            buffer_.resize(writerIndex_ + len);
        }
        else {
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                    begin() + writerIndex_,
                    begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};