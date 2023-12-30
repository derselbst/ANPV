
#pragma once

#include <QDateTime>

class MoonPhase
{
    enum { Full, Waning, Waxing, New };

public:
    static int fromDateTime(const QDateTime &t);
    static double calculateBrightness(int phase);
    static QString formatToString(int phase);
};
