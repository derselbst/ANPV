
#pragma once

#include <memory>
#include <QString>
#include <KExiv2/KExiv2>


namespace Exiv2
{
    class Image;
}

class QByteArray;
class AfPointOverlay;

/**
 * This helper class loads image using libexiv2, and takes care of exception
 * handling for the different versions of libexiv2.
 */
class ExifWrapper
{
public:
    ExifWrapper();
    ~ExifWrapper();

    bool loadFromData(const QByteArray& data);
    QString errorMessage();
    KExiv2Iface::KExiv2::ImageOrientation orientation();
    int dotsPerMeterX();
    int dotsPerMeterY();
    QSize size();
    QString comment();
    QImage thumbnail();
    std::unique_ptr<AfPointOverlay> autoFocusPoints();
    QString aperture();
    QString exposureTime();
    QString iso();
    QString lens();
    QString focalLength();
    QDateTime dateRecorded();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
