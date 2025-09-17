#ifndef __EVENT_LOOP_H__
#define __EVENT_LOOP_H__

#include <vector>
#include <memory>
#include <atomic>
#include "io_thread.hpp"

class EventLoop {
public:
    EventLoop(int thread_num);
    ~EventLoop();
    void NotifyNewCons(std::vector<int>& conns);
    void StopIOThread();
private:
    std::vector<std::unique_ptr<IOThread>> _work_threads;
    std::atomic<size_t> _next_idx;
    int _thread_num;
};

#endif