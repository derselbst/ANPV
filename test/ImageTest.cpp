
#include "ImageTest.hpp"
#include "DecoderFactory.hpp"
#include "Image.hpp"

#include <QTest>
#include <QDebug>
#include <QTemporaryFile>
#include <QApplication>

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

void ImageTest::testIconHeight()
{
    QTemporaryFile jpg("anpvtestfile-XXXXXX.jpg");
    jpg.open();
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    
    std::vector<int> validSizes{1,10,100,500,1000};
    
    for(int elem : validSizes)
    {
        QPixmap icon = imageJpg->icon(elem);
        QCOMPARE(icon.height(), elem);
    }
    
    std::vector<int> invalidSizes{0,-1,-10,-100,-500,-11000};
    
    for(int elem : invalidSizes)
    {
        QPixmap icon = imageJpg->icon(elem);
        QVERIFY(icon.isNull());
    }
}

void ImageTest::testIconForNonExistingFile()
{
    QSharedPointer<Image> image = DecoderFactory::globalInstance()->makeImage(QFileInfo("filenotfound.zzz"));
    QPixmap pix = image->icon(100);
    QVERIFY(!pix.isNull());
}
