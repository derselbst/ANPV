
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
    dec.reset();
    verifyDecodingState(imageJpg, spy, DecodingState::Metadata);
    QVERIFY(imageJpg->errorMessage().isEmpty());
    
    QCOMPARE(spy.count(), 0);
}
