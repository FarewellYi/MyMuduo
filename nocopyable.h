#pragma once

/*
    派生类可以正常构造与析构
    但是不可以拷贝构造与赋值构造
*/

class nocopyable
{
public:
    nocopyable(const nocopyable&) = delete;
    nocopyable operator=(const nocopyable&) = delete;
protected:
    nocopyable() = default;
    ~nocopyable() = default;

};