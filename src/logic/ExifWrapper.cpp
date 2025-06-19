
#include "ExifWrapper.hpp"

#include "AfPointOverlay.hpp"
#include "Formatter.hpp"
#include "MoonPhase.hpp"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QPoint>
#include <QRect>
#include <cmath>
#include <QDebug>
#include <QRegularExpression>
#include <KExiv2/KExiv2>
#include <optional>
#include <iomanip>
#include <array>
#include <limits>

using OR = KExiv2Iface::KExiv2::ImageOrientation;

struct ExifWrapper::Impl
{
    KExiv2Iface::KExiv2 mExivHandle;

    std::optional<KExiv2Iface::KExiv2::ImageOrientation> cachedOrientation;

    int dotsPerMeter(const QString &keyName)
    {
        QString keyVal = QStringLiteral("Exif.Image.") + keyName;
        int64_t res, val;

        if(!mExivHandle.getExifTagInt64("Exif.Image.ResolutionUnit", res) ||
                !mExivHandle.getExifTagInt64(keyVal.toLocal8Bit().data(), val))
        {
            return 0;
        }

        // The unit for measuring XResolution and YResolution. The same unit is used for both XResolution and YResolution.
        //     If the image resolution in unknown, 2 (inches) is designated.
        //         Default = 2
        //         2 = inches
        //         3 = centimeters
        //         Other = reserved
        constexpr float INCHESPERMETER = (100. / 2.54);

        switch(res)
        {
        case 3:  // dots per cm
            return int(val * 100);

        default:  // dots per inch
            return int(val * INCHESPERMETER);
        }

        return 0;
    }

    KExiv2Iface::KExiv2::ImageOrientation orientation()
    {
        if(!this->cachedOrientation)
        {
            // getImageOrientation() is a bit expensive down in Exiv2
            this->cachedOrientation = this->mExivHandle.getImageOrientation();
        }

        return this->cachedOrientation.value();
    }

    static QTransform scaleMatrix(OR orientation)
    {
        QTransform matrix;

        switch(orientation)
        {
        case OR::ORIENTATION_HFLIP:
        case OR::ORIENTATION_ROT_90_HFLIP:
            matrix.scale(-1, 1);
            break;

        case OR::ORIENTATION_VFLIP:
        case OR::ORIENTATION_ROT_90_VFLIP:
            matrix.scale(1, -1);
            break;

        default:
            break;
        }

        return matrix;
    }

    static int rotation(OR orientation)
    {
        int rot = 0;

        switch(orientation)
        {
        default:
            break;

        case OR::ORIENTATION_ROT_180:
            rot = 180;
            break;

        case OR::ORIENTATION_ROT_90_HFLIP:
        case OR::ORIENTATION_ROT_90:
        case OR::ORIENTATION_ROT_90_VFLIP:
            rot = 90;
            break;

        case OR::ORIENTATION_ROT_270:
            rot = 270;
            break;
        }

        return rot;
    }

    static QString boolToString(bool b)
    {
        if(b)
        {
            return QStringLiteral("enabled");
        }

        return QStringLiteral("disabled");
    }

    // Borrowed from https://gitlab.com/lspies/photoqt
    QPointF convertGPSToDecimal(QString gpsLatRef, QString gpsLat, QString gpsLonRef, QString gpsLon)
    {
        bool ok;
        const QStringList lat = gpsLat.split(" ");
        const QStringList lon = gpsLon.split(" ");
        if(lat.length() != 3 || lon.length() != 3)
            return QPointF();

        double x = 0, y = 0;

        constexpr const std::array<double,3> div = {1, 60, 3600};

        for(int i = 0; i < 3; ++i) {

            double xval = 0;

            if(lat.at(i).contains("/")) {
                const QStringList p = lat.at(i).split("/");
                const double one = p.at(0).toDouble();
                const double two = p.at(1).toDouble();

                xval = one;
                if(two != 0)
                    xval /= two;

            }
            else
            {
                QString token = lat.at(i);
                xval = token.left(token.indexOf(QRegularExpression("[^0-9]"))).toDouble(&ok);
                if(!ok)
                {
                    qWarning() << "GPS: unable to parse latitude '" << token << "' as double";
                    return QPointF();
                }
            }

            x += xval/div.at(i);

            double yval = 0;

            if(lon.at(i).contains("/")) {
                const QStringList p = lon.at(i).split("/");
                const double one = p.at(0).toDouble();
                const double two = p.at(1).toDouble();

                yval = one;
                if(two != 0)
                    yval /= two;

            }
            else
            {
                QString token = lon.at(i);
                yval = token.left(token.indexOf(QRegularExpression("[^0-9]"))).toDouble(&ok);
                if(!ok)
                {
                    qWarning() << "GPS: unable to parse longitude '" << token << "' as double";
                    return QPointF();
                }
            }
            y += yval/div.at(i);

        }

        if(gpsLatRef.toLower().at(0) == 's')
            x *= -1;

        if(gpsLonRef.toLower().at(0) == 'w')
            y *= -1;

        return QPointF(x,y);

    }
};

