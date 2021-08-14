
#include "TraceTimer.hpp"
#include "Formatter.hpp"
#include <QElapsedTimer>
#include <QDebug>
#include <string>

struct TraceTimer::Impl
{
    std::experimental::source_location location;
    std::string className;
    QElapsedTimer tim;
    const char* info = nullptr;
};

TraceTimer::TraceTimer(const std::type_info& ti, const std::experimental::source_location& location) : d(std::make_unique<Impl>())
{
    d->location = location;
    d->className = std::string(ti.name());
    d->tim.start();
}

TraceTimer::~TraceTimer()
{
    Formatter f;
    f << d->className << "::" << d->location.function_name() << "()\n"
    << "\tElapsed time: " << d->tim.elapsed() << " ms\n";
    if(d->info)
    {
        f << "\tAdditional info: " << d->info;
    }
    qDebug() << f.str().c_str();
}

void TraceTimer::setInfo(const char* str)
{
    d->info = str;
}
