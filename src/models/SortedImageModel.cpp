
#include "SortedImageModel.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QDir>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring> // for strverscmp()

#ifdef _WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#endif

#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "WaitCursor.hpp"
#include "Image.hpp"
#include "ANPV.hpp"
#include "ProgressIndicatorHelper.hpp"
#include "ImageSectionDataContainer.hpp"

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::unique_ptr<QPromise<DecodingState>> directoryWorker;
    
    QPointer<QFileSystemWatcher> watcher;
    QDir currentDir;
    QScopedPointer<ImageSectionDataContainer> entries;
    
    // keep track of all image decoding tasks we spawn in the background, guarded by mutex, because accessed by UI thread and directory worker thread
    std::recursive_mutex m;
    std::map<Image*, QSharedPointer<QFutureWatcher<DecodingState>>> backgroundTasks;
    std::map<Image*, QMetaObject::Connection> spinningIconDrawConnections;
    QList<Image*> checkedImages;
    
    // The column which is currently sorted
    Column currentSortedCol = Column::FileName;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    
    // we cache the most recent iconHeight, so avoid asking ANPV::globalInstance() from a worker thread, avoiding an invoke, etc.
    int cachedIconHeight = 1;
    ViewFlags_t cachedViewFlags = static_cast<ViewFlags_t>(ViewFlag::None);
    
    QTimer layoutChangedTimer;
    QPointer<ProgressIndicatorHelper> spinningIconHelper;

    Impl(SortedImageModel* parent) : q(parent)
    {}
    
    ~Impl()
    {
        clear();
    }
    
    void cancelAllBackgroundTasks()
    {
        std::lock_guard<std::recursive_mutex> l(m);
        if(!backgroundTasks.empty())
        {
            for (const auto& [key,value] : backgroundTasks)
            {
                auto& future = value;
                future->disconnect(q);
                future->cancel();
            }

            for (const auto& [key, value] : backgroundTasks)
            {
                auto& future = value;
                future->waitForFinished();
            }
            layoutChangedTimer.stop();
            backgroundTasks.clear();
            spinningIconHelper->stopRendering();
        }
    }
    
    // stop processing, delete everything and wait until finished
    void clear()
    {
        xThreadGuard g(q);
        if(directoryWorker != nullptr && !directoryWorker->future().isFinished())
        {
            directoryWorker->future().cancel();
            directoryWorker->future().waitForFinished();
        }
        
        cancelAllBackgroundTasks();
        entries.clear();

        directoryWorker = std::make_unique<QPromise<DecodingState>>();
        watcher->removePath(currentDir.absolutePath());
        currentDir = QDir();
        this->numberOfCheckedImages = 0;
    }

    void updateLayout()
    {
        xThreadGuard g(q);
        if (!layoutChangedTimer.isActive())
        {
            layoutChangedTimer.start();
        }
    }

    void forceUpdateLayout()
    {
        xThreadGuard g(q);
        qInfo() << "forceUpdateLayout()";
        emit q->layoutAboutToBeChanged();
        emit q->layoutChanged();
    }

    bool hideRawIfNonRawAvailable(const QSharedPointer<Image>& e)
    {
        xThreadGuard g(q);
        return ((this->cachedViewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
                 && e->isRaw()
                 && (e->hasEquallyNamedJpeg() || e->hasEquallyNamedTiff());
    }

    void onThumbnailChanged(Image* img)
    {
        int i = this->entries->getLinearIndexOfItem(img);
        QPersistentModelIndex pm = q->index(i, 0);
        if (pm.isValid())
        {
            emit q->layoutAboutToBeChanged({ pm });
            emit q->dataChanged(pm, pm, { Qt::DecorationRole });
            emit q->layoutChanged();
        }
    }

    void onBackgroundImageTaskStateChanged(Image* img, quint32 newState, quint32)
    {
        if (newState == DecodingState::Ready)
        {
            // ignore ready state
            return;
        }

        int i = this->entries->getLinearIndexOfItem(img);
        QModelIndex idx = q->index(i, 0);
        if (idx.isValid())
        {
            emit q->dataChanged(idx, idx, { Qt::DecorationRole, Qt::ToolTipRole });
        }
    }

    void onBackgroundTaskFinished(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<Image>& img)
    {
        std::lock_guard<std::recursive_mutex> l(m);
        auto& watcher2 = this->backgroundTasks[img.data()];
        Q_ASSERT(watcher2.get() == watcher.get());
        watcher->disconnect(q);
        this->spinningIconHelper->disconnect(this->spinningIconDrawConnections[img.data()]);
        this->spinningIconDrawConnections.erase(img.data());
        this->backgroundTasks.erase(img.data());
        if (this->backgroundTasks.empty())
        {
            this->spinningIconHelper->stopRendering();
        }

        // Reschedule an icon draw event, in case no thumbnail was obtained after decoding finished
        this->scheduleSpinningIconRedraw(q->index(img));
    }

    void onBackgroundTaskStarted(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<Image>& img)
    {
        QModelIndex idx = q->index(img);
        if (!idx.isValid())
        {
            qInfo() << "onBackgroundTaskStarted: image surprisingly gone before background task could be started?!";
            this->onBackgroundTaskFinished(watcher, img);
            return;
        }
        this->spinningIconDrawConnections[img.data()] = q->connect(this->spinningIconHelper, &ProgressIndicatorHelper::needsRepaint, q, [=]() { this->scheduleSpinningIconRedraw(idx); });
        q->connect(watcher.get(), &QFutureWatcher<DecodingState>::progressValueChanged, q, [=]() { this->scheduleSpinningIconRedraw(idx); });
    }

    void scheduleSpinningIconRedraw(const QModelIndex& idx)
    {
        emit q->dataChanged(idx, idx, { Qt::DecorationRole });
    }
};

SortedImageModel::SortedImageModel(QObject* parent) : QAbstractTableModel(parent), d(std::make_unique<Impl>(this))
{
    d->layoutChangedTimer.setInterval(1000);
    d->layoutChangedTimer.setSingleShot(true);
    connect(&d->layoutChangedTimer, &QTimer::timeout, this, [&](){ d->forceUpdateLayout();});
    
    d->spinningIconHelper = new ProgressIndicatorHelper(this);
    d->entries.reset(new ImageSectionDataContainer(this));

    connect(ANPV::globalInstance(), &ANPV::iconHeightChanged, this,
            [&](int v)
            {
                d->cachedIconHeight = v;
                d->updateLayout();
            });

    connect(ANPV::globalInstance(), &ANPV::sortOrderChanged, this,
            [&](Qt::SortOrder newOrd, Qt::SortOrder)
            {
                this->sort(newOrd);
            });

    connect(ANPV::globalInstance(), &ANPV::primarySortColumnChanged, this,
            [&](SortedImageModel::Column newCol, SortedImageModel::Column)
            {
                this->sort(newCol);
            });
    
    connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this,
            [&](ViewFlags_t v, ViewFlags_t)
            {
                d->cachedViewFlags = v;
                d->forceUpdateLayout();
            });
}

