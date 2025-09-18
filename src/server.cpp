#include "server.hpp"
#include "configmgr.hpp"

Server::Server(int port) : _port(port), _event_count(32), _stop(true) {
    auto b_res = create_and_bind(port);
    if (!b_res) {
        exit(EXIT_FAILURE);
    }

    auto n_res = set_nonblocking(_listen_fd);
    if (n_res == -1) {
        perror("fcntl error: ");
        exit(EXIT_FAILURE);
    }
    std::cout << "server listen_fd is " << _listen_fd << std::endl;

    _event_fd = eventfd(0, EFD_NONBLOCK);
    if (_event_fd == -1) {
        perror("eventfd");
        exit(EXIT_FAILURE);
    }
    std::cout << "server _event_fd is " << _event_fd << std::endl;

    _epoll_fd = epoll_create1(0);
    if (_epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    std::cout << "server _epoll_fd is " << _epoll_fd << std::endl;

    {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = _listen_fd;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _listen_fd, &ev) == -1) {
            perror("epoll_ctl add listen_fd");
            exit(EXIT_FAILURE);
        }
    }

    {
        struct epoll_event ev2;
        ev2.events = EPOLLIN;
        ev2.data.fd = _event_fd;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _event_fd, &ev2) == -1) {
            perror("epoll_ctl add event_fd");
            exit(EXIT_FAILURE);
        }
    }

    _event_addr = (struct epoll_event*)malloc(sizeof(epoll_event) * _event_count);
    if (_event_addr == nullptr) {
        perror("malloc events failed: ");
        exit(EXIT_FAILURE);
    }
    auto &cfg = ConfigMgr::Inst();
    int thread_num = cfg.get<int>("server.thread_num", 2);
    _loop = std::make_unique<EventLoop>(thread_num);
}

Server::~Server() {
    _loop->StopIOThread();
    if (_event_addr) {
        free(_event_addr);
        _event_addr = nullptr;
    }
    if (_listen_fd != -1) {
        close(_listen_fd);
    }
    if (_event_fd != -1) {
        close(_event_fd);
    }
    if (_epoll_fd != -1) {
        close(_epoll_fd);
    }
    std::cout << "Server exit" << std::endl;
}

void Server::run() {
    _stop = false;
    while (!_stop) {
        int nfds = epoll_wait(_epoll_fd, _event_addr, _event_count, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll wait");
            break;
        }
        for (int i = 0; i < nfds; i++) {
            int fd = _event_addr[i].data.fd;
            uint32_t evs = _event_addr[i].events;

            if (evs & (EPOLLERR | EPOLLHUP)) {
                int err = 0, errlen = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, (socklen_t*)&errlen);
                fprintf(stderr, "fd=%d  error: %s\n", fd, strerror(err));
                continue;
            }

            if (fd == _listen_fd) {
                struct sockaddr_in cli;
                socklen_t len = sizeof(cli);
                while (1) {
                    int conn_fd = accept(_listen_fd, (struct sockaddr*)&cli, &len);
                    if (conn_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)    break;
                        if (errno == EINTR) continue;
                        perror("accept");
                        break;
                    }

                    _con_fds.push_back(conn_fd);
                    printf("Accepted fd = %d", conn_fd);
                }
                if (!_con_fds.empty()) {
                    // 将新的连接交给EventLoop处理
                    _loop->NotifyNewCons(_con_fds);
                    _con_fds.clear();
                }
                continue;
            }

            if (fd == _event_fd) {
                uint64_t cnt;
                read(_event_fd, &cnt, sizeof(cnt));
                std::cout << "receive exit eventfd" << std::endl;
                return;
            }
        }
    }
}

void Server::stop() {
    _stop = true;
    uint64_t one = 1;
    write(_event_fd, &one, sizeof(one));
}

bool Server::create_and_bind(int port) {
    _listen_fd =socket(AF_INET, SOCK_STREAM, 0);
    if (_listen_fd < 0) {
        perror("socket");
        return false;
    }
    int opt = 1;
    setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_port);

    if (bind(_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return false;
    }
    if (listen(_listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        return false;
    }
    return true;
}

int Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
