
#include "DecoderTest.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <QTest>
#include <QDebug>
#include <QStringLiteral>
#include <QTemporaryFile>
#include <QApplication>
#include <QSignalSpy>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>

#include <thread>
#include <chrono>

QTEST_MAIN(DecoderTest)
#include "DecoderTest.moc"

constexpr const char errHeader[] = "Some header decode error";
constexpr const char errDec[]    = "Some decoding decode error";


class ImageDecoderUnderTest : public SmartImageDecoder
{
    friend class DecoderTest;
    bool decodeHeaderFail = false;
    bool decodingLoopFail = false;
public:
    void setDecodeHeaderFail(bool b)
    {
        this->decodeHeaderFail = b;
    }
    
    void setDecodingLoopFail(bool b)
    {
        this->decodingLoopFail = b;
    }
    
    ImageDecoderUnderTest(QSharedPointer<Image> image) : SmartImageDecoder(image)
    {}
    
protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override
    {
        if(this->decodeHeaderFail)
            throw std::runtime_error(errHeader);
    }

    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override
    {
        if(this->decodingLoopFail)
            throw std::runtime_error(errDec);
        
        return QImage(desiredResolution, QImage::Format_ARGB32);
    }
};

static void verifyDecodingState(QSharedPointer<Image>& i, QSignalSpy& spy, DecodingState newState)
{
    static DecodingState oldState = DecodingState::Ready;
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> sig = spy.takeFirst();
    QCOMPARE(sig.at(1).typeId(), QMetaType::UInt);
    QCOMPARE(sig.at(2).typeId(), QMetaType::UInt);
    QCOMPARE(sig.at(1).value<DecodingState>(), newState);
    QCOMPARE(sig.at(2).value<DecodingState>(), oldState);
    QCOMPARE(i->decodingState(), newState);
    
    oldState = newState;
}

void DecoderTest::initTestCase()
{
    Q_INIT_RESOURCE(ANPV);
    static ANPV a;
}

void DecoderTest::errorWhileOpeningFile()
{
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo("IdON0tEx1st.jpg"));
    ImageDecoderUnderTest dec(imageJpg);
    
    QCOMPARE(imageJpg->decodingState(), DecodingState::Ready);
    QVERIFY_EXCEPTION_THROWN(dec.open(), std::runtime_error);
    QCOMPARE(imageJpg->decodingState(), DecodingState::Fatal);
    QVERIFY(!imageJpg->errorMessage().isEmpty());
    dec.reset();
    QCOMPARE(imageJpg->decodingState(), DecodingState::Ready);
    QVERIFY(imageJpg->errorMessage().isEmpty());
    dec.close();
}

