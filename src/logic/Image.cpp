
#include "Image.hpp"

#include "Formatter.hpp"
#include "xThreadGuard.hpp"
#include "ExifWrapper.hpp"
#include "TraceTimer.hpp"
#include "ANPV.hpp"

#include <QDir>
#include <QIcon>
#include <QtDebug>
#include <QSvgRenderer>
#include <QAbstractFileIconProvider>
#include <QPainter>
#include <KDCRAW/KDcraw>
#include <mutex>

struct Image::Impl
{
    mutable std::recursive_mutex m;

    // set to true, once any SmartImageDecoder has been created for this Image, otherwise stays false
    bool hasDecoder = false;
    
    // file path to the decoded input file
    const QFileInfo fileInfo;
    
    // a low resolution preview image of the original full image
    QImage thumbnail;
    
    // same as thumbnail, but rotated according to EXIF orientation
    QPixmap thumbnailTransformed;
    
    QIcon icon;
    
    // size of the fully decoded image, already available in DecodingState::Metadata
    QSize size;
    
    QSharedPointer<ExifWrapper> exifWrapper;
    
    QTransform userTransform;
    
    QColorSpace colorSpace;
    
    Impl(const QFileInfo& url) : fileInfo(url)
    {}
    
    bool hasEquallyNamedFile(QString wantedSuffix)
    {
        QString basename = this->fileInfo.completeBaseName();
        QString path = this->fileInfo.canonicalPath();
        if(path.isEmpty())
        {
            return false;
        }
        
        QFileInfo wantedFile(QDir(path).filePath(basename + QLatin1Char('.') + wantedSuffix.toLower()));
        bool lowerExists = wantedFile.exists();
        
        wantedFile = QFileInfo(QDir(path).filePath(basename + QLatin1Char('.') + wantedSuffix));
        bool upperExists = wantedFile.exists();
        return lowerExists || upperExists;
    }

    QTransform transformMatrixOrIdentity()
    {
        if(this->exifWrapper)
        {
            return this->exifWrapper->transformMatrix();
        }
        else
        {
            return QTransform();
        }
    }
};

Image::Image(const QFileInfo& url) : d(std::make_unique<Impl>(url))
{
}

Image::~Image()
{
    xThreadGuard g(this);
}

bool Image::hasDecoder() const
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->hasDecoder;
}

void Image::setHasDecoder(bool b)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->hasDecoder = b;
}

const QFileInfo& Image::fileInfo() const
{
    // no lock, it's const
    return d->fileInfo;
}

QSize Image::size() const
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->size;
}

void Image::setSize(QSize size)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->size = size;
}

QTransform Image::userTransform() const
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->userTransform;
}

// this is what the user wants it to look like in the UI
void Image::setUserTransform(QTransform trans)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->userTransform = trans;
}

QImage Image::thumbnail()
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->thumbnail;
}

void Image::setThumbnail(QImage thumb)
{
    if(thumb.isNull())
    {
        return;
    }
    
    std::unique_lock<std::recursive_mutex> lck(d->m);
    if(thumb.width() > d->thumbnail.width())
    {
        d->thumbnail = thumb;
        d->thumbnailTransformed = QPixmap();
        
        if(!this->signalsBlocked())
        {
            lck.unlock();
            emit this->thumbnailChanged();
        }
    }
}

QIcon Image::icon()
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->icon;
}

void Image::setIcon(QIcon ico)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->icon = ico;
}

void Image::lookupIconFromFileType()
{
    QAbstractFileIconProvider* prov = ANPV::globalInstance()->iconProvider();
    // this operation is expensive, up to 30ms per call!
    QIcon ico = prov->icon(this->fileInfo());
    this->setIcon(ico);
}

