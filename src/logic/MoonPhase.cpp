
#include "MoonPhase.hpp"

#include <cmath>


int MoonPhase::fromDateTime(const QDateTime &t)
{
    static const QDateTime historicFullMoon(QDate(1999, 12, 22), QTime(18, 31, 18));
    static const qint64 historicFullMoonSec = historicFullMoon.toSecsSinceEpoch();

    static const double cycle = std::floor(29.530588861 * 86400);

    qint64 now = t.toSecsSinceEpoch();

    auto phase = std::round((((now - historicFullMoonSec) / cycle) - floor((now - historicFullMoonSec) / cycle)) * 100);

    return static_cast<int>(phase);
}

QString MoonPhase::formatToString(int phase)
{
    if(phase <= 2 || 98 <= phase)
    {
        return "Full Moon";
    }
    else if(2 < phase && phase < 48)
    {
        return "Waning Moon";
    }
    else if(48 <= phase && phase <= 52)
    {
        return "New Moon";
    }
    else
    {
        return "Waxing Moon";
    }
}
