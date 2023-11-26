
#pragma once

#include "AfPointOverlay.hpp"

#include <memory>
#include <QString>
#include <QTransform>


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
    ExifWrapper(const ExifWrapper &);
    ExifWrapper &operator=(const ExifWrapper &);

    bool loadFromData(const QByteArray &data);
    QString errorMessage();
    qreal rotation();
    QTransform rotationMatrix();
    QTransform scaleMatrix();
    QTransform transformMatrix();
    int dotsPerMeterX();
    int dotsPerMeterY();
    QSize size();
    QSize sizeTransposed(QSize size);
    QString comment();
    QImage thumbnail();
    std::optional<std::tuple<std::vector<AfPoint>, QSize>> autoFocusPoints();

    QString aperture();
    bool aperture(double &quot);

    QString exposureTime();
    bool exposureTime(double &quot);
    bool exposureTime(long &num, long &den);

    QString iso();
    bool iso(int64_t &num);

    QString lens();

    bool focalLength(double &quot);
    QString focalLength();

    QDateTime dateRecorded();
    QString darkFrameSubtraction();
    bool isMirrorLockupEnabled(bool &isEnabled);

    QString formatToString();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
