#ifndef __DEFER_H__
#define __DEFER_H__

#include <functional>
class Defer {
public:
    Defer(std::function<void()> func): _func(func) {}
    ~Defer() {
        _func();
    }
private:
    std::function<void()> _func;
};

#endif