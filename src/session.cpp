#include "session.hpp"
#include "io_thread.hpp"
#include "defer.hpp"

HeadBuf::HeadBuf(size_t head_len) :_head_len(head_len), _offset(0) {
    _buf = (char*)malloc(sizeof(char) * head_len);
    if (_buf) {
        perror("malloc head buf failed!\n");
        exit(EXIT_FAILURE);
    }
}

HeadBuf::~HeadBuf() {
    if (_buf) {
        std::free(_buf);
        _buf = nullptr;
    }
}

// 接收用构造函数 从socket中收到包头时就已经知道消息体的长度
DataBuf::DataBuf(uint16_t type, size_t data_len) :_type(type), _data_len(data_len), _offset(0) { 
    _buf = static_cast<char*>(std::malloc(data_len));
    if (_buf) {
        perror("malloc data buf failed!\n");
        exit(EXIT_FAILURE);
    }
}

// 发送用构造函数 需要自己将消息体与包头拼接成一个完整的数据包
DataBuf::DataBuf(uint16_t type, std::string data, size_t data_len) :_type(type), _data_len(data_len + HEAD_LEN), _offset(0) {
    _buf = static_cast<char*>(std::malloc(_data_len));
    if (_buf) {
        perror("malloc data buf failed!\n");
        exit(EXIT_FAILURE);
    }
    uint16_t net_type = htons(type);
    uint16_t net_len = htons(data_len);
    memcpy(_buf, &net_type, 2);
    memcpy(_buf + 2, &net_len, 2);
    memcpy(_buf + 4, data.data(), data_len);
}

DataBuf::~DataBuf() {
    if (_buf) {
        std::free(_buf);
        _buf = nullptr;
    }
}

Session::Session(int fd, IOThread *pthread) {
    _head_buf = std::make_shared<HeadBuf>(HEAD_LEN);
    _data_buf = nullptr;
    _recv_stage = NO_RECV;
    _send_stage = NO_SEND;
}

Session::~Session() {

}

void Session::Send(int msg_type, const std::string &data) {
   
}

void Session::enqueue_data(std::shared_ptr<DataBuf> data) {
    _send_que.push(data);
}

int Session::send_data(std::shared_ptr<DataBuf> data) {
    _send_stage = SENDING;
    Defer defer([this](){
        this->_send_stage = NO_SEND;
    });

    _send_que.push(data);
    while (!_send_que.empty()) {
        auto send_node = _send_que.front();
        while (send_node->_offset < send_node->_data_len) {
            auto result = write(_fd, send_node->_buf + send_node->_offset, send_node->_data_len - send_node->_offset);
            if (result > 0) {
                send_node->_offset += result;
                continue;
            }
            if (result < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return IO_EAGAIN;
                }
                if (errno == EINTR) {
                    continue;
                }
                perror("send failed: ");
                return IO_ERROR;
            }
            if (result == 0) {
                std::cout << "send peer closed, fd is " << _fd << std::endl;
                return IO_ERROR;
            }
        }
        if (send_node->_offset == send_node->_data_len) {
            _send_que.pop();
            continue;
        }
    }
    return IO_SUCCESS;
}
