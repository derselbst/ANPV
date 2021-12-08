#pragma once

#include <memory>
#include <source_location>

class TraceTimer
{
public:
    TraceTimer(const std::type_info& ti, int maxMs, const std::source_location& location = std::source_location::current());
    ~TraceTimer();

    TraceTimer(const TraceTimer &) = delete;
    TraceTimer(TraceTimer&&) = delete;

    void setInfo(const char* str);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