ExifWrapper::ExifWrapper()
    : d(std::make_unique<Impl>())
{
}

ExifWrapper::~ExifWrapper() = default;

ExifWrapper::ExifWrapper(const ExifWrapper &other) : d(nullptr)
{
    if(other.d)
    {
        d = std::make_unique<Impl>(*other.d);
    }
}

ExifWrapper &ExifWrapper::operator=(const ExifWrapper &other)
{
    if(!other.d)
    {
        d.reset();
    }
    else if(!d)
    {
        d = std::make_unique<Impl>(*other.d);
    }
    else
    {
        *d = *other.d;
    }

    return *this;
}

// NOTE: Exiv2 takes ownership of data, so the caller must keep a reference to it to avoid use-after-free!
bool ExifWrapper::loadFromData(const QByteArray &data)
{
    d->cachedOrientation.reset();
    return d->mExivHandle.loadFromData(data);
}

QString ExifWrapper::errorMessage()
{
    return d->mExivHandle.getErrorMessage();
}

qreal ExifWrapper::rotation()
{
    return d->rotation(d->orientation());
}

QTransform ExifWrapper::rotationMatrix()
{
    QTransform rotationMatrix;
    rotationMatrix.rotate(this->rotation());
    return rotationMatrix;
}

QTransform ExifWrapper::scaleMatrix()
{
    return d->scaleMatrix(d->orientation());
}

QTransform ExifWrapper::transformMatrix()
{
    return this->scaleMatrix() * this->rotationMatrix();
}

int ExifWrapper::dotsPerMeterX()
{
    return d->dotsPerMeter(QStringLiteral("XResolution"));
}

int ExifWrapper::dotsPerMeterY()
{
    return d->dotsPerMeter(QStringLiteral("YResolution"));
}

QSize ExifWrapper::size()
{
    return this->sizeTransposed(d->mExivHandle.getImageDimensions());
}

QSize ExifWrapper::sizeTransposed(QSize size)
{
    // Adjust the size according to the orientation
    switch(d->orientation())
    {
    case OR::ORIENTATION_ROT_90_HFLIP:
    case OR::ORIENTATION_ROT_90:
    case OR::ORIENTATION_ROT_90_VFLIP:
    case OR::ORIENTATION_ROT_270:
        size.transpose();
        break;

    default:
        break;
    }

    return size;
}

QString ExifWrapper::comment()
{
    if(d->mExivHandle.hasComments())
    {
        return d->mExivHandle.getCommentsDecoded();
    }

    return QStringLiteral("");
}

