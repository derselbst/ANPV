
#include "Image.hpp"

#include "Formatter.hpp"
#include "xThreadGuard.hpp"
#include "ExifWrapper.hpp"
#include "TraceTimer.hpp"
#include "ANPV.hpp"
#include "SmartImageDecoder.hpp"
#include "LibRawHelper.hpp"

#include <QPointer>
#include <QDir>
#include <QIcon>
#include <QAbstractFileIconProvider>
#include <QMetaMethod>
#include <QTimer>
#include <mutex>

struct Image::Impl
{
    mutable std::recursive_mutex m;

    DecodingState state{ DecodingState::Unknown };

    // Quick reference to the decoder, possibly an owning reference as well
    QSharedPointer<SmartImageDecoder> decoder;

    QWeakPointer<Image> neighbor;

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

    std::unordered_map<std::string, std::string> additionalMetadata;

    QString errorMessage;

    std::optional<std::tuple<std::vector<AfPoint>, QSize>> cachedAfPoints;

    QPointer<QTimer> updateRectTimer;
    QRect cachedUpdateRect;

    // indicates whether this instance has been marked by the user
    Qt::CheckState checked = Qt::Unchecked;

    Impl(const QFileInfo &url) : fileInfo(url)
    {}

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

Image::Image(const QFileInfo &url) : AbstractListItem(ListItemType::Image), d(std::make_unique<Impl>(url))
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

        if(!updateRect.isValid())
        {
            return;
        }

        d->cachedUpdateRect = QRect();
        lck.unlock();
        emit this->previewImageUpdated(this, updateRect);
    });
}

Image::~Image()
{
    xThreadGuard g(this);
}

QString Image::getName() const
{
    return this->fileInfo().fileName();
}

bool Image::hasDecoder() const
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->state != DecodingState::Unknown;
}

const QFileInfo &Image::fileInfo() const
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
    return QRect(QPoint(0, 0), this->size());
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

    // don't hold the lock when querying this
    auto iconHeight = ANPV::globalInstance()->iconHeight();

    thumb.convertTo(QImage::Format_ARGB32_Premultiplied, Qt::ColorOnly | Qt::OrderedDither);
    
    std::unique_lock<std::recursive_mutex> lck(d->m);

    if(thumb.width() > d->thumbnail.width())
    {
        d->thumbnail = thumb;
        d->thumbnailTransformed = QPixmap();

        if(!thumb.isNull())
        {
            // precompute and transform the thumbnail for the UI thread, before we are announcing that a thumbnail is available
            this->thumbnailTransformed(iconHeight);
        }

        lck.unlock();

        if(!this->signalsBlocked())
        {
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
    QAbstractFileIconProvider *prov = ANPV::globalInstance()->iconProvider();
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
    QPixmap thumb = QPixmap::fromImage(this->thumbnail(), Qt::ColorOnly | Qt::DiffuseDither);

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

void Image::setAdditionalMetadata(std::unordered_map<std::string, std::string>&& m)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    d->additionalMetadata = std::move(m);
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
        infoStr += QStringLiteral(
                   "<b>This is a RAW file!</b><br>"
                   "What you see is an<br>"
                   "embedded preview, which<br>"
                   "might be of lower quality<br>"
                   "than the RAW itself!<br><br>");
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
            s = QStringLiteral("unknown");
        }

        infoStr += QStringLiteral("ColorSpace: ") + s + QStringLiteral("<br><br>");

        s = exifWrapper->formatToString();

        if(!s.isEmpty())
        {
            infoStr += QStringLiteral("<b>===EXIF===</b><br><br>") + s + QStringLiteral("<br><br>");
        }
    }

    infoStr += QStringLiteral("<b>===stat()===</b><br><br>");
    if (this->fileInfo().isFile())
    {
        infoStr += QStringLiteral("File Size: ");
        infoStr += ANPV::formatByteHtmlString(this->fileInfo().size());
        infoStr += QStringLiteral("<br><br>");
    }

    QDateTime t = this->fileInfo().fileTime(QFileDevice::FileBirthTime);
    if(t.isValid())
    {
        infoStr += QStringLiteral("File created on:<br>");
        infoStr += t.toString("  yyyy-MM-dd (dddd)<br>");
        infoStr += t.toString("  hh:mm:ss<br><br>");
    }

    t = this->fileInfo().fileTime(QFileDevice::FileModificationTime);
    if(t.isValid())
    {
        infoStr += QStringLiteral("File modified on:<br>");
        infoStr += t.toString("yyyy-MM-dd (dddd)<br>");
        infoStr += t.toString("hh:mm:ss");
    }

    std::unique_lock<std::recursive_mutex> lck(d->m);
    if (!d->additionalMetadata.empty())
    {
        QString s;

        for (auto& [key, value] : d->additionalMetadata)
        {
            s += QStringLiteral("<br><br><i>") + QString::fromStdString(key) + QStringLiteral(":</i>");
            s += QStringLiteral("<br>") + QString::fromStdString(value).replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        }

        if (!s.isEmpty())
        {
            infoStr += QStringLiteral("<br><br><b>===Decoder Text===</b>") + s;
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
    bool isRaw = LibRawHelper::rawFilesList().contains(formatHint);
    return isRaw;
}

// If this->isRaw() == true and a neighbor is set, that neighbor will be JPEG (or TIF) image with the same name.
// If this->isRaw() == false, the neighbor will be RAW file the same name.
void Image::setNeighbor(const QSharedPointer<Image>& newNeighbor)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);

    if (d->neighbor)
    {
        auto neighbor = d->neighbor.toStrongRef();
        Q_ASSERT(neighbor != nullptr);

        if (neighbor)
        {
            neighbor->disconnect(this);
        }
    }

    d->neighbor = newNeighbor.toWeakRef();
    if (newNeighbor)
    {
        connect(newNeighbor.get(), &Image::destroyed, this, [&]()
            {
                d->neighbor = nullptr;
                emit this->thumbnailChanged(this, d->thumbnail);
                emit this->checkStateChanged(this, d->checked, d->checked);
            });

        if (this->isRaw())
        {
            connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this, [&](ViewFlags_t neu, ViewFlags_t old)
                {
                    bool before = (old & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0;
                    bool after = (neu & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0;

                    if (before != after)
                    {
                        auto n = d->neighbor.toStrongRef();
                        if (n)
                        {
                            bool previewIsChecked = n->checked();
                            bool selfIsChecked = d->checked;

                            if (previewIsChecked != selfIsChecked)
                            {
                                if (after)
                                {
                                    // we shall now combine raws and jpegs, i.e. the raw takes the checkstate of the preview
                                    emit this->checkStateChanged(this, previewIsChecked, selfIsChecked);
                                }
                                else
                                {
                                    // we shall stop combining, i.e. the raw takes its own checkstate again
                                    emit this->checkStateChanged(this, selfIsChecked, previewIsChecked);
                                }
                            }
                        }
                    }
                });
        }

        emit this->thumbnailChanged(this, d->thumbnail);
        emit this->checkStateChanged(this, newNeighbor->d->checked, d->checked);
    }
}

