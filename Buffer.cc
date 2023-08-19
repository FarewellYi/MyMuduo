#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

ssize_t Buffer::readFd(int fd, int* saveErrno) {
    char extrabuf[65536] = {0};
    struct iovec vec[2];

    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writable < sizeof extrabuf) ? 2: 1;
    /*本节的关键函数 readv，即分散读，将零散内存块的数据读取到一处*/
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0) {
        *saveErrno = errno;
    }
    else if (n <= writable) {   // Buffer的可写缓冲区足以存储读出来的数据
        writerIndex_ += n;
    }
    else {  // extrabuf中也写入了数据
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable); // 从writeIndex_处开始写 n - writable大小的数据
    }
    return n;
}

ssize_t Buffer::writeFd(int fd, int* saveErrno) {
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) {
        *saveErrno = errno;
    }
    return n;
}