QImage ExifWrapper::thumbnail()
{
    QImage image = d->mExivHandle.getExifThumbnail(false);

    if(!image.isNull())
    {
        int64_t x1, y1, x2, y2;

        constexpr const char *canonThumbKey = "Exif.Canon.ThumbnailImageValidArea";

        if(d->mExivHandle.getExifTagInt64(canonThumbKey, x1, 0) &&
                d->mExivHandle.getExifTagInt64(canonThumbKey, x2, 1) &&
                d->mExivHandle.getExifTagInt64(canonThumbKey, y1, 2) &&
                d->mExivHandle.getExifTagInt64(canonThumbKey, y2, 3))
        {
            // ensure ThumbnailImageValidArea actually specifies a rectangle, i.e. there must be 4 coordinates
            QRect validArea(QPoint(x1, y1), QPoint(x2, y2));
            image = image.copy(validArea);
        }
        else
        {
            constexpr const char *sonyThumbKey = "Exif.Sony1.PreviewImageSize";

            if(d->mExivHandle.getExifTagInt64(sonyThumbKey, x1, 0) &&
                    d->mExivHandle.getExifTagInt64(sonyThumbKey, y1, 1))
            {
                // Unfortunately, Sony does not provide an exif tag that specifies the valid area of the
                // embedded thumbnail. Need to derive it from the size of the preview image instead.
                const int64_t prevHeight = x1;
                const int64_t prevWidth = y1;

                const double scale = prevWidth / image.width();

                // the embedded thumb only needs to be cropped vertically
                const int64_t validThumbAreaHeight = std::ceil(prevHeight / scale);
                const int64_t totalHeightOfBlackArea = image.height() - validThumbAreaHeight;
                // black bars on top and bottom should be equal in height
                const int64_t offsetFromTop = totalHeightOfBlackArea / 2;

                const QRect validArea(QPoint(0, offsetFromTop), QSize(image.width(), validThumbAreaHeight));
                image = image.copy(validArea);
            }
        }
    }

    return image;
}

std::optional<std::tuple<std::vector<AfPoint>, QSize>> ExifWrapper::autoFocusPoints()
{
    int64_t afValidPoints, imageWidth, imageHeight;

    if(d->mExivHandle.getExifTagInt64("Exif.Canon.AFValidPoints",      afValidPoints) &&
            d->mExivHandle.getExifTagInt64("Exif.Canon.AFCanonImageWidth",  imageWidth) &&
            d->mExivHandle.getExifTagInt64("Exif.Canon.AFCanonImageHeight", imageHeight))
    {
        QString model = d->mExivHandle.getExifTagString("Exif.Canon.ModelID");

        if(!model.isNull())
        {
            long flipY;

            if(model.indexOf("EOS") != -1)
            {
                flipY = -1;
            }
            else if(model.indexOf("PowerShot") != -1)
            {
                flipY = 1;
            }
            else
            {
                qInfo() << "Canon image contains AF point information, but camera model '" << model << "' is unknown.";
                return std::nullopt;
            }

            if(afValidPoints <= 0)
            {
                qInfo() << "A negative number of valid AF point is not allowed.";
                return std::nullopt;
            }

            std::vector<AfPoint> points;
            points.reserve(afValidPoints);

            for(long i = 0; i < afValidPoints; i++)
            {
                int64_t rectWidth, rectHeight, x, y;

                // should be unsigned because bitmasks
                int64_t foc, sel, dis;

                if(d->mExivHandle.getExifTagInt64("Exif.Canon.AFAreaWidths",    rectWidth,  i) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFAreaHeights",   rectHeight, i) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFXPositions",    x,          i) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFYPositions",    y,          i) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFPointsInFocus", foc,        i / 16) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFPointsSelected", sel,        i / 16) &&
                        d->mExivHandle.getExifTagInt64("Exif.Canon.AFPointsUnusable", dis,        i / 16))
                {
                    int64_t rectPosX = x + imageWidth / 2 - rectWidth / 2;
                    int64_t rectPosY = flipY * y + imageHeight / 2 - rectHeight / 2;

                    QRect rectAF(rectPosX, rectPosY, rectWidth, rectHeight);

                    AfType type;

                    if(dis & (1 << (i % 16)))
                    {
                        type = AfType::Disabled;
                    }
                    else
                    {
                        if(foc & (1 << (i % 16)))
                        {
                            type = AfType::HasFocus;
                        }
                        else if(sel & (1 << (i % 16)))
                        {
                            type = AfType::Selected;
                        }
                        else
                        {
                            type = AfType::Normal;
                        }
                    }

                    points.push_back(std::make_tuple(type, rectAF));
                }
                else
                {
                    qWarning() << "Error while parsing Canon AF";
                    return std::nullopt;
                }
            }

            return std::optional(std::tuple(points, QSize(imageWidth, imageHeight)));
        }
    }

    return std::nullopt;
}

bool ExifWrapper::autoFocusRotation(double& rotation)
{
    int64_t r;
    if(d->mExivHandle.getExifTagInt64("Exif.Canon.AFFineRotation", r))
    {
        // anti-clockwise rotation!
        rotation = r / 100.0; // centidegrees to degrees
        return true;
    }
    return false;
}

