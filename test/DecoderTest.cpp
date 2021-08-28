
#include "DecoderTest.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"

#include <QTest>
#include <QDebug>
#include <QStringLiteral>
#include <QTemporaryFile>
#include <QApplication>

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

void DecoderTest::initTestCase()
{
    Q_INIT_RESOURCE(ANPV);
}

void DecoderTest::errorWhileOpeningFile()
{
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo("IdON0tEx1st.jpg"));
    ImageDecoderUnderTest dec(imageJpg);
    
    QCOMPARE(dec.decodingState(), DecodingState::Ready);
    QVERIFY_EXCEPTION_THROWN(dec.open(), std::runtime_error);
    QCOMPARE(dec.decodingState(), DecodingState::Fatal);
    QVERIFY(!dec.errorMessage().isEmpty());
    dec.reset();
    QCOMPARE(dec.decodingState(), DecodingState::Ready);
    QVERIFY(dec.errorMessage().isEmpty());
    dec.close();
}

void DecoderTest::testInitialize()
{
    QTemporaryFile jpg("anpvtestfile-XXXXXX.jpg");
    jpg.open();
    QVERIFY(jpg.isOpen());
    
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    ImageDecoderUnderTest dec(imageJpg);
    
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::logic_error);
    
    // try to open an empty file
    dec.open();
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::runtime_error);
    QCOMPARE(dec.decodingState(), DecodingState::Fatal);
    dec.reset();
    QCOMPARE(dec.decodingState(), DecodingState::Ready);
    QVERIFY(dec.errorMessage().isEmpty());
    dec.close();
    
    // try to open a non-empty file successfully
    QVERIFY(jpg.putChar('\0'));
    QVERIFY(jpg.flush());
    QCOMPARE(jpg.size(), 1);
    dec.open();
    dec.init();
    QCOMPARE(dec.decodingState(), DecodingState::Metadata);
    dec.close();
    QCOMPARE(dec.decodingState(), DecodingState::Metadata);
    
    // try to open a non-empty file non-successfully with err msg
    dec.setDecodeHeaderFail(true);
    dec.open();
    QVERIFY_EXCEPTION_THROWN(dec.init(), std::runtime_error);
    QCOMPARE(dec.decodingState(), DecodingState::Fatal);
    QCOMPARE(dec.errorMessage(), QString(errHeader));
    dec.close();
    QCOMPARE(dec.decodingState(), DecodingState::Fatal);
    dec.reset();
    QCOMPARE(dec.decodingState(), DecodingState::Ready);
    QVERIFY(dec.errorMessage().isEmpty());
    
    dec.setDecodeHeaderFail(false);
    dec.setDecodingLoopFail(true);
    dec.open();
    dec.init();
    dec.decode(DecodingState::FullImage);
    QCOMPARE(dec.decodingState(), DecodingState::Error);
    QCOMPARE(dec.errorMessage(), QString(errDec));
    dec.close();
    QCOMPARE(dec.decodingState(), DecodingState::Error);
    QCOMPARE(dec.errorMessage(), QString(errDec));
    dec.reset();
    QCOMPARE(dec.decodingState(), DecodingState::Metadata);
    QVERIFY(dec.errorMessage().isEmpty());
}
