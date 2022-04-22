
#pragma once

#include <QObject>

class DecoderTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void errorWhileOpeningFile();
    void testInitialize();
    void testResettingWhileDecoding();
    void testFinishBeforeSettingFutureWatcher();
    void testAccessingDecoderWhileStillDecodingOngoing();
    void testTakeDecoderFromThreadPoolBeforeDecodingCouldBeStarted();
};
