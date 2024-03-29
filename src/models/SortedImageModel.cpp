
#include "SortedImageModel.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QDir>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring> // for strverscmp()
#include <list>
#include <iterator>

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
    SortedImageModel *q;

    // the data container might be shared with a DocumentView
    QSharedPointer<ImageSectionDataContainer> entries;
    QScopedPointer<DirectoryWorker> directoryWatcher;
    std::list<QSharedPointer<AbstractListItem>> visibleItemList;

    // keep track of all image decoding tasks we spawn in the background, guarded by mutex, because accessed by UI thread and directory worker thread
    std::recursive_mutex m;
    std::map<Image *, QSharedPointer<QFutureWatcher<DecodingState>>> backgroundTasks;

    std::map<Image *, QMetaObject::Connection> spinningIconDrawConnections;
    QList<Image *> checkedImages; //should contain non-owning references only, so that it can be cleared by Image::destroyed()

    // we cache the most recent iconHeight, so avoid asking ANPV::globalInstance() from a worker thread, avoiding an invoke, etc.
    int cachedIconHeight = 1;
    std::atomic<ViewFlags_t> cachedViewFlags{ static_cast<ViewFlags_t>(ViewFlag::None) };

    QPointer<QTimer> layoutChangedTimer;

    QPointer<QFutureWatcher<DecodingState>> directoryWorker;

    Impl(SortedImageModel *parent) : q(parent)
    {}

    ~Impl()
    {
        cancelAllBackgroundTasks();
        this->checkedImages.clear();
        this->visibleItemList.clear();
    }

    void cancelAllBackgroundTasks()
    {
        xThreadGuard g(q);
        std::lock_guard<std::recursive_mutex> l(m);
        // first, go through all the images, take unstarted ones from the threadpool and cancel all the other ones
        auto size = q->rowCount();

        for(int i = 0; i < size; i++)
        {
            auto img = AbstractListItem::imageCast(q->item(q->index(i, 0)));

            if(!img)
            {
                continue;
            }

            if(this->backgroundTasks.contains(img.data()))
            {
                auto &fut = this->backgroundTasks[img.data()];
                fut->disconnect(q);
                img->decoder()->cancelOrTake(fut->future());
            }
        }

        layoutChangedTimer->stop();

        // now, walk through the list again and wait for the decoders to actually finish
        // do not delete all backgroundTasks as it may already contain tasks for images from a new directory
        // it should be fine to wait while holding the lock
        for(int i = 0; i < size; i++)
        {
            auto img = AbstractListItem::imageCast(q->item(q->index(i, 0)));

            if(!img)
            {
                continue;
            }

            if(this->backgroundTasks.contains(img.data()))
            {
                auto &fut = this->backgroundTasks[img.data()];
                Q_ASSERT(!fut.isNull());
                fut->waitForFinished();
                this->onBackgroundTaskFinished(fut, img);
                // element removed, all iterators to backgroundTasks invalidated!
            }
        }
    }

    void updateLayout()
    {
        xThreadGuard g(q);

        if(!layoutChangedTimer->isActive())
        {
            layoutChangedTimer->start();
        }
    }

    void forceUpdateLayout()
    {
        xThreadGuard g(q);
        qInfo() << "forceUpdateLayout()";
        emit q->layoutAboutToBeChanged();
        emit q->layoutChanged();
    }

    void onThumbnailChanged(Image *img)
    {
        xThreadGuard g(q);
        QModelIndex m = q->index(img);

        if(m.isValid())
        {
            emit q->dataChanged(m, m, { Qt::DecorationRole });
            this->updateLayout();
        }
    }

    void onBackgroundImageTaskStateChanged(Image *img, quint32 newState, quint32)
    {
        xThreadGuard g(q);

        QModelIndex idx = q->index(img);

        if(idx.isValid())
        {
            // in case of failure, refresh the thumbnail to show to error icon
            if(newState == DecodingState::Error || newState == DecodingState::Fatal)
            {
                emit q->dataChanged(idx, idx, { Qt::DecorationRole, Qt::ToolTipRole });
            }
            else if(newState == DecodingState::Metadata)
            {
                emit q->dataChanged(idx, idx, { Qt::ToolTipRole });
            }
            else
            {
                // ignore any successful and cancelled states
            }
        }
    }

    void onBackgroundTaskFinished(const QSharedPointer<QFutureWatcher<DecodingState>> &watcher, const QSharedPointer<Image> &img)
    {
        std::lock_guard<std::recursive_mutex> l(m);

        if(this->backgroundTasks.contains(img.data()))
        {
            auto watcher2 = this->backgroundTasks[img.data()];
            Q_ASSERT(watcher2 == watcher);
            watcher->disconnect(q);
            this->backgroundTasks.erase(img.data());

            if(this->backgroundTasks.empty())
            {
                QMetaObject::invokeMethod(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::stopRendering);
                this->layoutChangedTimer->stop();
                this->forceUpdateLayout();
            }
        }
        else
        {
            // Most likely, the task has been already removed by cancelAllBackgroundTasks(). Silently ignore.
        }

        if(this->spinningIconDrawConnections.contains(img.data()))
        {
            ANPV::globalInstance()->spinningIconHelper()->disconnect(this->spinningIconDrawConnections[img.data()]);
            this->spinningIconDrawConnections.erase(img.data());

            // Reschedule an icon draw event, in case no thumbnail was obtained after decoding finished
            this->scheduleSpinningIconRedraw(img);
        }
    }

    void onBackgroundTaskStarted(const QSharedPointer<QFutureWatcher<DecodingState>> &watcher, const QSharedPointer<Image> &img)
    {
        std::lock_guard<std::recursive_mutex> l(m);
        QModelIndex idx = q->index(img);

        if(!idx.isValid())
        {
            qInfo() << "onBackgroundTaskStarted: image surprisingly gone before background task could be started?!";
            this->onBackgroundTaskFinished(watcher, img);
            return;
        }

        this->spinningIconDrawConnections[img.data()] = q->connect(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::needsRepaint, q, [img, this]()
        {
            this->scheduleSpinningIconRedraw(img);
        });
        q->connect(watcher.get(), &QFutureWatcher<DecodingState>::progressValueChanged, q, [img, this]()
        {
            this->scheduleSpinningIconRedraw(img);
        });
    }

    void scheduleSpinningIconRedraw(const QSharedPointer<Image> &img)
    {
        QModelIndex idx = q->index(img);

        if(idx.isValid())
        {
            emit q->dataChanged(idx, idx, { Qt::DecorationRole });
        }
    }
};

