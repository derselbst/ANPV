
#include "SortedImageModel.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QFileIconProvider>
#include <QDir>
#include <QGuiApplication>
#include <QCursor>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring> // for strverscmp()

#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "Image.hpp"


struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::atomic<int> runningBackgroundTasks{0};
    std::unique_ptr<QPromise<DecodingState>> directoryWorker;
    
    QFileSystemWatcher* watcher = nullptr;
    QDir currentDir;
    std::vector<QSharedPointer<Image>> entries;
    QMap<QSharedPointer<SmartImageDecoder>, QSharedPointer<QFutureWatcher<DecodingState>>> backgroundTasks;
    
    // The column which is currently sorted
    Column currentSortedCol = Column::FileName;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    
    int iconHeight = 150;
    
    QTimer layoutChangedTimer;
    QFileIconProvider iconProvider;

    Impl(SortedImageModel* parent) : q(parent)
    {}
    
    ~Impl()
    {
        clear();
    }
    
    // returns true if the column that is sorted against requires us to preload the image metadata
    // before we insert the items into the model
    bool sortedColumnNeedsPreloadingMetadata()
    {
        if (currentSortedCol == Column::FileName ||
            currentSortedCol == Column::FileSize ||
            currentSortedCol == Column::DateModified)
        {
            return false;
        }
        else
        {
            return true;
        }
    }

    void throwIfDirectoryLoadingCancelled()
    {
        if(directoryWorker->isCanceled())
        {
            throw UserCancellation();
        }
    }
    
    static bool compareFileName(const QFileInfo& linfo, const QFileInfo& rinfo)
    {
        QByteArray lfile = linfo.fileName().toCaseFolded().toLatin1();
        QByteArray rfile = rinfo.fileName().toCaseFolded().toLatin1();
        
        return strverscmp(lfile.constData(), rfile.constData()) < 0;
    }

    template<Column SortCol>
    static bool sortColumnPredicateLeftBeforeRight(const QSharedPointer<Image>& limg, const QFileInfo& linfo, const QSharedPointer<Image>& rimg, const QFileInfo& rinfo)
    {
        if constexpr (SortCol == Column::Resolution)
        {
            QSize lsize = limg->size();
            QSize rsize = rimg->size();

            if (lsize.isValid() && rsize.isValid())
            {
                if (lsize.width() != rsize.width() && lsize.height() != rsize.height())
                {
                    return static_cast<size_t>(lsize.width()) * lsize.height() < static_cast<size_t>(rsize.width()) * rsize.height();
                }
            }
            else if (lsize.isValid())
            {
                return true;
            }
            else if (rsize.isValid())
            {
                return false;
            }
        }
        else if constexpr (SortCol == Column::FileName)
        {
            // nothing to do here, we use the fileName comparison below
        }
        else if constexpr (SortCol == Column::FileSize)
        {
            return linfo.size() < rinfo.size();
        }
        else if constexpr (SortCol == Column::DateModified)
        {
            return linfo.lastModified() < rinfo.lastModified();
        }

        auto lexif = limg->exif();
        auto rexif = rimg->exif();

        if (lexif && rexif)
        {
            if constexpr (SortCol == Column::DateRecorded)
            {
                QDateTime ltime = lexif->dateRecorded();
                QDateTime rtime = rexif->dateRecorded();
                
                if (ltime.isValid() && rtime.isValid())
                {
                    if (ltime != rtime)
                    {
                        return ltime < rtime;
                    }
                }
                else if(ltime.isValid())
                {
                    return true;
                }
                else if(rtime.isValid())
                {
                    return false;
                }
            }
            else if constexpr (SortCol == Column::Aperture)
            {
                double lap,rap;
                lap = rap = std::numeric_limits<double>::max();
                
                lexif->aperture(lap);
                rexif->aperture(rap);
                
                if(lap != rap)
                {
                    return lap < rap;
                }
            }
            else if constexpr (SortCol == Column::Exposure)
            {
                double lex,rex;
                lex = rex = std::numeric_limits<double>::max();
                
                lexif->exposureTime(lex);
                rexif->exposureTime(rex);
                
                if(lex != rex)
                {
                    return lex < rex;
                }
            }
            else if constexpr (SortCol == Column::Iso)
            {
                long liso,riso;
                liso = riso = std::numeric_limits<long>::max();
                
                lexif->iso(liso);
                rexif->iso(riso);
                
                if(liso != riso)
                {
                    return liso < riso;
                }
            }
            else if constexpr (SortCol == Column::FocalLength)
            {
                double ll,rl;
                ll = rl = std::numeric_limits<double>::max();
                
                lexif->focalLength(ll);
                rexif->focalLength(rl);
                
                if(ll != rl)
                {
                    return ll < rl;
                }
            }
            else if constexpr (SortCol == Column::Lens)
            {
                QString ll,rl;
                
                ll = lexif->lens();
                rl = rexif->lens();
                
                if(!ll.isEmpty() && !rl.isEmpty())
                {
                    return ll < rl;
                }
                else if (!ll.isEmpty())
                {
                    return true;
                }
                else if (!rl.isEmpty())
                {
                    return false;
                }
            }
            else if constexpr (SortCol == Column::CameraModel)
            {
                throw std::logic_error("not yet implemented");
            }
            else
            {
//                 static_assert("Unknown column to sort for");
            }
        }
        else if (lexif && !rexif)
        {
            return true; // l before r
        }
        else if (!lexif && rexif)
        {
            return false; // l behind r
        }

        return compareFileName(linfo, rinfo);
    }

    // This is the entry point for sorting. It sorts all Directories first.
    // Second criteria is to sort according to fileName
    // For regular files it dispatches the call to sortColumnPredicateLeftBeforeRight()
    //
    // |   L  \   R    | DIR  | SortCol | UNKNOWN |
    // |      DIR      |  1   |   1     |    1    |
    // |     SortCol   |  0   |   1     |    1    |
    // |    UNKNOWN    |  0   |   0     |    1    |
    //
    template<Column SortCol>
    static bool topLevelSortFunction(const QSharedPointer<Image>& l, const QSharedPointer<Image>& r)
    {
        const QFileInfo& linfo = l->fileInfo();
        const QFileInfo& rinfo = r->fileInfo();

        bool leftIsBeforeRight =
            (linfo.isDir() && (!rinfo.isDir() || compareFileName(linfo, rinfo))) ||
            (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(l, linfo, r, rinfo));
        
        return leftIsBeforeRight;
    }

    std::function<bool(const QSharedPointer<Image>&, const QSharedPointer<Image>&)> getSortFunction()
    {
        switch (currentSortedCol)
        {
        case Column::FileName:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::FileName>(l, r); };
        
        case Column::FileSize:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::FileSize>(l, r); };
            
        case Column::DateModified:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::DateModified>(l, r); };
            
        case Column::Resolution:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::Resolution>(l, r); };

        case Column::DateRecorded:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::DateRecorded>(l, r); };

        case Column::Aperture:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::Aperture>(l, r); };

        case Column::Exposure:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::Exposure>(l, r); };

        case Column::Iso:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::Iso>(l, r); };

        case Column::FocalLength:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::FocalLength>(l, r); };

        case Column::Lens:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::Lens>(l, r); };

        case Column::CameraModel:
            return [=](const QSharedPointer<Image>& l, const QSharedPointer<Image>& r) { return topLevelSortFunction<Column::CameraModel>(l, r); };

        default:
            throw std::logic_error(Formatter() << "No sorting function implemented for column " << currentSortedCol);
        }
    }

    void sortEntries()
    {
        auto sortFunction = getSortFunction();
        std::sort(/*std::execution::par_unseq, */std::begin(entries), std::end(entries), sortFunction);
    }
    
    void reverseEntries()
    {
        // only reverse entries that have an image decoder
        
        // find the beginning to reverse
        auto itBegin = std::begin(entries);
        while(itBegin != std::end(entries) && !(*itBegin)->hasDecoder())
        {
            ++itBegin;
        }
        
        // find end to reverse
        auto itEnd = std::end(entries) - 1;
        while(itEnd != std::begin(entries) && !(*itEnd)->hasDecoder())
        {
            --itEnd;
        }
        
        if(std::distance(itBegin, itEnd) > 1)
        {
            std::reverse(itBegin, itEnd);
        }
    }

    void onDirectoryLoaded()
    {
        xThreadGuard g(q);
        q->beginInsertRows(QModelIndex(), 0, entries.size());
        q->endInsertRows();
    }
    
    // stop processing, delete everything and wait until finished
    void clear()
    {
        if(directoryWorker != nullptr && !directoryWorker->future().isFinished())
        {
            directoryWorker->future().cancel();
            directoryWorker->future().waitForFinished();
        }

        directoryWorker = std::make_unique<QPromise<DecodingState>>();
        watcher->removePath(currentDir.absolutePath());
        currentDir = QDir();
    }

    void onBackgroundImageTaskStateChanged(SmartImageDecoder* dec, quint32 newState, quint32)
    {
        xThreadGuard g(q);
        if(newState == DecodingState::Ready)
        {
            // ignore ready state
            return;
        }
        // A thumbnail may be inserted into the list.
        // This typically happens when the user has clicked on an image that does not have an embedded thumbnail.
        updateLayout();
    }

    void onDecodingTaskFinished(QSharedPointer<SmartImageDecoder> dec)
    {
        xThreadGuard g(q);
        
        backgroundTasks.remove(dec);
        if(backgroundTasks.empty())
        {
            layoutChangedTimer.stop();
            forceUpdateLayout();
            QGuiApplication::restoreOverrideCursor();
            qInfo() << "RESTORE";
        }
        else
        {
            updateLayout();
        }
    }
    
    void setStatusMessage(int prog, const QString& msg)
    {
        directoryWorker->setProgressValueAndText(prog , msg);
    }
    
    void updateLayout()
    {
        layoutChangedTimer.start();
    }
    
    void forceUpdateLayout()
    {
        qInfo() << "forceUpdateLayout()";
        emit q->layoutAboutToBeChanged();
        emit q->layoutChanged();
    }
    
    bool addSingleFile(QFileInfo&& inf)
    {
        auto image = DecoderFactory::globalInstance()->makeImage(inf);
        auto decoder = DecoderFactory::globalInstance()->getDecoder(image);
        
        // move both objects to the UI thread to ensure proper signal delivery
        image->moveToThread(QGuiApplication::instance()->thread());
        decoder->moveToThread(QGuiApplication::instance()->thread());
        entries.push_back(image);
        
        if(decoder)
        {
            try
            {
                QSize iconSize(iconHeight, iconHeight);
                decoder->open();
                
                if(sortedColumnNeedsPreloadingMetadata())
                {
                    // decode synchronously
                    decoder->decode(DecodingState::Metadata, iconSize);
                }
                else
                {
                    auto& watcher = backgroundTasks[decoder];
                    if(!watcher.isNull())
                    {
                        qCritical() << "This shouldn't happen: Image to be added is already decoding??";
                        return false;
                    }
                    
                    watcher.reset(new QFutureWatcher<DecodingState>(), &QObject::deleteLater);
                    // decode asynchronously
                    auto fut = decoder->decodeAsync(DecodingState::Metadata, Priority::Background, iconSize);
                    watcher->setFuture(fut);
                    
                    connect(decoder.data(), &SmartImageDecoder::decodingStateChanged, q,
                            [=](SmartImageDecoder* dec, quint32 newState, quint32 old)
                            { onBackgroundImageTaskStateChanged(dec, newState, old); }
                        );
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, q,
                            [=](){ onDecodingTaskFinished(decoder); }
                        );
                }
            }
            catch(const std::exception& e)
            {
                throw std::logic_error("todo: handle error, display error");
            }
            
            return true;
        }
        return false;
    }
    
    void onDirectoryChanged(const QString& path)
    {
        if(path != this->currentDir.absolutePath())
        {
            return;
        }
        
        QFileInfoList fileInfoList = currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        
        q->beginResetModel();
        for(auto it = entries.begin(); it != entries.end();)
        {
            QFileInfo eInfo = (*it)->fileInfo();
            auto result = std::find_if(fileInfoList.begin(),
                                    fileInfoList.end(),
                                    [=](QFileInfo& other)
                                    { return eInfo == other; });
            
            if(result == fileInfoList.end())
            {
                // file e doesn't exist in the directory, probably deleted
                it = entries.erase(it);
                continue;
            }
            
            // file e is still up to date
            fileInfoList.erase(result);
            ++it;
        }

        if(!fileInfoList.isEmpty())
        {
            // any file still in the list are new, we need to add them
            for(QFileInfo i : fileInfoList)
            {
                addSingleFile(std::move(i));
            }
        }
        
        sortEntries();
        if(sortOrder == Qt::DescendingOrder)
        {
            reverseEntries();
        }
        q->endResetModel();
    }
};

