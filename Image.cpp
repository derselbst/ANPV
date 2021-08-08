
#include "Image.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"

#include <QtDebug>
#include <QPromise>
#include <QMetaMethod>
#include <QThreadPool>
#include <QFileIconProvider>
#include <chrono>
#include <atomic>
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