SortedImageModel::SortedImageModel(QObject *parent) : QAbstractTableModel(parent), d(std::make_unique<Impl>(this))
{
    d->layoutChangedTimer = new QTimer(this);
    d->layoutChangedTimer->setSingleShot(true);
    connect(d->layoutChangedTimer, &QTimer::timeout, this, [&]()
    {
        d->forceUpdateLayout();
    });

    d->entries.reset(new ImageSectionDataContainer(this));
    d->directoryWatcher.reset(new DirectoryWorker(d->entries.get()/*, this*/));
    d->directoryWatcher->moveToThread(ANPV::globalInstance()->backgroundThread());
    //connect(ANPV::globalInstance()->backgroundThread(), &QThread::finished, this->fileModel, &QObject::deleteLater);
    //connect(ANPV::globalInstance()->backgroundThread(), &QThread::finished, q, [&]() { this->fileModel = nullptr; }); // for some reason the destroyed event is not emitted or processed, leaving the pointer dangling without this

    d->directoryWorker = new QFutureWatcher<DecodingState>(this);
    connect(d->directoryWorker, &QFutureWatcher<DecodingState>::started, this, [&]()
    {
        d->layoutChangedTimer->stop();
        d->layoutChangedTimer->setInterval(500);
    });
    connect(d->directoryWorker, &QFutureWatcher<DecodingState>::canceled, this, [&]()
    {
        d->cancelAllBackgroundTasks();
    });

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
        emit this->dataChanged(this->index(0, 0), this->index(this->rowCount(), 0), { Qt::EditRole, Qt::CheckStateRole });
    });
}

SortedImageModel::~SortedImageModel()
{
    xThreadGuard(this);
    d->cancelAllBackgroundTasks();
}

QSharedPointer<ImageSectionDataContainer> SortedImageModel::dataContainer()
{
    xThreadGuard(this);
    return d->entries;
}

QFuture<DecodingState> SortedImageModel::changeDirAsync(const QString &dir)
{
    xThreadGuard(this);
    auto fut = d->directoryWatcher->changeDirAsync(dir);
    d->directoryWorker->setFuture(fut);
    return fut;
}

void SortedImageModel::decodeAllImages(DecodingState state, int imageHeight)
{
    d->entries->decodeAllImages(state, imageHeight);
}