SortedImageModel::~SortedImageModel()
{
    xThreadGuard(this);
}

QFuture<DecodingState> SortedImageModel::changeDirAsync(const QString& dir)
{
    xThreadGuard g(this);

    auto rowCount = this->rowCount();
    if (rowCount != 0)
    {
        this->beginRemoveRows(QModelIndex(), 0, rowCount-1);
    }

    d->clear();
    d->currentDir = QDir(dir);

    if (rowCount != 0)
    {
        this->endRemoveRows();
    }

    QThreadPool::globalInstance()->start(this, static_cast<int>(Priority::Normal));
    return d->directoryWorker->future();
}

void SortedImageModel::decodeAllImages(DecodingState state, int imageHeight)
{
    xThreadGuard(this);
    d->waitForDirectoryWorker();
    d->cancelAllBackgroundTasks();

    std::lock_guard<std::recursive_mutex> l(d->m);
    if (!d->entries.empty())
    {
        d->spinningIconHelper->startRendering();
    }
    for(Entry_t& e : d->entries)
    {
        const QSharedPointer<Image>& image = SortedImageModel::image(e);
        const QSharedPointer<SmartImageDecoder>& decoder = SortedImageModel::decoder(e);
        if(decoder)
        {
            bool taken = QThreadPool::globalInstance()->tryTake(decoder.get());
            if(taken)
            {
                qWarning() << "Decoder '0x" << (void*)decoder.get() << "' was surprisingly taken from the ThreadPool's Queue???";
            }
            QImage thumb = image->thumbnail();
            if (state == DecodingState::PreviewImage && !thumb.isNull())
            {
                qDebug() << "Skipping preview decoding of " << image->fileInfo().fileName() << " as it already has a thumbnail of sufficient size.";
                continue;
            }

            QSharedPointer<QFutureWatcher<DecodingState>> watcher(new QFutureWatcher<DecodingState>());
            connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
            connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
            connect(watcher.get(), &QFutureWatcher<DecodingState>::started,  this, [=]() { d->onBackgroundTaskStarted(watcher, decoder); });

            // decode asynchronously
            auto fut = decoder->decodeAsync(state, Priority::Background, QSize(imageHeight, imageHeight));
            watcher->setFuture(fut);
            fut.then(
                [=](DecodingState result)
                {
                    decoder->releaseFullImage();
                    return result;
                });

            d->backgroundTasks[decoder] = (watcher);
        }
    }
}

