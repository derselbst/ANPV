#pragma once

#include "Formatter.hpp"

#include <QThread>
#include <QObject>

#include <stdexcept>

class xThreadGuard
{
public:
    xThreadGuard(const QObject* o)
    {
        if(QThread::currentThread() != o->thread())
        {
            throw std::logic_error("Cross Thread Exception!");
        }
    }
    xThreadGuard(const xThreadGuard&) = delete;
    xThreadGuard(xThreadGuard&&) = delete;
    ~xThreadGuard() = default;
};
