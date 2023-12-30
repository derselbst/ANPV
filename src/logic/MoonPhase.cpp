
#include "MoonPhase.hpp"

#include <cmath>


int MoonPhase::fromDateTime(const QDateTime &t)
{
    constexpr double MeanSynodicMonth = 29.530588861; // 29.53058867; // 
    constexpr double cycle = /*std::floor*/(MeanSynodicMonth * 86400);

    static const QDateTime historicFullMoon(QDate(1999, 12, 22), QTime(18, 31, 18));
    static const qint64 historicFullMoonSec = historicFullMoon.toSecsSinceEpoch();

    qint64 now = t.toSecsSinceEpoch();

    auto phase = ((((now - historicFullMoonSec) / cycle) - floor((now - historicFullMoonSec) / cycle)) * 100);
    phase = std::round(phase);

    return static_cast<int>(phase);
}

double MoonPhase::calculateBrightness(int phase)
{
    double brightness = 0.0;

    if (phase <= 2 || 98 <= phase)
    {
        brightness = 100.0; // Vollmond
    }
    else if (2 < phase && phase < 48)
    {
        brightness = 50.0 + (48 - phase) * 50.0 / 46.0; // Abnehmender Mond
    }
    else if (48 <= phase && phase <= 52)
    {
        brightness = 0.0; // Neumond
    }
    else
    {
        brightness = 50.0 + (phase - 52) * 50.0 / 46.0; // Zunehmender Mond
    }

    return brightness;
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