Entry_t SortedImageModel::goTo(const QSharedPointer<Image>& img, int stepsFromCurrent)
{
    xThreadGuard g(this);
    d->waitForDirectoryWorker();

    int step = (stepsFromCurrent < 0) ? -1 : 1;
    
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](const Entry_t& other)
                            { return this->image(other) == img; });
    
    if(result == d->entries.end())
    {
        qInfo() << "requested image not found";
        return {};
    }
    
    int size = d->entries.size();
    int idx = std::distance(d->entries.begin(), result);
    Entry_t e;
    do
    {
        if(idx >= size - step || // idx + step >= size
            idx < -step) // idx + step < 0
        {
            return {};
        }
        
        idx += step;
        
        e = this->entry(idx);
        QFileInfo eInfo = this->image(e)->fileInfo();
        bool shouldSkip = eInfo.suffix() == "bak";
        shouldSkip |= d->hideRawIfNonRawAvailable(this->image(e));
        if(this->image(e)->hasDecoder() && !shouldSkip)
        {
            stepsFromCurrent -= step;
        }
        else
        {
            // skip unsupported files
        }
        
    } while(stepsFromCurrent);

    return e;
}

Qt::ItemFlags SortedImageModel::flags(const QModelIndex &index) const
{
    xThreadGuard g(this);
    if (!index.isValid())
    {
        return;
    }

    Qt::ItemFlags f = this->QAbstractTableModel::flags(index);

    bool isSection = index.model()->data(index, SortedImageModel::ItemIsSection).toBool();
    if (isSection)
    {
        f &= ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    }
    else
    {
        f |= Qt::ItemIsUserCheckable;
        if ((d->cachedViewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
        {
            QSharedPointer<Image> e = this->image(this->entry(index));
            if (e && d->hideRawIfNonRawAvailable(e))
            {
                f &= ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            }
        }
    }
    
    return f;
}

int SortedImageModel::columnCount(const QModelIndex &) const
{
    xThreadGuard g(this);
    return 1;
}

int SortedImageModel::rowCount(const QModelIndex&) const
{
    xThreadGuard g(this);
    return d->entries->size();
}

bool SortedImageModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    xThreadGuard g(this);

    if (index.isValid())
    {
        QSharedPointer<AbstractListItem> item = d->entries->getItemByLinearIndex(index.row());
        Image* img = dynamic_cast<Image*>(item.data());
        if (img != nullptr)
        {
            switch (role)
            {
            case Qt::CheckStateRole:
                img->setChecked(static_cast<Qt::CheckState>(value.toInt()));
                emit this->dataChanged(index, index, { role });
                return true;
            }
        }
    }
    return false;
}

QVariant SortedImageModel::data(const QModelIndex& index, int role) const
{
    xThreadGuard g(this);

    if (index.isValid())
    {
        QSharedPointer<AbstractListItem> item = d->entries->getItemByLinearIndex(index.row());
        if (item)
        {
            if (role == ItemModelUserRoles::ItemName)
            {
                return item->getName();
            }
            else if (role == ItemModelUserRoles::ItemIsSection)
            {
                return item->getType() == ListItemType::Section;
            }
            else
            {
                Image* img = dynamic_cast<Image*>(item.data());
                Q_ASSERT(item->getType() == ListItemType::Image);
                Q_ASSERT(item != nullptr);

                const QFileInfo fi = img->fileInfo();
                switch (role)
                {

                case ItemFileSize:
                    return QString::number(fi.size());
                case ItemFileType:
                    return fi.suffix();
                case ItemFileLastModified:
                    return fi.lastModified();

                default:
                {
                    auto exif = img->exif();
                    if (!exif.isNull())
                    {
                        switch (role)
                        {
                        case ItemImageDateRecorded:
                            return exif->dateRecorded();
                        case ItemImageResolution:
                            return exif->size();
                        case ItemImageAperture:
                            return exif->aperture();
                        case ItemImageExposure:
                            return exif->exposureTime();
                        case ItemImageIso:
                            return exif->iso();
                        case ItemImageFocalLength:
                            return exif->focalLength();
                        case ItemImageLens:
                            return exif->lens();
                        case ItemImageCameraModel:
                            throw std::logic_error("ItemImageCameraModel not yet implemented");
                        }
                    }
                    break;
                }
                case Qt::DecorationRole:
                {
                    QSharedPointer<QFutureWatcher<DecodingState>> watcher;
                    {
                        std::lock_guard<std::recursive_mutex> l(d->m);
                        if (d->backgroundTasks.contains(img))
                        {
                            watcher = d->backgroundTasks[img];
                        }
                    }
                    if (watcher && watcher->isRunning())
                    {
                        QPixmap frame = d->spinningIconHelper->getProgressIndicator(*watcher);
                        return frame;
                    }
                    return img->thumbnailTransformed(d->cachedIconHeight);
                }
                case Qt::ToolTipRole:
                    switch (img->decodingState())
                    {
                    case Ready:
                        return "Decoding not yet started";
                    case Cancelled:
                        return "Decoding cancelled";
                    case Error:
                    case Fatal:
                        return img->errorMessage();
                    default:
                        return img->formatInfoString();
                    }

                case Qt::TextAlignmentRole:
                {
                    constexpr Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
                    constexpr int a = alignment;
                    return a;
                }
                case Qt::CheckStateRole:
                    return img->checked();
                case CheckAlignmentRole:
                {
                    constexpr Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignTop;
                    constexpr int a = alignment;
                    return a;
                }
                case DecorationAlignmentRole:
                case Qt::EditRole:
                case Qt::StatusTipRole:
                case Qt::WhatsThisRole:
                    break;
                }
            }
        }
    }
    return QVariant();
}

bool SortedImageModel::insertRows(int row, int count, const QModelIndex& parent)
{
    xThreadGuard g(this);
    return false;
}

QModelIndex SortedImageModel::index(const QSharedPointer<Image>& img)
{
    xThreadGuard g(this);

    return this->index(img.data());
}

QModelIndex SortedImageModel::index(const Image* img)
{
    xThreadGuard g(this);
    if(img == nullptr)
    {
        return QModelIndex();
    }

    return this->index(d->entries->getLinearIndexOfItem(img), 0);
}

QList<Image*> SortedImageModel::checkedEntries()
{
    xThreadGuard g(this);
    return d->checkedImages;
}

bool SortedImageModel::isSafeToChangeDir()
{
    xThreadGuard g(this);
    return d->checkedImages.size() == 0;
}

// this function makes all conections required right after having added a new image to the model, making sure decoding progress is displayed, etc
// this function is thread-safe
void SortedImageModel::welcomeImage(QSharedPointer<Image> image, QSharedPointer<SmartImageDecoder> decoder, QSharedPointer<QFutureWatcher<DecodingState>> watcher)
{
    Q_ASSERT(image != nullptr);

    this->connect(image.data(), &Image::decodingStateChanged, this,
        [&](Image* img, quint32 newState, quint32 old)
        {
            d->onBackgroundImageTaskStateChanged(img, newState, old);
        });

    this->connect(image.data(), &Image::thumbnailChanged, this, [&](Image* i, QImage) { d->onThumbnailChanged(i); });

    this->connect(image.data(), &Image::checkStateChanged, this,
        [&](Image* i, int c, int old)
        {
            if (c != old)
            {
                if (c == Qt::Unchecked)
                {
                    d->checkedImages.removeAll(i);
                }
                else
                {
                    d->checkedImages.append(i);
                }
            }
        });

    this->connect(image.data(), &Image::destroyed, this,
        [&](QObject* i)
        {
            Image* img = dynamic_cast<Image*>(i);
            Q_ASSERT(img != nullptr);
            d->checkedImages.removeAll(img);
            d->backgroundTasks.erase(img);
        });

    if (watcher != nullptr)
    {
        QMetaObject::invokeMethod(this, [=]() { d->backgroundTasks[image.data()] = watcher; });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [=]() { d->onBackgroundTaskFinished(watcher, image); });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [=]() { d->onBackgroundTaskFinished(watcher, image); });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::started,  this, [=]() { d->onBackgroundTaskStarted(watcher, image); });
    }
}