void DecoderTest::testInitialize()
{
    QTemporaryFile jpg("anpvtestfile-XXXXXX.jpg");
    jpg.open();
    QVERIFY(jpg.isOpen());
    
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    ImageDecoderUnderTest dec(imageJpg);
    
    QSignalSpy spy(imageJpg.data(), &Image::decodingStateChanged);
    // drop the first signal, it's null for some reason...
    (void)spy.takeFirst();
    
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::logic_error);
    verifyDecodingState(imageJpg, spy, DecodingState::Fatal);
    
    // try to open an empty file
    dec.open();
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::runtime_error);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(imageJpg->decodingState(), DecodingState::Fatal);
    dec.reset();
    verifyDecodingState(imageJpg, spy, DecodingState::Ready);
    QVERIFY(imageJpg->errorMessage().isEmpty());
    dec.close();
    
    // try to open a non-empty file successfully
    QVERIFY(jpg.putChar('\0'));
    QVERIFY(jpg.flush());
    QCOMPARE(jpg.size(), 1);
    dec.open();
    dec.init();
    verifyDecodingState(imageJpg, spy, DecodingState::Metadata);
    dec.close();
    QCOMPARE(imageJpg->decodingState(), DecodingState::Metadata);
    
    // try to open a non-empty file non-successfully with err msg
    dec.setDecodeHeaderFail(true);
    dec.open();
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::runtime_error);
    verifyDecodingState(imageJpg, spy, DecodingState::Fatal);
    QCOMPARE(imageJpg->errorMessage(), QString(errHeader));
    dec.close();
    QCOMPARE(imageJpg->decodingState(), DecodingState::Fatal);
    dec.releaseFullImage();
    QCOMPARE(spy.count(), 0); // no state change
    dec.reset();
    verifyDecodingState(imageJpg, spy, DecodingState::Ready);
    QVERIFY(imageJpg->errorMessage().isEmpty());
    
    dec.setDecodeHeaderFail(false);
    dec.setDecodingLoopFail(true);
    dec.open();
    dec.init();
    verifyDecodingState(imageJpg, spy, DecodingState::Metadata);
    dec.decode(DecodingState::FullImage);
    verifyDecodingState(imageJpg, spy, DecodingState::Error);
    QCOMPARE(imageJpg->errorMessage(), QString(errDec));
    dec.close();
    QCOMPARE(imageJpg->decodingState(), DecodingState::Error);
    QCOMPARE(imageJpg->errorMessage(), QString(errDec));
    dec.releaseFullImage();
    QCOMPARE(spy.count(), 0); // no state change
    dec.reset();
    verifyDecodingState(imageJpg, spy, DecodingState::Ready);
    QVERIFY(imageJpg->errorMessage().isEmpty());
    
    QCOMPARE(spy.count(), 0);
}

class MySleepyImageDecoder : public ImageDecoderUnderTest
{
    friend class DecoderTest;
    int sleep = 0;
public:
    explicit MySleepyImageDecoder() : ImageDecoderUnderTest(DecoderFactory::globalInstance()->makeImage(QFileInfo("IdON0tEx1st.jpg")))
    {}
    
    void setSleep(int sleep)
    {
        this->sleep = sleep;
    }
    
    void open() override {}
    void close() override {}
    void init() override
    {
        this->decodeHeader(nullptr, 0);
    }
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override
    {
        QThread::msleep(this->sleep);
        this->cancelCallback();
    }
    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override
    {
        // do not return a NULL image
        QImage img(1,1,QImage::Format_ARGB32);
        this->image()->setDecodedImage(img);
        return img;
    }
};

void DecoderTest::testResettingWhileDecoding()
{
    std::unique_ptr<MySleepyImageDecoder> dec(new MySleepyImageDecoder());
    dec->setAutoDelete(false);
    dec->setSleep(2000);
    QFutureWatcher<DecodingState> watcher;
    QFuture<DecodingState> fut = dec->decodeAsync(DecodingState::Metadata, Priority::Normal);

    connect(&watcher, &QFutureWatcher<DecodingState>::started, this,
            [&]()
            {
                // as soon as the decoding has started, try to reset the decoder
                QVERIFY_EXCEPTION_THROWN(dec->reset(), std::logic_error);
            });
    QSignalSpy spy(&watcher, &QFutureWatcher<DecodingState>::started);
    
    watcher.setFuture(fut);
    QDeadlineTimer deadline(10000);
    while(spy.count() == 0 && !deadline.hasExpired())
    {
        // manually run the event loop to get the events delivered
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, dec->sleep/4);
    }
    watcher.waitForFinished();

    QVERIFY(fut.isStarted());
    QVERIFY(watcher.isStarted());
    QVERIFY(fut.isFinished());
    QVERIFY(watcher.isFinished());
    QVERIFY(!fut.isRunning());
    QVERIFY(!watcher.isRunning());
    QCOMPARE(spy.count(), 1);
}

