
#include "TraceTimer.hpp"
#include "Formatter.hpp"
#include <QElapsedTimer>
#include <QDebug>
#include <string>

struct TraceTimer::Impl
{
    std::experimental::source_location location;
    int maxDuration; // in miliseconds
    std::string className;
    QElapsedTimer tim;
    const char* info = nullptr;
};

TraceTimer::TraceTimer(const std::type_info& ti, int maxMs, const std::experimental::source_location& location) : d(std::make_unique<Impl>())
{
    d->location = location;
    d->maxDuration = maxMs;
    d->className = std::string(ti.name());
    d->tim.start();
}

TraceTimer::~TraceTimer()
{
    Formatter f;
    
    int elapsed = d->tim.elapsed();
    if(elapsed > d->maxDuration)
    {
        f << "WARNING: This operation took longer than permitted!\n\t";
    }
    
    f << d->className << "::" << d->location.function_name() << "()\n"
    << "\tElapsed time: " << elapsed << " ms\n";
    if(d->info)
    {
        f << "\tAdditional info: " << d->info;
    }
    
    if(elapsed > 0)
    {
        if(elapsed > d->maxDuration)
        {
            qWarning() << f.str().c_str();
        }
    }
}

void TraceTimer::setInfo(const char* str)
{
    d->info = str;
}
