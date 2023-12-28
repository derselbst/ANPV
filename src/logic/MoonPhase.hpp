
#pragma once

#include <QDateTime>

class MoonPhase
{
    enum { Full, Waning, Waxing, New };

public:
    static double fromDateTime(const QDateTime &t);
    static QString formatToString(double phase);
};
