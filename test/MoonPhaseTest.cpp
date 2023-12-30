
#include "MoonPhaseTest.hpp"
#include "MoonPhase.hpp"

#include <QTest>
#include <QLabel>
#include <QDebug>
#include <QFuture>
#include <QPromise>
#include <QMainWindow>
#include <QApplication>


QTEST_MAIN(MoonPhaseTest)
#include "MoonPhaseTest.moc"

static void verifyNewMoon(int phase)
{
    QVERIFY(phase >= 48 && phase <= 52);
    QCOMPARE(MoonPhase::formatToString(phase), "New Moon");
}

static void verifyFullMoon(int phase)
{
    QVERIFY(phase <= 3 || 97 <= phase);
    QCOMPARE(MoonPhase::formatToString(phase), "Full Moon");
}

static void verifyWaningMoon(int phase)
{
    QVERIFY(phase > 3 && phase < 48);
    QCOMPARE(MoonPhase::formatToString(phase), "Waning Moon");
}

static void verifyWaxingMoon(int phase)
{
    QVERIFY(phase > 52);
    QCOMPARE(MoonPhase::formatToString(phase), "Waxing Moon");
}


void MoonPhaseTest::initTestCase()
{
}

void MoonPhaseTest::testPhase()
{
    QDateTime moonTime;
    int phase;

    // https://vollmond-info.de/mondkalender/mondkalender-2020-2/

    // Test Full Moon
    moonTime = QDateTime(QDate(1999, 12, 22), QTime(18, 31, 18));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyFullMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 100);

    moonTime = QDateTime(QDate(2022, 06, 14), QTime(13, 51, 00)); // Vollmond (Super-Vollmond)
    phase = MoonPhase::fromDateTime(moonTime);
    verifyFullMoon(phase); // Vollmond (Super-Vollmond)
    QCOMPARE(MoonPhase::calculateBrightness(phase), 100);

    moonTime = QDateTime(QDate(2022, 07, 13), QTime(20, 37, 00));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyFullMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 100);

    // Test New Moon
    moonTime = QDateTime(QDate(2022, 6, 29), QTime(04, 52, 0)); // Neumond (Mini-Neumond)
    phase = MoonPhase::fromDateTime(moonTime);
    verifyNewMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 0);

    moonTime = QDateTime(QDate(2022, 4, 30), QTime(22, 28, 0)); // Neumond (Black-Moon)
    phase = MoonPhase::fromDateTime(moonTime);
    verifyNewMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 0);
	
    moonTime = QDateTime(QDate(2000, 1, 6), QTime(18, 14, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyNewMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 0);

    // Test Waning Moon
    moonTime = QDateTime(QDate(1999, 12, 29), QTime(18, 0, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaningMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 77);

    moonTime = QDateTime(QDate(2022, 06, 21), QTime(05, 10, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaningMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 77);

    moonTime = QDateTime(QDate(2022, 07, 20), QTime(16, 18, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaningMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 78);


    // Test Waxing Moon
    moonTime = QDateTime(QDate(2000, 1, 13), QTime(18, 0, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaxingMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 74);

    moonTime = QDateTime(QDate(2022, 7, 7), QTime(04, 14, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaxingMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 77);

    // AAAD0551
    moonTime = QDateTime(QDate(2020, 6, 26), QTime(23, 15, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaxingMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 70);

    // AAAE2137
    moonTime = QDateTime(QDate(2020, 9, 19), QTime(20, 54, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyWaxingMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 57);

    // historic full moon
    moonTime = QDateTime(QDate(2023, 6, 4), QTime(5, 42, 0));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyFullMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 100);

    // AAAP5633
    moonTime = QDateTime(QDate(2023, 6, 4), QTime(21, 47, 3));
    phase = MoonPhase::fromDateTime(moonTime);
    verifyFullMoon(phase);
    QCOMPARE(MoonPhase::calculateBrightness(phase), 100);
}
