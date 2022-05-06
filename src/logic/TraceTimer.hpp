#pragma once

#include <version>
#include <memory>
#include <string>

#if defined(__cpp_lib_source_location) && (__cpp_lib_source_location == 201907L)
#include <source_location>
using source_loc = std::source_location;
#else
#include <experimental/source_location>
using source_loc = std::experimental::source_location;
#endif



class TraceTimer
{
public:
    TraceTimer(const std::type_info& ti, int maxMs, const source_loc& location = source_loc::current());
    ~TraceTimer();

    TraceTimer(const TraceTimer &) = delete;
    TraceTimer(TraceTimer&&) = delete;

    void setInfo(const char* str);
    void setInfo(std::string&& str);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