bool Image::hideIfNonRawAvailable(ViewFlags_t viewFlags) const
{
    return ((viewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
           && this->isRaw()
           && d->neighbor;
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
    std::unique_lock<std::recursive_mutex> lck(d->m);
    return d->errorMessage;
}

void Image::setErrorMessage(const QString &err)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);

    if(d->errorMessage != err)
    {
        d->errorMessage = err;
    }
}

Qt::CheckState Image::checked()
{
    std::unique_lock<std::recursive_mutex> lck(d->m);

    auto n = d->neighbor.toStrongRef();
    if (n && this->hideIfNonRawAvailable(ANPV::globalInstance()->viewFlags()))
    {
        return n->checked();
    }

    return d->checked;
}

void Image::setChecked(Qt::CheckState b)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);
    auto old = d->checked;

    if(old != b)
    {
        d->checked = b;
        auto n = d->neighbor.toStrongRef();

        lck.unlock();

        if (!this->isRaw() && n && (ANPV::globalInstance()->viewFlags() & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
        {
            // also signal the checkStateChange for our parent RAW
            emit n->checkStateChanged(n.get(), b, old);
        }
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

void Image::updatePreviewImage(const QRect &r)
{
    std::unique_lock<std::recursive_mutex> lck(d->m);

    if(!r.isValid())
    {
        // reset and stop timer
        d->cachedUpdateRect = QRect();
        QMetaObject::invokeMethod(d->updateRectTimer, &QTimer::stop, Qt::AutoConnection);
        return;
    }

    QRect updateRect = d->cachedUpdateRect;
    updateRect = updateRect.isValid() ? updateRect.united(r) : r;
    d->cachedUpdateRect = updateRect;
    Q_ASSERT(updateRect.isValid());
    lck.unlock();

    QMetaObject::invokeMethod(this, [&]()
    {
        if(!d->updateRectTimer->isActive())
        {
            d->updateRectTimer->start();
        }
    }, Qt::AutoConnection);
}

void Image::connectNotify(const QMetaMethod &signal)
{
    if(signal.name() == QStringLiteral("decodingStateChanged"))
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
    else if(signal.name() == QStringLiteral("checkStateChanged"))
    {
        Qt::CheckState s = this->checked();
        emit this->checkStateChanged(this, s, s);
    }

    QObject::connectNotify(signal);
}

QSharedPointer<SmartImageDecoder> Image::decoder()
{
    return d->decoder;
}

void Image::setDecoder(const QSharedPointer<SmartImageDecoder> &dec)
{
    d->decoder = dec;
}
