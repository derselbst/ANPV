
#include "Image.hpp"

#include "Formatter.hpp"
#include "xThreadGuard.hpp"
#include "ExifWrapper.hpp"

#include <QDir>
#include <QtDebug>
#include <QFileIconProvider>
#include <KDCRAW/KDcraw>
#include <mutex>

struct Image::Impl
{
    std::recursive_mutex m;

    // file path to the decoded input file
    const QFileInfo fileInfo;
    
    // a low resolution preview image of the original full image
    QPixmap thumbnail;
    
    // same as thumbnail, but rotated according to EXIF orientation
    QPixmap thumbnailTransformed;
    
    // size of the fully decoded image, already available in DecodingState::Metadata
    QSize size;
    
    QSharedPointer<ExifWrapper> exifWrapper;
    
    QTransform defaultTransform;
    QTransform userTransform;
    
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
};

Image::Image(const QFileInfo& url) : d(std::make_unique<Impl>(url))
{
}

Image::~Image()
{
    xThreadGuard g(this);
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
    
QTransform Image::defaultTransform() const
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    return d->defaultTransform;
}

// this could be fed by exif()->transformMatrix()
void Image::setDefaultTransform(QTransform trans)
{
    std::lock_guard<std::recursive_mutex> lck(d->m);
    d->defaultTransform = trans;
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

QPixmap Image::thumbnail()
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
    
    QPixmap pix = QPixmap::fromImage(thumb, Qt::NoFormatConversion);
    this->setThumbnail(pix);
}

void Image::setThumbnail(QPixmap pix)
{
    if(pix.isNull())
    {
        // thumbnails should not be unset
        return;
    }
    
    std::unique_lock<std::recursive_mutex> lck(d->m);
    if(pix.width() > d->thumbnail.width())
    {
        d->thumbnail = pix;
        d->thumbnailTransformed = QPixmap();
        
        if(!this->signalsBlocked())
        {
            lck.unlock();
            emit this->thumbnailChanged();
        }
    }
}

QPixmap Image::icon(int height)
{
    std::unique_lock<std::recursive_mutex> lck(d->m, std::defer_lock);
    
    lck.lock();
    QPixmap thumb = this->thumbnail();
    if(thumb.isNull())
    {
        lck.unlock();
        
        QFileIconProvider prov;
        QIcon ico = prov.icon(this->fileInfo());
        QPixmap pix = ico.pixmap(height, height);
        pix = pix.scaledToHeight(height);
        
        lck.lock();
        d->thumbnailTransformed = pix;
    }
    else if(d->thumbnailTransformed.isNull() || d->thumbnailTransformed.height() != height)
    {
        QTransform trans = this->defaultTransform();
        lck.unlock();
        
        QPixmap transedThumb = thumb.transformed(trans);
        transedThumb = transedThumb.scaledToHeight(height);
        
        lck.lock();
        d->thumbnailTransformed = transedThumb;
    }
    
    return d->thumbnailTransformed;
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


QString Image::formatInfoString()
{
    Formatter f;
    
    long n, d;
    double r;
    QString s, infoStr;
    
    QSize size = this->size();
    if(size.isValid())
    {
        f << "Resolution: " << size.width() << " x " << size.height() << " px<br><br>";
    }
    
    infoStr += f.str().c_str();
    
    s = this->exif()->formatToString();
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
    return d->hasEquallyNamedFile("JPEG") || d->hasEquallyNamedFile("JPG");
}

bool Image::hasEquallyNamedTiff()
{
    return d->hasEquallyNamedFile("TIFF") || d->hasEquallyNamedFile("TIF");
}