Qt::ItemFlags SortedImageModel::flags(const QModelIndex &index) const
{
    xThreadGuard(this);

    if(!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    auto item = this->item(index);
    return this->flags(item);
}

Qt::ItemFlags SortedImageModel::flags(const QSharedPointer<AbstractListItem> &item) const
{
    xThreadGuard(this);

    if(!item)
    {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;

    bool isSection = this->data(item, SortedImageModel::ItemIsSection).toBool();

    if(isSection)
    {
        f &= ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    }
    else
    {
        f |= Qt::ItemIsUserCheckable;
        ViewFlags_t viewFlagsLocal = d->cachedViewFlags;

        if((viewFlagsLocal & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
        {
            QSharedPointer<Image> e = AbstractListItem::imageCast(item);

            if(e && e->hideIfNonRawAvailable(viewFlagsLocal))
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

int SortedImageModel::rowCount(const QModelIndex &) const
{
    xThreadGuard(this);
    return d->visibleItemList.size();
}

bool SortedImageModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    xThreadGuard(this);

    if(index.isValid())
    {
        QSharedPointer<AbstractListItem> item = this->item(index);
        Image *img = dynamic_cast<Image *>(item.data());

        if(img != nullptr)
        {
            switch(role)
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

QVariant SortedImageModel::data(const QModelIndex &index, int role) const
{
    xThreadGuard(this);

    if(!index.isValid())
    {
        return QVariant();
    }

    auto item = this->item(index);
    return this->data(item, role);
}

QVariant SortedImageModel::data(const QSharedPointer<AbstractListItem> &item, int role) const
{
    xThreadGuard(this);

    if(item)
    {
        if(role == ItemModelUserRoles::ItemName)
        {
            return item->getName();
        }
        else if(role == ItemModelUserRoles::ItemIsSection)
        {
            return item->getType() == ListItemType::Section;
        }
        else
        {
            auto img = AbstractListItem::imageCast(item);

            if(img != nullptr)
            {
                const QFileInfo fi = img->fileInfo();

                switch(role)
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

                    if(!exif.isNull())
                    {
                        switch(role)
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

                        if(d->backgroundTasks.contains(img.data()))
                        {
                            watcher = d->backgroundTasks[img.data()];
                        }
                    }

                    if(watcher && watcher->isRunning())
                    {
                        QPixmap frame = ANPV::globalInstance()->spinningIconHelper()->getProgressIndicator(*watcher);
                        return frame;
                    }

                    return img->thumbnailTransformed(d->cachedIconHeight);
                }

                case Qt::ToolTipRole:
                {
                    switch (img->decodingState())
                    {
                    case Error:
                    case Fatal:
                        return img->errorMessage();

                    default:
                    {
                        QString info;
                        bool decodePending = false;
                        if(fi.isFile())
                        {
                            std::lock_guard<std::recursive_mutex> l(d->m);
                            decodePending = d->backgroundTasks.contains(img.data());
                        }
                        if (decodePending)
                        {
                            info = QStringLiteral("Decoding not yet started");
                        }
                        else
                        {
                            info = img->formatInfoString();
                        }

                        return info;
                    }
                    }
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

bool SortedImageModel::insertRows(int row, int count, const QModelIndex &parent)
{
    return false;
}

bool SortedImageModel::insertRows(int row, std::list<QSharedPointer<AbstractListItem>> &items)
{
    xThreadGuard(this);

    if(items.empty() || row < 0)
    {
        Q_ASSERT(false);
        return false;
    }

    this->beginInsertRows(QModelIndex(), row, row + items.size() - 1);

    auto insertIt = d->visibleItemList.begin();
    std::advance(insertIt, row);
    d->visibleItemList.splice(insertIt, items);

    this->endInsertRows();

    // We override ThumbnailListView::rowsInserted() to avoid flickering, but the view needs a call to QListView::doItemsLayout() when inserting items to correctly show the icons.
    // If we read a directory with reading EXIF data, the thumbnails will already be ready and therefore no layout change events will be emitted.
    // And that's why we update the layout here.
    d->updateLayout();
    return true;
}

bool SortedImageModel::removeRows(int row, int count, const QModelIndex &parent)
{
    xThreadGuard(this);
    auto rc = this->rowCount();
    Q_ASSERT(row >= 0 && count >= 0);
    Q_ASSERT(row + count <= rc);

    this->beginRemoveRows(parent, row, row + count - 1);

    auto first = d->visibleItemList.begin();
    std::advance(first, row);

    auto last = first;
    std::advance(last, count);

    {
        std::lock_guard<std::recursive_mutex> l(d->m);

        for(auto it = first; it != last; ++it)
        {
            // first, go through all the images, take unstarted ones from the threadpool and cancel all the other ones
            auto img = AbstractListItem::imageCast(*it);

            if(!img)
            {
                continue;
            }

            if(d->backgroundTasks.contains(img.data()))
            {
                auto &fut = d->backgroundTasks[img.data()];
                fut->disconnect(this);
                img->decoder()->cancelOrTake(fut->future());
            }
        }

        for(auto it = first; it != last; ++it)
        {
            // now, walk through the list again and wait for the decoders to actually finish
            // do not delete all backgroundTasks as it may already contain tasks for images from a new directory
            // it should be fine to wait while holding the lock
            auto img = AbstractListItem::imageCast(*it);

            if(!img)
            {
                continue;
            }

            if(d->backgroundTasks.contains(img.data()))
            {
                auto &fut = d->backgroundTasks[img.data()];
                Q_ASSERT(!fut.isNull());
                fut->waitForFinished();
                d->onBackgroundTaskFinished(fut, img);
                // element removed, all iterators to backgroundTasks invalidated!
            }
        }
    }

    // now that all pending background tasks have been removed, we can delete the owning references to those images
    d->visibleItemList.erase(first, last);

    this->endRemoveRows();

    return true;
}

QModelIndex SortedImageModel::index(const QSharedPointer<Image> &img)
{
    xThreadGuard(this);
    return this->index(img.data());
}

QModelIndex SortedImageModel::index(const Image *img)
{
    xThreadGuard(this);

    if(img == nullptr)
    {
        return QModelIndex();
    }

    int k = 0;

    for(auto &i : d->visibleItemList)
    {
        if(i.data() == static_cast<const AbstractListItem *>(img))
        {
            return this->index(k, 0);
        }

        k++;
    }

    return QModelIndex();
}

QSharedPointer<AbstractListItem> SortedImageModel::item(const QModelIndex &idx) const
{
    xThreadGuard(this);

    if(!idx.isValid())
    {
        return nullptr;
    }

    auto it = d->visibleItemList.begin();
    std::advance(it, idx.row());

    if(it == d->visibleItemList.end())
    {
        return nullptr;
    }

    return *it;
}

QList<Image *> SortedImageModel::checkedEntries()
{
    xThreadGuard(this);
    return d->checkedImages;
}

bool SortedImageModel::isSafeToChangeDir()
{
    xThreadGuard(this);
    return d->checkedImages.size() == 0;
}

// this function makes all conections required right after having added a new image to the model, making sure decoding progress is displayed, etc
// this function is thread-safe
void SortedImageModel::welcomeImage(const QSharedPointer<Image> &image, const QSharedPointer<QFutureWatcher<DecodingState>> &watcher)
{
    Q_ASSERT(image != nullptr);

    this->connect(image.data(), &Image::decodingStateChanged, this,
                  [&](Image * img, quint32 newState, quint32 old)
    {
        d->onBackgroundImageTaskStateChanged(img, newState, old);
    });

    this->connect(image.data(), &Image::thumbnailChanged, this, [&](Image * i, QImage)
    {
        d->onThumbnailChanged(i);
    });

    this->connect(image.data(), &Image::checkStateChanged, this,
                  [&](Image * i, int c, int old)
    {
        if(c != old)
        {
            if(c == Qt::Unchecked)
            {
                d->checkedImages.removeAll(i);
            }
            else
            {
                // retrieve the QSharedPointer for *i
                // auto qimg = AbstractListItem::imageCast(this->item(this->index(i)));
                d->checkedImages.append(i);
            }
        }
    });

    this->connect(image.data(), &Image::destroyed, this,
                  [&](QObject * i)
    {
        Q_ASSERT(i != nullptr);
        // i is already partly destroyed, hence we cannot dynamic_cast here
        d->checkedImages.removeAll(static_cast<Image *>(i));
    });

    if(watcher != nullptr)
    {
        {
            std::lock_guard<std::recursive_mutex> l(d->m);

            if(d->backgroundTasks.empty())
            {
                QMetaObject::invokeMethod(ANPV::globalInstance()->spinningIconHelper(), &ProgressIndicatorHelper::startRendering);
            }

            d->backgroundTasks[image.data()] = watcher;
        }
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [watcher, image, this]()
        {
            d->onBackgroundTaskFinished(watcher, image);
        });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [watcher, image, this]()
        {
            d->onBackgroundTaskFinished(watcher, image);
        });
        watcher->connect(watcher.get(), &QFutureWatcher<DecodingState>::started,  this, [watcher, image, this]()
        {
            d->onBackgroundTaskStarted(watcher, image);
        });
    }
}

void SortedImageModel::cancelAllBackgroundTasks()
{
    xThreadGuard(this);
    d->cancelAllBackgroundTasks();
}

void SortedImageModel::setLayoutTimerInterval(qint64 t)
{
    d->layoutChangedTimer->setInterval(t);
}
