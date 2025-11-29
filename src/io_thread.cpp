#include "io_thread.hpp"
#include "session.hpp"

IOThread::IOThread(int index) : _event_count(1024), _stop(true), _index(index), _expanded_once(false) {
    _event_fd = eventfd(0, EFD_NONBLOCK);
    if (_event_fd == -1) {
        perror("eventfd");
        exit(1);
    }
    _epoll_fd = epoll_create1(0);
    if (_epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    auto add_res = add_fd(_event_fd, EPOLLIN);
    if (!add_res) {
        perror("epoll add eventfd failed!\n");
        exit(1);
    }
    _event_addr = (struct epoll_event*)malloc(sizeof(epoll_event) * _event_count);
    if (_event_addr == nullptr) {
        perror("malloc events failed: ");
        exit(1);
    }
}

IOThread::~IOThread() {
    std::cout << "IOThread 【" << _index << "】exit" << std::endl;
}

int IOThread::set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void IOThread::enqueue_new_conn(int fd) {
    auto task = std::make_shared<IOTask>(fd, TaskType::RegisterConn);
    enqueue_task(task);
}

void IOThread::catche_new_conn(int fd) {
    auto task = std::make_shared<IOTask>(fd, TaskType::RegisterConn);
    {
        std::lock_guard<std::mutex> lk(_task_mtx);
        _tasks.push(task); 
    }
}

void IOThread::start() {
    _stop = false;
    _thread = std::thread([this]{this->loop();});
}

void IOThread::wakeup() {
    uint64_t one = 1;
    auto n = write(_event_fd, &one, sizeof(one));
}

void IOThread::stop() {
    _stop = true;
    auto task = std::make_shared<IOTask>(_event_fd, TaskType::Shutdown);
    enqueue_task(task);
}

void IOThread::join() {
    if (_thread.joinable()) {
        _thread.join();
    }
    std::cout << "IOThread join exit" << std::endl;
}

void IOThread::enqueue_task(std::shared_ptr<IOTask> task) {
    {
        std::lock_guard<std::mutex> lk(_task_mtx);
        _tasks.push(task);
    }
    wakeup();
}

void IOThread::enqueue_send_data(int fd, const std::string &msg, int msgtype) {
    auto task = std::make_shared<IOTask>(fd, TaskType::SendData, msg, msgtype);
    enqueue_task(task);
}

void IOThread::loop() {
    while (!_stop) {
        // 无线阻塞 直到有事件
        int nfds = epoll_wait(_epoll_fd, _event_addr, _event_count, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        if ((size_t)nfds == _event_count && !_expanded_once) {
            size_t new_count = _event_count * 2;
            struct epoll_event *new_addr = (struct epoll_event*)malloc(sizeof(epoll_event) * new_count);
            if (!new_addr) {
                perror("realloc event_addr");
            }
            else {
                free(_event_addr);
                _event_addr = new_addr;
                _event_count = new_count;
                std::cout << "expanded event_addr to " << _event_count << " size";
            }
            _expanded_once = true;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = _event_addr[i].data.fd;
            uint32_t evs = _event_addr[i].events;
            // 表示socket出错或者对端关闭
            if (evs & (EPOLLERR | EPOLLHUP)) {
                int err = 0, errlen = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, (socklen_t*)&errlen);
                fprintf(stderr, "fd=%d error: %s\n", fd, strerror(err));
                clear_fd(fd);
                continue;
            }

            if (fd == _event_fd) {
                uint64_t cnt;
                read(_event_fd, &cnt, sizeof(cnt));
                auto deal_res = deal_enque_tasks();
                if (!deal_res) {
                    std::cout << "io_thread receive exit eventfd" << std::endl;
                    return;
                }
                continue;
            }

            if (evs & (EPOLLIN | EPOLLOUT)) {
                while(1) {
                    auto iter = _sessions.find(fd);
                    if (iter == _sessions.end()) {
                        continue;
                    }
                    auto sess = iter->second;
                    if (evs & EPOLLIN) {
                        while(1) {
                            if (sess->_recv_stage == NO_RECV || sess->_recv_stage == HEAD_RECVING) {
                                int head_res = read_head_data(sess);
                                if (head_res == IO_CONTINUE) {
                                    continue;
                                }
                                if (head_res == IO_ERROR) {
                                    clear_fd(fd);
                                    break;
                                }
                                if (head_res == IO_EAGAIN) {
                                    break;
                                }
                                // 成功则继续轮询
                                continue;
                            }
                            if (sess->_recv_stage == BODY_RECVING) {
                                int body_res = read_body_data(sess);
                                if (body_res == IO_CONTINUE) {
                                    continue;
                                }
                                if (body_res == IO_ERROR) {
                                    clear_fd(fd);
                                    break;
                                }
                                if (body_res == IO_EAGAIN) {
                                    break;
                                }
                                continue;
                            }
                        }
                    }
                    if (evs & EPOLLOUT) {
                        if (sess->_send_stage == SENDING) {
                            continue;
                        }
                        handle_epollout(sess);
                    }
                    continue;
                }
            }
        }
    }
}

/************************************
 *   @section Private Member Function
 *   @author  Snow
 *   @brief   private member function
 ************************************/
bool IOThread::deal_enque_tasks() {
    std::queue<std::shared_ptr<IOTask>> q;
    // 拷贝并清空队列
    {
        std::lock_guard<std::mutex> lk(_task_mtx);
        std::swap(q, _tasks);
    }
    while (!q.empty()) {
        auto task = q.front();
        q.pop();
        if (task->_type == TaskType::RegisterConn) {
            // Session和IOThread建立关系
            auto sess = std::make_shared<Session>(task->_fd, this);
            _sessions[task->_fd] = sess;
            set_nonblocking(task->_fd);
            add_fd(task->_fd, EPOLLIN | EPOLLOUT);
            continue;
        }
        if (task->_type == TaskType::SendData) {
            auto iter = _sessions.find(task->_fd);
            if (iter == _sessions.end()) {
                continue;
            }
            auto data_buf = std::make_shared<DataBuf>(task->_msgtype, task->_data, task->_data.size());
            if (iter->second->_send_stage == SendStage::SENDING) {
                iter->second->enqueue_data(data_buf);
                continue;
            }
            // 没有数据发送，则直接发送
            auto send_res = iter->second->send_data(data_buf);
            if (send_res == IO_ERROR) {
                _sessions.erase(task->_fd);
                del_fd(task->_fd);
                close(task->_fd);
                continue;
            }
            if (send_res == IO_EAGAIN) {
                mod_fd(task->_fd, EPOLLET | EPOLLIN | EPOLLOUT);
                continue;
            }
        }

        if (task->_type == TaskType::Shutdown) {
            return false;
        }
    }
    return true;
}

bool IOThread::add_fd(int fd, int events)
{
    struct epoll_event ev2{};
    ev2.events = events;
    ev2.data.fd = fd;

    if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev2) == 0) return true;
    perror("fd add to epoll failed, try to modify\n");
    if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev2) == -1) {
        perror("fd modity failed: ");
        return false;
    }
    return true;
}

