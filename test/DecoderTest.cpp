
#include "DecoderTest.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"

#include <QTest>
#include <QDebug>
#include <QTemporaryFile>
#include <QApplication>

QTEST_MAIN(DecoderTest)
#include "DecoderTest.moc"

class ImageDecoderUnderTest : public SmartImageDecoder
{
    friend class DecoderTest;
public:
    ImageDecoderUnderTest(QSharedPointer<Image> image) : SmartImageDecoder(image)
    {}
    
protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override
    {
        
    }

    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override
    {
        return QImage(desiredResolution, QImage::Format_ARGB32);
    }
};

void DecoderTest::initTestCase()
{
    Q_INIT_RESOURCE(ANPV);
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
    dec.close();
    
    QVERIFY(jpg.putChar('\0'));
    QVERIFY(jpg.flush());
    QCOMPARE(jpg.size(), 1);
    
    dec.open();
    dec.init();
    QCOMPARE(dec.decodingState(), DecodingState::Metadata);
}
