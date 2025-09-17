#ifndef __SESSION_H__
#define __SESSION_H__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <arpa/inet.h>
#include <unistd.h>
#include "global.hpp"

// 接受状态
enum RecvStage{
    NO_RECV = 0,  
    HEAD_RECVING,
    BODY_RECVING,
};

//发送状态
enum SendStage{
    NO_SEND = 0,
    SENDING = 1
};

//头部buffer
class HeadBuf{
public:
    HeadBuf(size_t head_len);
    ~HeadBuf();
    //缓存接受的头部信息
    char* _buf;
    //头部总长度
    size_t _head_len;
    //头部已经接受的偏移量
    size_t _offset;
};

//数据buffer,用来存储接受或者发送的数据
class DataBuf{
public:
    DataBuf(uint16_t type, size_t data_len);
    DataBuf(uint16_t type, std::string data , size_t data_len);
    ~DataBuf();
    //数据类型id, 比如1001表示登录，1002表示聊天等。
    uint16_t _type;
    //接受或发送缓存
    char* _buf;
    //数据总长度
    size_t _data_len;
    //已接受或发送的偏移量
    size_t _offset;
};

class IOThread;
class Session {
public:
    friend class IOThread;
    Session(int fd, IOThread* pthread);
    ~Session();
    void Send(int msg_type, const std::string& data);

private:
    void enqueue_data(std::shared_ptr<DataBuf> data);
    int send_data(std::shared_ptr<DataBuf> data);
    friend class IOThread;

private:
    int _fd;
    std::shared_ptr<DataBuf> _data_buf;
    std::shared_ptr<HeadBuf> _head_buf;
    enum RecvStage _recv_stage;
    enum SendStage _send_stage;
    std::mutex _send_mtx;
    std::queue<std::shared_ptr<DataBuf>> _send_que;
    IOThread* _p_ownerthread;
};

#endif