SortedImageModel::SortedImageModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Impl>(this))
{
    this->setAutoDelete(false);
    
    d->layoutChangedTimer.setInterval(1000);
    d->layoutChangedTimer.setSingleShot(true);
    connect(&d->layoutChangedTimer, &QTimer::timeout, this, [&](){ d->forceUpdateLayout();});
    
    d->watcher = new QFileSystemWatcher(parent);
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, [&](const QString& p){ d->onDirectoryChanged(p);});
}

SortedImageModel::~SortedImageModel()
{
    xThreadGuard(this);
}

QFuture<DecodingState> SortedImageModel::changeDirAsync(const QDir& dir)
{
    this->beginRemoveRows(QModelIndex(), 0, rowCount());
    d->clear();
    
    d->currentDir = dir;

    QThreadPool::globalInstance()->start(this, static_cast<int>(Priority::Normal));

    return d->directoryWorker->future();
}

void SortedImageModel::run()
{
    int entriesProcessed = 0;
    try
    {
        d->directoryWorker->start();
        d->setStatusMessage(0, "Clearing old entries");
        d->entries.clear();
        
        d->setStatusMessage(0, "Looking up directory");
        QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const int entriesToProcess = fileInfoList.size();
        if(entriesToProcess > 0)
        {
            d->directoryWorker->setProgressRange(0, entriesToProcess + 1 /* for sorting effort */);
            
            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            if (d->sortedColumnNeedsPreloadingMetadata())
            {
                msg += " and reading EXIF data (making it quite slow)";
            }
            
            d->setStatusMessage(0, msg);
            d->entries.reserve(entriesToProcess);
            d->entries.shrink_to_fit();
            unsigned readableImages = 0;
            while (!fileInfoList.isEmpty())
            {
                do
                {
                    QFileInfo inf = fileInfoList.takeFirst();
                    if(d->addSingleFile(std::move(inf)))
                    {
                        ++readableImages;
                    }
                } while (false);

                d->throwIfDirectoryLoadingCancelled();
                d->setStatusMessage(entriesProcessed++, msg);
            }

            d->setStatusMessage(entriesProcessed++, "Almost done: Sorting entries, please wait...");
            d->sortEntries();
            if(d->sortOrder == Qt::DescendingOrder)
            {
                d->reverseEntries();
            }
            
            d->setStatusMessage(entriesProcessed, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
            QMetaObject::invokeMethod(this, [&](){ d->onDirectoryLoaded(); }, Qt::QueuedConnection);
        }
        else
        {
            d->directoryWorker->setProgressRange(0, 1);
            d->setStatusMessage(1, "Directory is empty, nothing to see here.");
        }
            
        d->directoryWorker->addResult(DecodingState::FullImage);
        d->watcher->addPath(d->currentDir.absolutePath());
    }
    catch (const UserCancellation&)
    {
        d->directoryWorker->addResult(DecodingState::Cancelled);
    }
    catch (const std::exception& e)
    {
        d->setStatusMessage(entriesProcessed, QString("Exception occurred while loading the directory: %1").arg(e.what()));
        d->directoryWorker->addResult(DecodingState::Error);
    }
    catch (...)
    {
        d->setStatusMessage(entriesProcessed, "Fatal error occurred while loading the directory");
        d->directoryWorker->addResult(DecodingState::Error);
    }
    this->endRemoveRows();
    d->directoryWorker->finish();
}

QSharedPointer<Image> SortedImageModel::goTo(const QString& currentUrl, int stepsFromCurrent, QModelIndex& idxOut)
{
    int step = (stepsFromCurrent < 0) ? -1 : 1;
    
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](QSharedPointer<Image>& other)
                            { return other->fileInfo().absoluteFilePath() == currentUrl; });
    
    if(result == d->entries.end())
    {
        qCritical() << "This should not happen: currentUrl not found.";
        return nullptr;
    }
    
    int size = d->entries.size();
    int idx = std::distance(d->entries.begin(), result);
    QSharedPointer<Image> e;
    do
    {
        if(idx >= size - step || // idx + step >= size
            idx < -step) // idx + step < 0
        {
            return nullptr;
        }
        
        idx += step;
        
        e = d->entries[idx];
        QFileInfo eInfo = e->fileInfo();
        if(e->hasDecoder() && eInfo.suffix() != "bak")
        {
            stepsFromCurrent -= step;
        }
        else
        {
            // skip unsupported files
        }
        
    } while(stepsFromCurrent);
    
    idxOut = this->index(idx, 0);
    return e;
}


