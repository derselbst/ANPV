
#include "ExifWrapper.hpp"

#include "AfPointOverlay.hpp"
#include "Formatter.hpp"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QPoint>
#include <QRect>
#include <cmath>
#include <QDebug>
#include <QPainter>
#include <QPen>
#include <iomanip>

using OR = KExiv2Iface::KExiv2::ImageOrientation;

struct ExifWrapper::Impl
{
    KExiv2Iface::KExiv2 mExivHandle;
        
    int dotsPerMeter(const QString& keyName)
    {
        QString keyVal = QStringLiteral("Exif.Image.") + keyName;
        long res, val;
        if(!mExivHandle.getExifTagLong("Exif.Image.ResolutionUnit", res) ||
           !mExivHandle.getExifTagLong(keyVal.toLocal8Bit().data(), val))
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
        switch (res)
        {
        case 3:  // dots per cm
            return int(val * 100);
        default:  // dots per inch
            return int(val * INCHESPERMETER);
        }

        return 0;
    }
    
    QTransform transformMatrix(OR orientation)
    {
        QTransform matrix;
        switch (orientation) {
        case OR::ORIENTATION_UNSPECIFIED:
        case OR::ORIENTATION_NORMAL:
            break;

        case OR::ORIENTATION_HFLIP:
            matrix.scale(-1, 1);
            break;

        case OR::ORIENTATION_ROT_180:
            matrix.rotate(180);
            break;

        case OR::ORIENTATION_VFLIP:
            matrix.scale(1, -1);
            break;

        case OR::ORIENTATION_ROT_90_HFLIP:
            matrix.scale(-1, 1);
            matrix.rotate(90);
            break;

        case OR::ORIENTATION_ROT_90:
            matrix.rotate(90);
            break;

        case OR::ORIENTATION_ROT_90_VFLIP:
            matrix.scale(1, -1);
            matrix.rotate(90);
            break;

        case OR::ORIENTATION_ROT_270:
            matrix.rotate(270);
            break;
        }

        return matrix;
    }
};

ExifWrapper::ExifWrapper()
: d(std::make_unique<Impl>())
{
}

ExifWrapper::~ExifWrapper() = default;


// NOTE: Exiv2 takes ownership of data, so the caller must keep a reference to it to avoid use-after-free!
bool ExifWrapper::loadFromData(const QByteArray& data)
{
    return d->mExivHandle.loadFromData(data);
}

QString ExifWrapper::errorMessage()
{
    return d->mExivHandle.getErrorMessage();
}

KExiv2Iface::KExiv2::ImageOrientation ExifWrapper::orientation()
{
    return d->mExivHandle.getImageOrientation();
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
    QSize size = d->mExivHandle.getImageDimensions();

    // Adjust the size according to the orientation
    switch (orientation())
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
    
    if (!image.isNull())
    {
        long x1,y1,x2,y2;
        
        constexpr const char* canonThumbKey = "Exif.Canon.ThumbnailImageValidArea";
        if(d->mExivHandle.getExifTagLong(canonThumbKey, x1, 0) &&
           d->mExivHandle.getExifTagLong(canonThumbKey, x2, 1) &&
           d->mExivHandle.getExifTagLong(canonThumbKey, y1, 2) &&
           d->mExivHandle.getExifTagLong(canonThumbKey, y2, 3))
        {
            // ensure ThumbnailImageValidArea actually specifies a rectangle, i.e. there must be 4 coordinates
            QRect validArea(QPoint(x1, y1), QPoint(x2, y2));
            image = image.copy(validArea);
        }
        else
        {
            constexpr const char* sonyThumbKey = "Exif.Sony1.PreviewImageSize";
            if(d->mExivHandle.getExifTagLong(sonyThumbKey, x1, 0) &&
               d->mExivHandle.getExifTagLong(sonyThumbKey, y1, 1))
            {
                // Unfortunately, Sony does not provide an exif tag that specifies the valid area of the 
                // embedded thumbnail. Need to derive it from the size of the preview image instead.
                const long prevHeight = x1;
                const long prevWidth = y1;

                const double scale = prevWidth / image.width();

                // the embedded thumb only needs to be cropped vertically
                const long validThumbAreaHeight = std::ceil(prevHeight / scale);
                const long totalHeightOfBlackArea = image.height() - validThumbAreaHeight;
                // black bars on top and bottom should be equal in height
                const long offsetFromTop = totalHeightOfBlackArea / 2;

                const QRect validArea(QPoint(0, offsetFromTop), QSize(image.width(), validThumbAreaHeight));
                image = image.copy(validArea);
            }
        }
        
        auto o = orientation();
        if (o != OR::ORIENTATION_NORMAL && o != OR::ORIENTATION_UNSPECIFIED)
        {
            image = image.transformed(d->transformMatrix(o));
        }
    }
    return image;
}