QPixmap Image::thumbnailTransformed(int height)
{
    if(height <= 0)
    {
        return QPixmap();
    }
    
    TraceTimer t(typeid(Image), 50);
    std::lock_guard<std::recursive_mutex> lck(d->m);
    
    QPixmap pix;
    QPixmap thumb = QPixmap::fromImage(this->thumbnail(), Qt::NoFormatConversion);
    if(thumb.isNull())
    {
        pix = this->icon().pixmap(height);
        if(pix.isNull())
        {
            t.setInfo("no icon found, drawing our own...");
            pix = ANPV::globalInstance()->noIconPixmap();
        }
        else
        {
            t.setInfo("using icon from QFileIconProvider");
        }
        Q_ASSERT(!pix.isNull());
    }
    else
    {
        int currentHeight = d->thumbnailTransformed.height();
        if(!d->thumbnailTransformed.isNull() && currentHeight >= height)
        {
            t.setInfo("using cached thumbnail, size is sufficient");
            return currentHeight == height ? d->thumbnailTransformed : d->thumbnailTransformed.scaledToHeight(height, Qt::FastTransformation);
        }
        
        t.setInfo("no matching thumbnail cached, transforming and scaling it");
        pix = thumb.transformed(d->transformMatrixOrIdentity());
    }
    pix = pix.scaledToHeight(height, Qt::FastTransformation);
    d->thumbnailTransformed = pix;
    return pix;
}

QSharedPointer<ExifWrapper> Image::exif()
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->exifWrapper;
}

void Image::setExif(QSharedPointer<ExifWrapper> e)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->exifWrapper = e;
}

QColorSpace Image::colorSpace()
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->colorSpace;
}

QString Image::namedColorSpace()
{
    QColorSpace cs = this->colorSpace();
    return cs.description();
}

void Image::setColorSpace(QColorSpace cs)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->colorSpace = cs;
}


QString Image::formatInfoString()
{
    Formatter f;
    
    QString infoStr;
    auto exifWrapper = this->exif();
    if(exifWrapper)
    {
        QString s;
        
        QSize size = this->size();
        if(size.isValid())
        {
            f << "Resolution: " << size.width() << " x " << size.height() << " px<br>";
        }
        
        infoStr += f.str().c_str();
        
        s = this->namedColorSpace();
        if(s.isEmpty())
        {
            s = "unknown";
        }
        infoStr += QString("ColorSpace: " + s + "<br><br>");
        
        s = exifWrapper->formatToString();
        if(!s.isEmpty())
        {
            infoStr += QString("<b>===EXIF===</b><br><br>") + s + "<br><br>";
        }
        
        static const char *const sizeUnit[] = {" Bytes", " KiB", " MiB", " <b>GiB</b>"};
        float fsize = this->fileInfo().size();
        int i;
        for(i = 0; i<4 && fsize > 1024; i++)
        {
            fsize /= 1024.f;
        }
        
        infoStr += QString("<b>===stat()===</b><br><br>");
        infoStr += "File Size: ";
        infoStr += QString::number(fsize, 'f', 2) + sizeUnit[i];
        infoStr += "<br><br>";
        
        QDateTime t = this->fileInfo().fileTime(QFileDevice::FileBirthTime);
        if(t.isValid())
        {
            infoStr += "Created on:<br>";
            infoStr += t.toString("  yyyy-MM-dd (dddd)<br>");
            infoStr += t.toString("  hh:mm:ss<br><br>");
        }
        
        t = this->fileInfo().fileTime(QFileDevice::FileModificationTime);
        if(t.isValid())
        {
            infoStr += "Modified on:<br>";
            infoStr += t.toString("yyyy-MM-dd (dddd)<br>");
            infoStr += t.toString("hh:mm:ss");
        }
    }
    
    return infoStr;
}

// whether the Image looks like a RAW from its file extension
bool Image::isRaw()
{
    const QByteArray formatHint = this->fileInfo().fileName().section(QLatin1Char('.'), -1).toLocal8Bit().toLower();
    bool isRaw = KDcrawIface::KDcraw::rawFilesList().contains(QString::fromLatin1(formatHint));
    return isRaw;
}

bool Image::hasEquallyNamedJpeg()
{
    static const QLatin1String JPG("JPG");
    
    QString suffix = this->fileInfo().suffix().toUpper();
    return suffix != JPG && d->hasEquallyNamedFile(JPG);
}

bool Image::hasEquallyNamedTiff()
{
    static const QLatin1String TIF("TIF");
    
    QString suffix = this->fileInfo().suffix().toUpper();
    return suffix != TIF && d->hasEquallyNamedFile(TIF);
}
