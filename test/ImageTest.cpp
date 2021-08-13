
#include "ImageTest.hpp"
#include "DecoderFactory.hpp"
#include "Image.hpp"

#include <QTest>
#include <QDebug>
#include <QTemporaryFile> 

QTEST_MAIN(ImageTest)
#include "ImageTest.moc"

void ImageTest::testRawImageHasNoSilblings()
{
    QTemporaryFile jpg("anpvtestfile-XXXXXX.jpg"), raw("anpvtestfile-XXXXXX.cr2"), tif("anpvtestfile-XXXXXX.tif");
    jpg.open();
    raw.open();
    tif.open();
    
    QVERIFY(jpg.isOpen());
    QVERIFY(raw.isOpen());
    QVERIFY(tif.isOpen());
    
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    QSharedPointer<Image> imageRaw = DecoderFactory::globalInstance()->makeImage(QFileInfo(raw));
    QSharedPointer<Image> imageTif = DecoderFactory::globalInstance()->makeImage(QFileInfo(tif));
    
    QVERIFY(!imageJpg->isRaw());
    QVERIFY(!imageJpg->hasEquallyNamedJpeg());
    QVERIFY(!imageJpg->hasEquallyNamedTiff());
    
    QVERIFY(imageRaw->isRaw());
    QVERIFY(!imageRaw->hasEquallyNamedJpeg());
    QVERIFY(!imageRaw->hasEquallyNamedTiff());
    
    QVERIFY(!imageTif->isRaw());
    QVERIFY(!imageTif->hasEquallyNamedJpeg());
    QVERIFY(!imageTif->hasEquallyNamedTiff());
    
}

void ImageTest::testRawImageHasSilblings()
{
    QFile jpg("anpvtestfile.jpg"), raw("anpvtestfile.cr2"), tif("anpvtestfile.tif");
    jpg.open(QIODevice::WriteOnly);
    raw.open(QIODevice::WriteOnly);
    tif.open(QIODevice::WriteOnly);
    
    QVERIFY(jpg.isOpen());
    QVERIFY(raw.isOpen());
    QVERIFY(tif.isOpen());
    
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    QSharedPointer<Image> imageRaw = DecoderFactory::globalInstance()->makeImage(QFileInfo(raw));
    QSharedPointer<Image> imageTif = DecoderFactory::globalInstance()->makeImage(QFileInfo(tif));
    
    QVERIFY(!imageJpg->isRaw());
    QVERIFY(!imageJpg->hasEquallyNamedJpeg());
    QVERIFY(imageJpg->hasEquallyNamedTiff());
    
    QVERIFY(imageRaw->isRaw());
    QVERIFY(imageRaw->hasEquallyNamedJpeg());
    QVERIFY(imageRaw->hasEquallyNamedTiff());
    
    QVERIFY(!imageTif->isRaw());
    QVERIFY(imageTif->hasEquallyNamedJpeg());
    QVERIFY(!imageTif->hasEquallyNamedTiff());
    
    jpg.remove();
    raw.remove();
    tif.remove();
}