int SortedImageModel::columnCount(const QModelIndex &) const
{
    return Column::Count;
}

int SortedImageModel::rowCount(const QModelIndex&) const
{
    if (d->directoryWorker && d->directoryWorker->future().isFinished())
    {
        return d->entries.size();
    }
    else
    {
        return 0;
    }
}

QVariant SortedImageModel::data(const QModelIndex& index, int role) const
{
    if (index.isValid())
    {
        const QSharedPointer<Image>& e = d->entries.at(index.row());
        const QFileInfo& fileInfo = e->fileInfo();

        switch (role)
        {
        case Qt::DisplayRole:
            return fileInfo.fileName();

        case Qt::DecorationRole:
            return e->icon(iconHeight());

        case Qt::ToolTipRole:
            return e->formatInfoString();

        case Qt::TextAlignmentRole:
            if (index.column() == Column::FileName)
            {
                const Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
                return int(alignment);
            }
            [[fallthrough]];
        case Qt::EditRole:
        case Qt::StatusTipRole:
        case Qt::WhatsThisRole:
        default:
            break;
        }
    }
    return QVariant();
}

bool SortedImageModel::insertRows(int row, int count, const QModelIndex& parent)
{
    return false;

    this->beginInsertRows(parent, row, row + count - 1);

    this->endInsertRows();
}

