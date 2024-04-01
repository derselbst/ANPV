
#include "ImageTest.hpp"
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <QTest>
#include <QDebug>
#include <QTemporaryFile>
#include <QApplication>

QTEST_MAIN(ImageTest)
#include "ImageTest.moc"

void ImageTest::initTestCase()
{
    Q_INIT_RESOURCE(ANPV);
    static ANPV a;
}

void ImageTest::testIconHeight()
{
    QTemporaryFile jpg("anpvtestfile-XXXXXX.jpg");
    jpg.open();
    QSharedPointer<Image> imageJpg = DecoderFactory::globalInstance()->makeImage(QFileInfo(jpg));
    
    std::vector<int> validSizes{1,10,100,500,1000};
    
    for(int elem : validSizes)
    {
        QPixmap icon = imageJpg->thumbnailTransformed(elem);
        QCOMPARE(icon.height(), elem);
    }
    
    std::vector<int> invalidSizes{0,-1,-10,-100,-500,-11000};
    
    for(int elem : invalidSizes)
    {
        QPixmap icon = imageJpg->thumbnailTransformed(elem);
        QVERIFY(icon.isNull());
    }
}

void ImageTest::testIconForNonExistingFile()
{
    QSharedPointer<Image> image = DecoderFactory::globalInstance()->makeImage(QFileInfo("filenotfound.zzz"));
    QPixmap pix = image->thumbnailTransformed(100);
    QVERIFY(!pix.isNull());
}
