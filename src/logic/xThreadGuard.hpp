#pragma once

#include "Formatter.hpp"

#include <QThread>
#include <QObject>

#include <stdexcept>

class xThreadGuard
{
public:
    xThreadGuard(const QObject* o) : xThreadGuard(o->thread())
    {}
    xThreadGuard(const QThread * thrd)
    {
        if(QThread::currentThread() != thrd)
        {
            throw std::logic_error("Cross Thread Exception!");
        }
    }
    xThreadGuard(const xThreadGuard&) = delete;
    xThreadGuard(xThreadGuard&&) = delete;
    ~xThreadGuard() = default;
};
