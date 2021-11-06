#pragma once

#include <memory>
#include <experimental/source_location>

class TraceTimer
{
public:
    TraceTimer(const std::type_info& ti, int maxMs, const std::experimental::source_location& location = std::experimental::source_location::current());
    ~TraceTimer();

    TraceTimer(const TraceTimer &) = delete;
    TraceTimer(TraceTimer&&) = delete;

    void setInfo(const char* str);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