void DecoderTest::testFinishBeforeSettingFutureWatcher()
{
    MySleepyImageDecoder* dec = new MySleepyImageDecoder();
    dec->setAutoDelete(true);
    dec->setSleep(1);
    QFutureWatcher<DecodingState> watcher;
    QSignalSpy spyStartedBeforeStarted(&watcher, &QFutureWatcher<DecodingState>::started);
    QSignalSpy spyFinishedBeforeStarted(&watcher, &QFutureWatcher<DecodingState>::finished);
    
    QFuture<DecodingState> fut = dec->decodeAsync(DecodingState::Metadata, Priority::Normal);
    
    // at this point future is finished, dec has been deleted
    QThread::msleep(1000);
    watcher.setFuture(fut);
    
    QSignalSpy spyStartedAfterFinished(&watcher, &QFutureWatcher<DecodingState>::started);
    QSignalSpy spyFinishedAfterFinished(&watcher, &QFutureWatcher<DecodingState>::finished);
    
    fut.waitForFinished();
    watcher.waitForFinished();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    
    QVERIFY(fut.isStarted());
    QVERIFY(watcher.isStarted());
    QVERIFY(fut.isFinished());
    QVERIFY(watcher.isFinished());
    QVERIFY(!fut.isRunning());
    QVERIFY(!watcher.isRunning());
    
    QCOMPARE(spyStartedBeforeStarted.count(), 1);
    QCOMPARE(spyFinishedBeforeStarted.count(), 1);
    
    QCOMPARE(spyStartedAfterFinished.count(), 1);
    QCOMPARE(spyFinishedAfterFinished.count(), 1);
}

void DecoderTest::testAccessingDecoderWhileStillDecodingOngoing()
{
    std::unique_ptr<MySleepyImageDecoder> dec(new MySleepyImageDecoder());
    dec->setAutoDelete(false);
    dec->setSleep(5*1000);
    
    QFuture<DecodingState> fut = dec->decodeAsync(DecodingState::Metadata, Priority::Normal);
    
    QThread::msleep(100);
    QVERIFY(fut.isStarted());
    QVERIFY(fut.isRunning());
    QVERIFY(!fut.isCanceled());
    
    // fake the decodingState to Metadata, so that it will be propagated to dec->image(), to make the decodeAsync call below work as expected
    dec->setDecodingState(DecodingState::Metadata);
    // decoding a second time will return the same future
    QFuture<DecodingState> fut2 = dec->decodeAsync(DecodingState::Metadata, Priority::Normal);
    QVERIFY(fut.isStarted()); QVERIFY(fut2.isStarted());
    QVERIFY(fut.isRunning()); QVERIFY(fut2.isRunning());
    QVERIFY(!fut.isCanceled()); QVERIFY(!fut2.isCanceled());
    
    // decoding a third time with a different targetState will cancel the previous decoding
    QFuture<DecodingState> fut3 = dec->decodeAsync(DecodingState::PreviewImage, Priority::Normal);
    QVERIFY(fut.isStarted()); QVERIFY(fut2.isStarted());
    QVERIFY(!fut.isRunning()); QVERIFY(!fut2.isRunning());
    QVERIFY(fut.isCanceled()); QVERIFY(fut2.isCanceled());
    QThread::msleep(100);
    QVERIFY(fut3.isStarted());
    QVERIFY(fut3.isRunning());
    QVERIFY(!fut3.isCanceled());
    
    // will block until decoding done
    dec->releaseFullImage();
    QVERIFY(fut3.isFinished());
    QVERIFY(!fut3.isRunning());
}

void DecoderTest::testTakeDecoderFromThreadPoolBeforeDecodingCouldBeStarted()
{
    QThreadPool* qtp = QThreadPool::globalInstance();
    int maxThreads = qtp->maxThreadCount();
    qtp->setMaxThreadCount(1);
    
    bool tryTakeResult;
    qtp->start([&]()
    {
        MySleepyImageDecoder* dec = new MySleepyImageDecoder();
        dec->setAutoDelete(false);
        dec->setSleep(100*1000);
        
        // start the decoder
        dec->decodeAsync(DecodingState::Metadata, Priority::Normal);
        
        // immediately take it from the queue again. must always succeed as there is only one worker thread, and we're running it ;)
        tryTakeResult = qtp->tryTake(dec);
        
        // should not complain that decoding is ongoing
        dec->reset();
        
        delete dec;
    });
    
    qtp->waitForDone();
    QVERIFY(tryTakeResult);
    qtp->setMaxThreadCount(maxThreads);
}

