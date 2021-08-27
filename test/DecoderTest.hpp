
#pragma once

#include <QObject>

class DecoderTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testInitialize();
};
