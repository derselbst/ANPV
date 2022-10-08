
#include "Image.hpp"

#include "Formatter.hpp"
#include "xThreadGuard.hpp"
#include "ExifWrapper.hpp"
#include "TraceTimer.hpp"
#include "ANPV.hpp"

#include <QDir>
#include <QIcon>
#include <QAbstractFileIconProvider>
#include <QMetaMethod>
#include <QTimer>
#include <KDCRAW/KDcraw>
#include <mutex>

struct Image::Impl
{
    mutable std::recursive_mutex m;

    DecodingState state{ DecodingState::Unknown };
    
    // file path to the decoded input file
    const QFileInfo fileInfo;
    
    // a low resolution preview image of the original full image
    QImage thumbnail;
    
    // same as thumbnail, but rotated according to EXIF orientation
    QPixmap thumbnailTransformed;
    
    QIcon icon;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage decodedImage;
    
    // size of the fully decoded image, already available in DecodingState::Metadata
    QSize size;
    
    QSharedPointer<ExifWrapper> exifWrapper;
    
    QTransform userTransform;
    
    QColorSpace colorSpace;
    
    QString errorMessage;
    
    std::optional<std::tuple<std::vector<AfPoint>, QSize>> cachedAfPoints;
    
    QPointer<QTimer> updateRectTimer;
    QRect cachedUpdateRect;

    // indicates whether this instance has been marked by the user
    Qt::CheckState checked = Qt::Unchecked;
    
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
    d->updateRectTimer = new QTimer(this);
    d->updateRectTimer->setInterval(100);
    d->updateRectTimer->setSingleShot(true);
    d->updateRectTimer->setTimerType(Qt::CoarseTimer);
    connect(d->updateRectTimer.data(), &QTimer::timeout, this,
        [&]()
        {
            std::unique_lock<std::recursive_mutex> lck(d->m);
            QRect updateRect = d->cachedUpdateRect;
            Q_ASSERT(updateRect.isValid());
            d->cachedUpdateRect = QRect();
            lck.unlock();
            emit this->previewImageUpdated(this, updateRect);
        });
}

Image::~Image()
{
    xThreadGuard g(this);
}

bool Image::hasDecoder() const
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->state != DecodingState::Unknown;
}

const QFileInfo& Image::fileInfo() const
{
    // no lock, it's const
    return d->fileInfo;
}

QSize Image::size() const
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->size;
}

void Image::setSize(QSize size)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->size = size;
}

QRect Image::fullResolutionRect() const
{
    return QRect(QPoint(0,0), this->size());
}

QTransform Image::userTransform() const
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->userTransform;
}

// this is what the user wants it to look like in the UI
void Image::setUserTransform(QTransform trans)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->userTransform = trans;
}

QImage Image::thumbnail()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
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
            emit this->thumbnailChanged(this, d->thumbnail);
        }
    }
}

QIcon Image::icon()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->icon;
}

void Image::setIcon(QIcon ico)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->icon = ico;
}

// must be called from UI thread
void Image::lookupIconFromFileType()
{
    xThreadGuard g(this);
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
    
    TraceTimer t(typeid(Image), 10);
    std::unique_lock<std::recursive_mutex> lck(d->m);
    
    QPixmap pix;
    QPixmap thumb = QPixmap::fromImage(this->thumbnail(), Qt::NoFormatConversion);
    if(thumb.isNull())
    {
        pix = this->icon().pixmap(height);
        if(pix.isNull())
        {
            t.setInfo("no icon found, drawing our own...");
            auto state = this->decodingState();
            if(this->hasDecoder() && state != DecodingState::Error && state != DecodingState::Fatal)
            {
                pix = ANPV::globalInstance()->noPreviewPixmap();
            }
            else
            {
                pix = ANPV::globalInstance()->noIconPixmap();
            }
        }
        else
        {
            t.setInfo("using icon from QFileIconProvider");
        }
        Q_ASSERT(!pix.isNull());
    }
    else
    {
        QSize currentSize = d->thumbnailTransformed.size();
        int currentHeight = d->thumbnailTransformed.height();
        if(!d->thumbnailTransformed.isNull() && currentHeight >= height)
        {
            t.setInfo("using cached thumbnail, size is sufficient");
            return currentHeight == height ? d->thumbnailTransformed : d->thumbnailTransformed.scaledToHeight(height, Qt::FastTransformation);
        }
        
        int currentWidth = thumb.width();
        currentHeight = thumb.height();
        t.setInfo(Formatter() << "no matching thumbnail cached, transforming and scaling a thumbnail with an original size of " << currentWidth << "x" << currentHeight << "px to height " << height << "px");
        pix = thumb.transformed(d->transformMatrixOrIdentity());
    }
    pix = pix.scaledToHeight(height, Qt::FastTransformation);
    d->thumbnailTransformed = pix;
    return pix;
}

QSharedPointer<ExifWrapper> Image::exif()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->exifWrapper;
}

void Image::setExif(QSharedPointer<ExifWrapper> e)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->exifWrapper = e;
    d->cachedAfPoints = std::nullopt;
}

QColorSpace Image::colorSpace()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->colorSpace;
}

QString Image::namedColorSpace()
{
    QColorSpace cs = this->colorSpace();
    return cs.description();
}

void Image::setColorSpace(QColorSpace cs)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->colorSpace = cs;
}