void SortedImageModel::sort(int column, Qt::SortOrder order)
{
    bool sortColChanged = d->currentSortedCol != column;
    bool sortOrderChanged = d->sortOrder != order;
    
    d->currentSortedCol = static_cast<Column>(column);
    d->sortOrder = order;
    
    if(d->directoryWorker && d->directoryWorker->future().isFinished())
    {
        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        
        d->setStatusMessage(0, "Sorting entries");
        this->beginResetModel();
        
        if(sortColChanged)
        {
            d->sortEntries();
            if(order == Qt::DescendingOrder)
            {
                d->reverseEntries();
            }
        }
        else if(sortOrderChanged)
        {
            d->reverseEntries();
        }
        
        this->endResetModel();
        d->setStatusMessage(100, "Sorting complete");
        
        QGuiApplication::restoreOverrideCursor();
    }
}

void SortedImageModel::sort(Column column)
{
    this->sort(column, d->sortOrder);
}

void SortedImageModel::sort(Qt::SortOrder order)
{
    this->sort(d->currentSortedCol, order);
}

int SortedImageModel::iconHeight() const
{
    return d->iconHeight;
}

void SortedImageModel::setIconHeight(int iconHeight)
{
    emit this->layoutAboutToBeChanged();
    d->iconHeight = std::max(iconHeight, 1);
    emit this->layoutChanged();
}


QModelIndex SortedImageModel::index(const QFileInfo& info)
{
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](QSharedPointer<Image>& other)
                            { return other->fileInfo() == info; });
    
    if(result == d->entries.end())
    {
        return QModelIndex();
    }
    
    int idx = std::distance(d->entries.begin(), result);
    return this->index(idx, 0);
}

QFileInfo SortedImageModel::fileInfo(const QModelIndex &index) const
{
    if(index.isValid())
    {
        return d->entries.at(index.row())->fileInfo();
    }
    
    return QFileInfo();
}
