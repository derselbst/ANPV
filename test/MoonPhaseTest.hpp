
#pragma once

#include <QObject>

class MoonPhaseTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testPhase();
};