std::unique_ptr<AfPointOverlay> ExifWrapper::autoFocusPoints()
{
    long afValidPoints, imageWidth, imageHeight;
    if (d->mExivHandle.getExifTagLong("Exif.Canon.AFValidPoints",      afValidPoints) &&
        d->mExivHandle.getExifTagLong("Exif.Canon.AFCanonImageWidth",  imageWidth) &&
        d->mExivHandle.getExifTagLong("Exif.Canon.AFCanonImageHeight", imageHeight))
    {
        QString model = d->mExivHandle.getExifTagString("Exif.Canon.ModelID");
        if (!model.isNull())
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
                qInfo() << "Canon image contains AF point information, but camera model is unknown.";
                return nullptr;
            }
            
            auto apo = std::make_unique<AfPointOverlay>(afValidPoints, QSize(imageWidth, imageHeight));
            
            for(long i=0; i<afValidPoints; i++)
            {
                long rectWidth, rectHeight, x, y;
                
                // should be unsigned because bitmasks
                long foc, sel, dis;
                
                if (d->mExivHandle.getExifTagLong("Exif.Canon.AFAreaWidths",    rectWidth,  i) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFAreaHeights",   rectHeight, i) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFXPositions",    x,          i) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFYPositions",    y,          i) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFPointsInFocus", foc,        i/16) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFPointsSelected",sel,        i/16) &&
                    d->mExivHandle.getExifTagLong("Exif.Canon.AFPointsUnusable",dis,       i/16))
                {
                    long rectPosX = x + imageWidth/2 - rectWidth/2;
                    long rectPosY = flipY * y + imageHeight/2 - rectHeight/2;
                    
                    QRect rectAF(rectPosX, rectPosY, rectWidth, rectHeight);
                    
                    AfPointOverlay::AfType type;
                    if(dis & (1<<(i%16)))
                    {
                        type = AfPointOverlay::AfType::Disabled;
                    }
                    else
                    {
                        if(foc & (1<<(i%16)))
                        {
                            type = AfPointOverlay::AfType::HasFocus;
                        }
                        else if(sel & (1<<(i%16)))
                        {
                            type = AfPointOverlay::AfType::Selected;
                        }
                        else
                        {
                            type = AfPointOverlay::AfType::Normal;
                        }
                    }
                    
                    apo->addAfArea(rectAF, type);
                }
                else
                {
                    qWarning() << "Error while parsing Canon AF";
                    return nullptr;
                }
            }
            
            return apo;
        }
    }
    
    return nullptr;
}

bool ExifWrapper::aperture(double& quot)
{
    long num,den;
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
        return QString ((Formatter() << std::setprecision(2) << num).str().c_str());
    }
    else
    {
        return QString();
    }
}

bool ExifWrapper::exposureTime(long& num, long& den)
{
    return d->mExivHandle.getExifTagRational("Exif.Photo.ExposureTime", num, den);
}

bool ExifWrapper::exposureTime(double& quot)
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
    if(this->exposureTime(num, den))
    {
        double quot = num * 1.0 / den;
        if(quot < 1)
        {
            return QString ((Formatter() << num << "/" << den).str().c_str());
        }
        else
        {
            return QString ((Formatter() << std::setprecision(3) << quot).str().c_str());
        }
    }
    else
    {
        return QString();
    }
}

bool ExifWrapper::iso(long& num)
{
    return d->mExivHandle.getExifTagLong("Exif.Photo.ISOSpeedRatings", num);
}

QString ExifWrapper::lens()
{
    return d->mExivHandle.getExifTagString("Exif.Photo.LensModel");
}

bool ExifWrapper::focalLength(double& quot)
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

QDateTime ExifWrapper::dateRecorded()
{
    return d->mExivHandle.getImageDateTime();
}

QString ExifWrapper::formatToString()
{
    Formatter f;
    
    long n, d;
    double r;
    QString s;
    
    QSize size = this->size();
    if(size.isValid())
    {
        f << "Resolution: " << size.width() << " x " << size.height() << " px\n\n";
    }
    
    if(this->aperture(r))
    {
        f << "Aperture: " << std::fixed << std::setprecision(1) << r << "\n";
    }
    
    s = this->exposureTime();
    if(!s.isEmpty())
    {
        f << "Exposure: " << s.toStdString() << "\n";
    }
    
    if(this->iso(n))
    {
        f << "ISO: " << n << "\n";
    }
    
    s = this->lens();
    if(!s.isEmpty())
    {
        f << "Lens: " << s.toStdString() << "\n";
    }
    
    if(this->focalLength(r))
    {
        f << "Focal Length: " << std::fixed << std::setprecision(0) << r << "\n";
    }
    
    QDateTime dt = this->dateRecorded();
    if(dt.isValid())
    {
        f << "\nRecorded on:\n"
          << dt.toString("yyyy-MM-dd (dddd)\n").toStdString()
          << dt.toString("hh:mm:ss").toStdString()
          << "\n";
    }
    
    return QString(f.str().c_str());
}