bool ExifWrapper::aperture(double &quot)
{
    long num, den;

    if(d->mExivHandle.getExifTagRational("Exif.Photo.FNumber", num, den))
    {
        quot = num * 1.0 / den;
        return true;
    }
    else
    {
        return false;
    }
}

QString ExifWrapper::aperture()
{
    double num;

    if(this->aperture(num))
    {
        return QString((Formatter() << std::setprecision(2) << num).str().c_str());
    }
    else
    {
        return QString();
    }
}

bool ExifWrapper::exposureTime(long &num, long &den)
{
    return d->mExivHandle.getExifTagRational("Exif.Photo.ExposureTime", num, den);
}

bool ExifWrapper::exposureTime(double &quot)
{
    long num, den;
    bool res = this->exposureTime(num, den);

    if(res)
    {
        quot = num * 1.0 / den;
    }

    return res;
}

QString ExifWrapper::exposureTime()
{
    long num, den;

    if(this->exposureTime(num, den) && den != 0)
    {
        double quot = num * 1.0 / den;

        if(quot < 1)
        {
            return QString((Formatter() << num << "/" << den << "s").str().c_str());
        }
        else
        {
            unsigned h = static_cast<unsigned>(quot / 60 / 60);
            unsigned m = static_cast<unsigned>(quot / 60);
            double sec = std::fmod(quot, 60);

            Formatter f;

            if(h)
            {
                f << h << "h ";
            }

            if(m)
            {
                f << m << "m ";
            }

            return QString((f << std::setprecision(3) << sec << "s").str().c_str());
        }
    }
    else
    {
        return QString();
    }
}

bool ExifWrapper::iso(int64_t &num)
{
    return d->mExivHandle.getExifTagInt64("Exif.Photo.ISOSpeedRatings", num);
}

QString ExifWrapper::iso()
{
    int64_t i;

    if(this->iso(i))
    {
        return QString::number(i);
    }

    return QString();
}

QString ExifWrapper::lens()
{
    return d->mExivHandle.getExifTagString("Exif.Photo.LensModel");
}

bool ExifWrapper::focalLength(double &quot)
{
    long num, den;

    if(d->mExivHandle.getExifTagRational("Exif.Photo.FocalLength", num, den))
    {
        quot = num * 1.0 / den;
        return true;
    }
    else
    {
        return false;
    }
}

QString ExifWrapper::focalLength()
{
    double foc;

    if(this->focalLength(foc))
    {
        return QString(QStringLiteral("%1 mm").arg(QString::number(foc)));
    }
    else
    {
        return QString();
    }
}

QDateTime ExifWrapper::dateRecorded()
{
    return d->mExivHandle.getImageDateTime();
}

QString ExifWrapper::darkFrameSubtraction()
{
    int64_t l;

    if(d->mExivHandle.getExifTagInt64("Exif.CanonCf.NoiseReduction", l) ||
            d->mExivHandle.getExifTagInt64("Exif.CanonFi.NoiseReduction", l)
      )
    {
        if(l == -1)
        {
            if(d->mExivHandle.getExifTagInt64("Exif.Canon.LightingOpt", l, 4) || // Exiv 0.27 and older
                d->mExivHandle.getExifTagInt64("Exif.CanonLiOp.LongExposureNoiseReduction", l))
            {
                // translate Canon LightingOpt Tags to old value
                switch(l)
                {
                case 0:
                    break;

                case 1:
                    l = 4;
                    break;

                case 2:
                    l = 3;
                    break;

                default:
                    return QStringLiteral("unknown LightingOpt val ") + QString::number(l);
                }
            }
        }

        switch(l)
        {
        case 0:
            return QStringLiteral("Off");

        case 1:
        case 3:
            return QStringLiteral("On");

        case 4:
            return QStringLiteral("Auto");

        default:
            return QStringLiteral("unknown value ") + QString::number(l);
        }
    }

    return QString();
}