std::optional<std::tuple<std::vector<AfPoint>, QSize>> Image::cachedAutoFocusPoints()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    if(d->cachedAfPoints)
    {
        return d->cachedAfPoints;
    }
    
    QSharedPointer<ExifWrapper> exif = this->exif();
    if(!exif)
    {
        return std::nullopt;
    }
    
    // unlock while doing potentially expensive processing
    lck.unlock();
    auto temp = exif->autoFocusPoints();
    lck.lock();
    d->cachedAfPoints = std::move(temp);
    return d->cachedAfPoints;
}

QString Image::formatInfoString()
{
    Formatter f;
    
    QString infoStr;
    if(this->isRaw())
    {
        infoStr += "<b>This is a RAW file!</b><br>"
                   "What you see is an<br>"
                   "embedded preview, which<br>"
                   "might be of lower quality<br>"
                   "than the RAW itself!<br><br>";
    }
    
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
        
        infoStr += QString("<b>===stat()===</b><br><br>");
        infoStr += "File Size: ";
        infoStr += ANPV::formatByteHtmlString(this->fileInfo().size());
        infoStr += "<br><br>";
        
        QDateTime t = this->fileInfo().fileTime(QFileDevice::FileBirthTime);
        if(t.isValid())
        {
            infoStr += "File created on:<br>";
            infoStr += t.toString("  yyyy-MM-dd (dddd)<br>");
            infoStr += t.toString("  hh:mm:ss<br><br>");
        }
        
        t = this->fileInfo().fileTime(QFileDevice::FileModificationTime);
        if(t.isValid())
        {
            infoStr += "File modified on:<br>";
            infoStr += t.toString("yyyy-MM-dd (dddd)<br>");
            infoStr += t.toString("hh:mm:ss");
        }
    }
    
    return infoStr;
}

QString Image::fileExtension() const
{
    return this->fileInfo().fileName().section(QLatin1Char('.'), -1).toLower();
}

// whether the Image looks like a RAW from its file extension
bool Image::isRaw() const
{
    const QString formatHint = this->fileExtension();
    bool isRaw = KDcrawIface::KDcraw::rawFilesList().contains(formatHint);
    return isRaw;
}

bool Image::hasEquallyNamedJpeg() const
{
    static const QLatin1String JPG("JPG");
    
    QString suffix = this->fileInfo().suffix().toUpper();
    return suffix != JPG && d->hasEquallyNamedFile(JPG);
}

bool Image::hasEquallyNamedTiff() const
{
    static const QLatin1String TIF("TIF");
    
    QString suffix = this->fileInfo().suffix().toUpper();
    return suffix != TIF && d->hasEquallyNamedFile(TIF);
}

DecodingState Image::decodingState() const
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->state;
}

void Image::setDecodingState(DecodingState state)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    DecodingState old = d->state;
    
    if(old == DecodingState::Fatal && (state == DecodingState::Error || state == DecodingState::Cancelled))
    {
        // we are already Fatal, ignore new error states
        return;
    }
    
    if(old != state)
    {
        d->state = state;
        lck.unlock();
        emit this->decodingStateChanged(this, state, old);
    }
}

QString Image::errorMessage()
{
    xThreadGuard g(this);
    return d->errorMessage;
}

void Image::setErrorMessage(const QString& err)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    if(d->errorMessage != err)
    {
        lck.unlock();
        d->errorMessage = err;
    }
}

Qt::CheckState Image::checked()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->checked;
}

void Image::setChecked(Qt::CheckState b)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    auto old = d->checked;
    if (old != b)
    {
        d->checked = b;
        lck.unlock();
        emit this->checkStateChanged(this, b, old);
    }
}

QImage Image::decodedImage()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->decodedImage;
}

void Image::setDecodedImage(QImage img, QTransform scale)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    // skip comparison with current image, can be slow
    d->decodedImage = img;
    lck.unlock();
    emit this->decodedImageChanged(this, d->decodedImage, scale);
}

void Image::updatePreviewImage(const QRect& r)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    QRect updateRect = d->cachedUpdateRect;
    updateRect = updateRect.isValid() ? updateRect.united(r) : r;
    d->cachedUpdateRect = updateRect;
    Q_ASSERT(updateRect.isValid());
    lck.unlock();

    QMetaObject::invokeMethod(this, [&]()
        {
            if (!d->updateRectTimer->isActive())
            {
                d->updateRectTimer->start();
            }
        }, Qt::QueuedConnection);
}

void Image::connectNotify(const QMetaMethod& signal)
{
    if (signal.name() == QStringLiteral("decodingStateChanged"))
    {
        DecodingState cur = this->decodingState();
        emit this->decodingStateChanged(this, cur, cur);
    }
    else if(signal.name() == QStringLiteral("thumbnailChanged"))
    {
        QImage thumb = this->thumbnail();
        if(!thumb.isNull())
        {
            emit this->thumbnailChanged(this, thumb);
        }
    }
    else if(signal.name() == QStringLiteral("decodedImageChanged"))
    {
        QImage img = this->decodedImage();
        if(!img.isNull())
        {
            emit this->decodedImageChanged(this, img, QTransform());
        }
    }
    else if (signal.name() == QStringLiteral("checkStateChanged"))
    {
        Qt::CheckState s = this->checked();
        emit this->checkStateChanged(this, s, s);
    }

    QObject::connectNotify(signal);
}
