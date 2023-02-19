
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
#include "DirectoryWorker.hpp"

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    QScopedPointer<ImageSectionDataContainer> entries;
    QScopedPointer<DirectoryWorker> directoryWatcher;
    
    // keep track of all image decoding tasks we spawn in the background, guarded by mutex, because accessed by UI thread and directory worker thread
    std::recursive_mutex m;
    std::map<Image*, QSharedPointer<QFutureWatcher<DecodingState>>> backgroundTasks;
    std::map<Image*, QMetaObject::Connection> spinningIconDrawConnections;
    QList<Image*> checkedImages;
    
    // we cache the most recent iconHeight, so avoid asking ANPV::globalInstance() from a worker thread, avoiding an invoke, etc.
    int cachedIconHeight = 1;
    std::atomic<ViewFlags_t> cachedViewFlags{ static_cast<ViewFlags_t>(ViewFlag::None) };
    
    QTimer layoutChangedTimer;

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
                Q_ASSERT(!future.isNull());
                future->disconnect(q);
                Q_ASSERT(!future.isNull());
                future->cancel();
            }

            for (const auto& [key, value] : backgroundTasks)
            {
                auto& future = value;
                Q_ASSERT(!future.isNull());
                future->waitForFinished();
            }
            layoutChangedTimer.stop();
            backgroundTasks.clear();
            QMetaObject::invokeMethod(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::stopRendering);
        }
    }
    
    // stop processing, delete everything and wait until finished
    void clear()
    {
        this->checkedImages.clear();
        cancelAllBackgroundTasks();
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

    static bool hideRawIfNonRawAvailable(ViewFlags_t viewFlags, const QSharedPointer<Image>& e)
    {
        return ((viewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
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
        xThreadGuard g(q);
        if (newState == DecodingState::Ready)
        {
            // ignore ready state
            return;
        }

        int i = this->entries->getLinearIndexOfItem(img);
        QModelIndex idx = q->index(i, 0);
        if (idx.isValid())
        {
            // precompute and transform the thumbnail for the UI thread, before we are announcing that a thumbnail is available
            img->thumbnailTransformed(this->cachedIconHeight);
            emit q->dataChanged(idx, idx, { Qt::DecorationRole, Qt::ToolTipRole });
        }
    }

    void onBackgroundTaskFinished(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<Image>& img)
    {
        std::lock_guard<std::recursive_mutex> l(m);
        auto watcher2 = this->backgroundTasks[img.data()];
        Q_ASSERT(watcher2 == watcher);
        watcher->disconnect(q);
        ANPV::globalInstance()->spinningIconHelper()->disconnect(this->spinningIconDrawConnections[img.data()]);
        this->spinningIconDrawConnections.erase(img.data());
        this->backgroundTasks.erase(img.data());
        if (this->backgroundTasks.empty())
        {
            QMetaObject::invokeMethod(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::stopRendering);
            this->updateLayout();
        }

        // Reschedule an icon draw event, in case no thumbnail was obtained after decoding finished
        this->scheduleSpinningIconRedraw(img);
    }

    void onBackgroundTaskStarted(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<Image>& img)
    {
        std::lock_guard<std::recursive_mutex> l(m);
        QModelIndex idx = q->index(img);
        if (!idx.isValid())
        {
            qInfo() << "onBackgroundTaskStarted: image surprisingly gone before background task could be started?!";
            this->onBackgroundTaskFinished(watcher, img);
            return;
        }
        if (this->spinningIconDrawConnections.empty())
        {
            QMetaObject::invokeMethod(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::startRendering);
        }
        this->spinningIconDrawConnections[img.data()] = q->connect(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::needsRepaint, q, [=]() { this->scheduleSpinningIconRedraw(img); });
        q->connect(watcher.get(), &QFutureWatcher<DecodingState>::progressValueChanged, q, [=]() { this->scheduleSpinningIconRedraw(img); });
    }

    void scheduleSpinningIconRedraw(const QSharedPointer<Image>& img)
    {
        QModelIndex idx = q->index(img);
        emit q->dataChanged(idx, idx, { Qt::DecorationRole });
    }
};

SortedImageModel::SortedImageModel(QObject* parent) : QAbstractTableModel(parent), d(std::make_unique<Impl>(this))
{
    d->layoutChangedTimer.setInterval(1000);
    d->layoutChangedTimer.setSingleShot(true);
    connect(&d->layoutChangedTimer, &QTimer::timeout, this, [&](){ d->forceUpdateLayout();});
    
    d->entries.reset(new ImageSectionDataContainer(this));
    d->directoryWatcher.reset(new DirectoryWorker(d->entries.get(), this));

    connect(ANPV::globalInstance(), &ANPV::iconHeightChanged, this,
            [&](int v)
            {
                d->cachedIconHeight = v;
                d->updateLayout();
            });

    connect(ANPV::globalInstance(), &ANPV::imageSortOrderChanged, this,
            [&](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                d->entries->sortImageItems(newField, newOrder);
            });

    connect(ANPV::globalInstance(), &ANPV::sectionSortOrderChanged, this,
            [&](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                d->entries->sortSections(newField, newOrder);
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
    d->cancelAllBackgroundTasks();
}

QFuture<DecodingState> SortedImageModel::changeDirAsync(const QString& dir)
{
    d->clear();
    return d->directoryWatcher->changeDirAsync(dir);
}

void SortedImageModel::decodeAllImages(DecodingState state, int imageHeight)
{
    d->entries->decodeAllImages(state, imageHeight);
}

QSharedPointer<Image> SortedImageModel::goTo(const QSharedPointer<Image>& img, int stepsFromCurrent) const
{
    return {};
#if 0
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
#endif
}

Qt::ItemFlags SortedImageModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
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
        ViewFlags_t viewFlagsLocal = d->cachedViewFlags;
        if ((viewFlagsLocal & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
        {
            QSharedPointer<Image> e = this->imageFromItem(this->item(index));
            if (e && d->hideRawIfNonRawAvailable(viewFlagsLocal, e))
            {
                f &= ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            }
        }
    }
    
    return f;
}

int SortedImageModel::columnCount(const QModelIndex &) const
{
    return 1;
}

int SortedImageModel::rowCount(const QModelIndex&) const
{
    return d->entries->size();
}

bool SortedImageModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
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
                auto img = this->imageFromItem(item);
                if (img != nullptr)
                {
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
                            if (d->backgroundTasks.contains(img.data()))
                            {
                                watcher = d->backgroundTasks[img.data()];
                            }
                        }
                        if (watcher && watcher->isRunning())
                        {
                            QPixmap frame = ANPV::globalInstance()->spinningIconHelper()->getProgressIndicator(*watcher);
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
    }
    return QVariant();
}

bool SortedImageModel::insertRows(int row, int count, const QModelIndex& parent)
{
    return false;
}

QModelIndex SortedImageModel::index(const QSharedPointer<Image>& img)
{
    return this->index(img.data());
}

QModelIndex SortedImageModel::index(const Image* img)
{
    if(img == nullptr)
    {
        return QModelIndex();
    }

    return this->index(d->entries->getLinearIndexOfItem(img), 0);
}

QSharedPointer<AbstractListItem> SortedImageModel::item(const QModelIndex& idx) const
{
    if (!idx.isValid())
    {
        return nullptr;
    }

    return d->entries->getItemByLinearIndex(idx.row());
}

QSharedPointer<Image> SortedImageModel::imageFromItem(const QSharedPointer<AbstractListItem>& item) const
{
    if (item != nullptr && item->getType() == ListItemType::Image)
    {
        return qSharedPointerDynamicCast<Image>(item);
    }

    return nullptr;
}

QList<Image*> SortedImageModel::checkedEntries()
{
    return d->checkedImages;
}

bool SortedImageModel::isSafeToChangeDir()
{
    return d->checkedImages.size() == 0;
}

// this function makes all conections required right after having added a new image to the model, making sure decoding progress is displayed, etc
// this function is thread-safe
void SortedImageModel::welcomeImage(const QSharedPointer<Image>& image, const QSharedPointer<QFutureWatcher<DecodingState>>& watcher)
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
            Q_ASSERT(i != nullptr);
            d->checkedImages.removeAll(static_cast<Image*>(i));
        });

    if (watcher != nullptr)
    {
        {
            std::lock_guard<std::recursive_mutex> l(d->m);
            d->backgroundTasks[image.data()] = watcher;
        }
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [=]() { d->onBackgroundTaskFinished(watcher, image); });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [=]() { d->onBackgroundTaskFinished(watcher, image); });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::started,  this, [=]() { d->onBackgroundTaskStarted(watcher, image); });
    }
}

void SortedImageModel::cancelAllBackgroundTasks()
{
    d->cancelAllBackgroundTasks();
}