QPointF ExifWrapper::gpsLocation()
{
    QString gpsLatRef = d->mExivHandle.getExifTagString("Exif.GPSInfo.GPSLatitudeRef");

    QString gpsLat = d->mExivHandle.getExifTagString("Exif.GPSInfo.GPSLatitude");

    QString gpsLonRef = d->mExivHandle.getExifTagString("Exif.GPSInfo.GPSLongitudeRef");

    QString gpsLon = d->mExivHandle.getExifTagString("Exif.GPSInfo.GPSLongitude");

    return d->convertGPSToDecimal(gpsLatRef, gpsLat, gpsLonRef, gpsLon);
}

bool ExifWrapper::gpsAltitude(double& alt)
{
    long num, den;

    if(d->mExivHandle.getExifTagRational("Exif.GPSInfo.GPSAltitude", num, den))
    {
        alt = 1.0 * num / den;

        int64_t l;
        if(d->mExivHandle.getExifTagInt64("Exif.GPSInfo.GPSAltitudeRef", l))
        {
            alt *= (l == 0 ? 1 : -1);
        }
        return true;
    }

    return false;
}

bool ExifWrapper::gpsDop(double &dop)
{
    long num, den;

    if(d->mExivHandle.getExifTagRational("Exif.GPSInfo.GPSDOP", num, den))
    {
        dop = 1.0 * num / den;
        return true;
    }

    return false;
}

bool ExifWrapper::gpsHPosErr(double &dop)
{
    long num, den;

    if(d->mExivHandle.getExifTagRational("Exif.GPSInfo.GPSHPositioningError", num, den))
    {
        dop = 1.0 * num / den;
        return true;
    }

    return false;
}

bool ExifWrapper::isMirrorLockupEnabled(bool &isEnabled)
{
    int64_t l;

    if(d->mExivHandle.getExifTagInt64("Exif.CanonCf.MirrorLockup", l))
    {
        isEnabled = l != 0;
        return true;
    }

    return false;
}

QString ExifWrapper::formatToString()
{
    Formatter f;

    int64_t n;
    double r;
    bool b;
    QString s;
    QPointF p;

    if(this->aperture(r))
    {
        f << "Aperture: " << std::fixed << std::setprecision(1) << r << "<br>";
    }

    s = this->exposureTime();

    if(!s.isEmpty())
    {
        f << "Exposure: " << s.toStdString() << "<br>";
    }

    if(this->iso(n))
    {
        f << "ISO: " << n << "<br>";
    }

    s = this->darkFrameSubtraction();

    if(!s.isEmpty())
    {
        f << "Long Noise Reduction: " << s.toStdString() << "<br>";
    }

    if(this->isMirrorLockupEnabled(b))
    {
        s = d->boolToString(b);
        f << "Mirror Lockup: " << s.toStdString() << "<br>";
    }

    s = this->lens();

    if(!s.isEmpty())
    {
        f << "Lens: " << s.toStdString() << "<br>";
    }

    if(this->focalLength(r))
    {
        f << "Focal Length: " << std::fixed << std::setprecision(0) << r << "<br>";
    }
    
    p = this->gpsLocation();
    if(!p.isNull())
    {
        f << "<br>GPS: <a href=\"https://www.google.de/maps/place/" << std::setprecision(std::numeric_limits<double>::max_digits10) << p.x() << "," << p.y() << "\">" << std::setprecision(3) << p.x() << ", " << p.y() << "</a><br>";
    }
    
    if(this->gpsAltitude(r))
    {
        f << "Altitude: " << std::fixed << std::setprecision(0) << r << "m<br>";
    }
    
    if(this->gpsDop(r))
    {
        f << "GPS DOP: " << std::fixed << std::setprecision(1) << r << "<br>";
    }
    
    if(this->gpsHPosErr(r))
    {
        f << "GPS HorizPosErr: " << std::fixed << std::setprecision(1) << r << "m<br>";
    }

    QDateTime dt = this->dateRecorded();

    if(dt.isValid())
    {
        int phase = MoonPhase::fromDateTime(dt);
        f << "<br>Originally recorded on:<br>"
          << dt.toString("yyyy-MM-dd (dddd)<br>").toStdString()
          << dt.toString("hh:mm:ss<br>").toStdString()
          << MoonPhase::formatToString(phase).toStdString() << " (" << MoonPhase::calculateBrightness(phase) << "%)";
    }

    return QString(f.str().c_str());
}
