#ifndef __IO_THREAD_H__
#define __IO_THREAD_H__

#include <queue>
#include <string>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <algorithm>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include "defer.hpp"

enum class TaskType {
    RegisterConn, SendData, Shutdown
};

class IOTask {
public:
    IOTask(int fd, TaskType type, std::string data="", int msgtype=0) :
    _fd(fd), _type(type), _data(data), _msgtype(msgtype) {}
    ~IOTask() = default;
    TaskType _type;
    int _fd;
    // 下面的字段在发送时才生效
    std::string _data;
    int _msgtype;
};

class NoneCopy {
protected:
    NoneCopy() = default;
private:
    NoneCopy(const NoneCopy&) = delete;
    NoneCopy& operator=(const NoneCopy&) = delete;
};

class Session;
class IOThread : public NoneCopy{
public:
    IOThread(int index);
    ~IOThread();
    int set_nonblocking(int fd);
    // catche_new_con：适合批量分发 fd，不触发立即唤醒。
    // enqueue_new_con：适合单个 fd 立即入队并唤醒线程。
    void enqueue_new_conn(int fd);              // 任务入队并wakeup IOThread线程去处理队列里面的任务
    void catche_new_conn(int fd);               //线程内部任务入队的接口
    void start();
    void wakeup();
    void stop();
    void join();
    void enqueue_task(std::shared_ptr<IOTask> task);
    void enqueue_send_data(int fd, const std::string& msg, int msgtype);
    void loop();
private:
    bool deal_enque_tasks();
    bool add_fd(int fd, int events);
    bool mod_fd(int fd, int events);
    bool del_fd(int fd);
    void clear_fd(int fd);
    int read_head_data(std::shared_ptr<Session> sess);
    int read_body_data(std::shared_ptr<Session> sess);
    void handle_epollout(std::shared_ptr<Session> sess);

    int _event_fd;                                                  // event fd 用于唤醒线程
    int _epoll_fd;                                                  // epoll fd
    std::mutex _task_mtx;                                           // 保护任务队列锁
    std::queue<std::shared_ptr<IOTask>> _tasks;                     // 任务队列
    struct epoll_event* _event_addr;                                // epoll等待数组
    int _event_count;                                               // epoll最大事件数
    std::thread _thread;                                            // std::thread对象
    std::atomic<bool> _stop;                                        // 线程停止标志
    int _index;                                                     // IOThread索引
    std::unordered_map<int, std::shared_ptr<Session>> _sessions;     // fd --> session 映射
    bool _expanded_once;                                            // 是否扩展过epoll_event数组
};

#endif