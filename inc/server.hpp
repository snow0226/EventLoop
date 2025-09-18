#ifndef __SERVER_H__
#define __SERVER_H__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include "event_loop.hpp"

class Server {
public:
    Server(int port);
    ~Server();
    void run();
    void stop();
private:
    bool create_and_bind(int port);
    int set_nonblocking(int fd);
    int _port;
    int _listen_fd;
    int _epoll_fd;
    int _event_count;
    struct epoll_event* _event_addr;
    std::atomic<bool> _stop;
    int _event_fd;
    std::vector<int> _con_fds;
    std::shared_ptr<EventLoop> _loop;
};

#endif