bool IOThread::mod_fd(int fd, int events) {
    struct epoll_event ev2{};
    ev2.events = events;
    ev2.data.fd = fd;

    if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev2) == -1) {
        perror("fd modity failed: ");
        return false;
    }
    return true;
}

bool IOThread::del_fd(int fd) {
    if (epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("fd delete failed: ");
        return false;
    }
    return true;
}

void IOThread::clear_fd(int fd) {
    _sessions.erase(fd);
    del_fd(fd);
    close(fd);
}

int IOThread::read_head_data(std::shared_ptr<Session> sess) {
    if (sess == nullptr) {
        return -1;
    }
    int remain = HEAD_LEN - sess->_head_buf->_offset;
    ssize_t read_len = read(sess->_fd, sess->_head_buf->_buf + sess->_head_buf->_offset, remain);
    if (read_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IO_EAGAIN;
        }
        if (errno == EINTR) {
            return IO_CONTINUE;
        }
        perror("read header failed: ");
        return IO_ERROR;
    }

    if (read_len == 0) {
        std::cout << "read peer closed , fd is " << sess->_fd << std::endl;
        return -1;
    }

    if (read_len < remain) {
        sess->_recv_stage = HEAD_RECVING;
        sess->_head_buf->_offset += read_len;
        return IO_CONTINUE;
    }

    uint8_t* hdr = (uint8_t*)(sess->_head_buf->_buf);
    uint16_t t, l;
    memcpy(&t, hdr, sizeof(t));
    memcpy(&l, hdr + 2, sizeof(l));
    uint16_t msg_type = ntohs(t);
    uint16_t body_len = ntohs(l);

    if (body_len > BUFF_SIZE) {
        std::cout << "msg body too big" << std::endl;
        return IO_ERROR;
    }

    sess->_recv_stage = BODY_RECVING;
    sess->_head_buf->_offset += read_len;
    sess->_data_buf = std::make_shared<DataBuf>(msg_type, body_len);
    return IO_CONTINUE;
}

int IOThread::read_body_data(std::shared_ptr<Session> sess) {
    int remain = sess->_data_buf->_data_len - sess->_data_buf->_offset;
    ssize_t read_len = read(sess->_fd, sess->_data_buf->_buf + sess->_data_buf->_offset, remain);

    if (read_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IO_EAGAIN;
        }

        if (errno == EINTR) {
            return IO_CONTINUE;
        }

        perror("read body");
        return IO_ERROR;
    }

    if (read_len == 0) {
        return IO_ERROR;
    }

    sess->_data_buf->_offset += read_len;
    if (read_len < remain) {
        return IO_CONTINUE;
    }

    std::string str(sess->_data_buf->_buf, sess->_data_buf->_data_len);
    sess->Send(sess->_data_buf->_type, str);

    // 回收消息
    sess->_data_buf = NULL;
    sess->_recv_stage = NO_RECV;
    memset(sess->_head_buf->_buf, 0, HEAD_LEN);
    sess->_head_buf->_offset = 0;
    return IO_CONTINUE;
}

void IOThread::handle_epollout(std::shared_ptr<Session> sess) {
    sess->_send_stage = SENDING;
    // RAII defer析构执行函数
    Defer defer([sess](){
        sess->_send_stage = NO_SEND;
    });

    while (!sess->_send_que.empty()) {
        auto send_data = sess->_send_que.front();
        while (send_data->_offset < send_data->_data_len) {
            auto write_len = write(sess->_fd, send_data->_buf + send_data->_offset, send_data->_data_len - send_data->_offset);
            if (write_len > 0) {
                send_data->_offset += write_len;
                continue;
            }
            if (write_len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                if (errno == EINTR) {
                    continue;
                }
                clear_fd(sess->_fd);
                return;
            }
            clear_fd(sess->_fd);
            return;
        }
        sess->_send_que.pop();
    }
    // 现在队列里面的数据发完，只有有数据时才需要监听EPOLLOUT（可写）事件
    struct epoll_event ev;
    ev.events = EPOLLIN || EPOLLET;
    ev.data.fd = sess->_fd;
    epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, sess->_fd, &ev);
}
