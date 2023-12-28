
#include "MoonPhase.hpp"

#include <cmath>


double MoonPhase::fromDateTime(const QDateTime &t)
{
    constexpr double MeanSynodicMonth = 29.53058867;

    auto day = t.date().day();
    auto month = t.date().month();
    auto year = t.date().year();

    // Umwandlung des Datums in Julianisches Datum
    int jd = day - 32075 + 1461 * (year + 4800 + (month - 14) / 12) / 4 + 367 * (month - 2 - (month - 14) / 12 * 12) / 12 - 3 * ((year + 4900 + (month - 14) / 12) / 100) / 4;

    // Berechnung der Mondphase (simplifizierte Formel)
    double phase = jd - 2451550.1;
    phase = phase - std::floor(phase / MeanSynodicMonth) * MeanSynodicMonth;

    return phase;
}

QString MoonPhase::formatToString(double phase)
{
    // Berechnung der relativen Helligkeit
    double brightness = 0;
    QString phaseName;

    if (phase < 1 || phase >= 29)
    {
        phaseName = QStringLiteral("New Moon (%1%)");
        brightness = 0;
    }
    else if (phase < 7.4)
    {
        phaseName = QStringLiteral("Waxing Moon (%1%)");
        brightness = phase / 7.4 * 50;
    }
    else if (phase < 22.1)
    {
        phaseName = QStringLiteral("Full Moon (%1%)");
        brightness = (22.1 - phase) / 14.7 * 50 + 50;
    }
    else
    {
        phaseName = QStringLiteral("Waning Moon (%1%)");
        brightness = (29 - phase) / 7.4 * 50;
    }

    return phaseName.arg(QString::number(static_cast<long>(brightness+0.5)));
}
