
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

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::unique_ptr<QPromise<DecodingState>> directoryWorker;
    
    QPointer<QFileSystemWatcher> watcher;
    QDir currentDir;
    std::vector<Entry_t> entries;
    
    // keep track of all image decoding tasks we spawn in the background, guarded by mutex, because accessed by UI thread and directory worker thread
    std::mutex m;
    std::map<QSharedPointer<SmartImageDecoder>, QSharedPointer<QFutureWatcher<DecodingState>>> backgroundTasks;
    std::map<QSharedPointer<SmartImageDecoder>, QMetaObject::Connection> spinningIconDrawConnections;
    
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
    
    // returns true if the column that is sorted against requires us to preload the image metadata
    // before we insert the items into the model
    static constexpr bool sortedColumnNeedsPreloadingMetadata(Column SortCol)
    {
        if (SortCol == Column::FileName ||
            SortCol == Column::FileSize ||
            SortCol == Column::DateModified)
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
#ifdef _WINDOWS
        std::wstring l = linfo.fileName().toCaseFolded().toStdWString();
        std::wstring r = rinfo.fileName().toCaseFolded().toStdWString();
        return StrCmpLogicalW(l.c_str(),r.c_str()) < 0;
#else
        QByteArray lfile = linfo.fileName().toCaseFolded().toUtf8();
        QByteArray rfile = rinfo.fileName().toCaseFolded().toUtf8();
        return strverscmp(lfile.constData(), rfile.constData()) < 0;
#endif
    }

    template<Column SortCol>
    static bool sortColumnPredicateLeftBeforeRight(const QSharedPointer<Image>& limg, const QFileInfo& linfo, const QSharedPointer<Image>& rimg, const QFileInfo& rinfo)
    {
        if constexpr (SortCol == Column::FileName)
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
        
        bool leftFileNameIsBeforeRight = compareFileName(linfo, rinfo);

        if constexpr(sortedColumnNeedsPreloadingMetadata(SortCol))
        {
            // only evaluate exif() when sortedColumnNeedsPreloadingMetadata() is true!
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
                else if constexpr (SortCol == Column::Resolution)
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
        }

        return leftFileNameIsBeforeRight;
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
    static bool topLevelSortFunction(const Entry_t& l, const Entry_t& r)
    {
        const QFileInfo& linfo = SortedImageModel::image(l)->fileInfo();
        const QFileInfo& rinfo = SortedImageModel::image(r)->fileInfo();

        bool leftIsBeforeRight =
            (linfo.isDir() && (!rinfo.isDir() || compareFileName(linfo, rinfo))) ||
            (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(SortedImageModel::image(l), linfo, SortedImageModel::image(r), rinfo));
        
        return leftIsBeforeRight;
    }

    std::function<bool(const Entry_t&, const Entry_t&)> getSortFunction()
    {
        switch (currentSortedCol)
        {
        case Column::FileName:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::FileName>(l, r); };
        
        case Column::FileSize:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::FileSize>(l, r); };
            
        case Column::DateModified:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::DateModified>(l, r); };
            
        case Column::Resolution:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::Resolution>(l, r); };

        case Column::DateRecorded:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::DateRecorded>(l, r); };

        case Column::Aperture:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::Aperture>(l, r); };

        case Column::Exposure:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::Exposure>(l, r); };

        case Column::Iso:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::Iso>(l, r); };

        case Column::FocalLength:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::FocalLength>(l, r); };

        case Column::Lens:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::Lens>(l, r); };

        case Column::CameraModel:
            return [=](const Entry_t& l, const Entry_t& r) { return topLevelSortFunction<Column::CameraModel>(l, r); };

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
        while(itBegin != std::end(entries) && !SortedImageModel::image(*itBegin)->hasDecoder())
        {
            ++itBegin;
        }
        
        // find end to reverse
        auto itEnd = std::end(entries) - 1;
        while(itEnd != std::begin(entries) && !SortedImageModel::image(*itEnd)->hasDecoder())
        {
            --itEnd;
        }
        
        if(std::distance(itBegin, itEnd) > 1)
        {
            std::reverse(itBegin, itEnd);
        }
    }

    Entry_t entry(const QModelIndex& idx) const
    {
        if(idx.isValid())
        {
            return this->entry(idx.row());
        }
        throw std::invalid_argument("QModelIndex was invalid.");
    }

    Entry_t entry(unsigned int row) const
    {
        xThreadGuard g(q);
        this->waitForDirectoryWorker();
        if(row < this->entries.size())
        {
            return this->entries[row];
        }
        throw std::invalid_argument(Formatter() << "Entry number '" << row << "' not found.");
    }

    void onDirectoryLoaded()
    {
        xThreadGuard g(q);
        q->beginInsertRows(QModelIndex(), 0, entries.size()-1);
        q->endInsertRows();
    }
    
    void waitForDirectoryWorker() const
    {
        if(directoryWorker != nullptr && !directoryWorker->future().isFinished())
        {
            directoryWorker->future().waitForFinished();
        }
    }
    
    void cancelAllBackgroundTasks()
    {
        std::lock_guard<std::mutex> l(m);
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
    }

    void onBackgroundImageTaskStateChanged(Image* img, quint32 newState, quint32)
    {
        xThreadGuard g(q);
        if(newState == DecodingState::Ready)
        {
            // ignore ready state
            return;
        }
        if(!this->directoryWorker->future().isFinished())
        {
            // directory worker still running, once done he will reset the model. no need to dataChanged()
            return;
        }

        QModelIndex idx = q->index(img);
        if(idx.isValid())
        {
            emit q->dataChanged(idx, idx, {Qt::DecorationRole, Qt::ToolTipRole});
        }
    }
    
    void onBackgroundTaskFinished(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<SmartImageDecoder>& dec)
    {
        std::lock_guard<std::mutex> l(m);
        auto& watcher2 = this->backgroundTasks[dec];
        Q_ASSERT(watcher2.get() == watcher.get());
        watcher->disconnect(q);
        spinningIconHelper->disconnect(this->spinningIconDrawConnections[dec]);
        this->spinningIconDrawConnections.erase(dec);
        this->backgroundTasks.erase(dec);
        if (this->backgroundTasks.empty())
        {
            this->spinningIconHelper->stopRendering();
        }
    }

    void onBackgroundTaskStarted(const QSharedPointer<QFutureWatcher<DecodingState>>& watcher, const QSharedPointer<SmartImageDecoder>& dec)
    {
        QModelIndex idx = q->index(dec->image());
        Q_ASSERT(idx.isValid());
        this->spinningIconDrawConnections[dec] = q->connect(spinningIconHelper, &ProgressIndicatorHelper::needsRepaint, q, [=]() { this->scheduleSpinningIconRedraw(idx); });
        q->connect(watcher.get(), &QFutureWatcher<DecodingState>::progressValueChanged, q, [=]() { this->scheduleSpinningIconRedraw(idx); });
    }

    void scheduleSpinningIconRedraw(const QModelIndex& idx)
    {
        emit q->dataChanged(idx, idx, { Qt::DecorationRole });
    }

    void setStatusMessage(int prog, const QString& msg)
    {
        directoryWorker->setProgressValueAndText(prog , msg);
    }
    
    void updateLayout()
    {
        xThreadGuard g(q);
        if(!layoutChangedTimer.isActive())
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
    
    bool addSingleFile(QFileInfo&& inf, const QSize& iconSize, std::vector<QSharedPointer<SmartImageDecoder>>* decoderList)
    {
        auto image = DecoderFactory::globalInstance()->makeImage(inf);
        auto decoder = QSharedPointer<SmartImageDecoder>(DecoderFactory::globalInstance()->getDecoder(image).release());
        
        // move both objects to the UI thread to ensure proper signal delivery
        image->moveToThread(QGuiApplication::instance()->thread());

        connect(image.data(), &Image::decodingStateChanged, q,
                [&](Image* img, quint32 newState, quint32 old)
                { onBackgroundImageTaskStateChanged(img, newState, old); }
            , Qt::QueuedConnection);
        connect(image.data(), &Image::thumbnailChanged, q,
                [&](Image*, QImage){ updateLayout(); });

        this->entries.push_back(std::make_pair(image, decoder));
        
        if(decoder)
        {
            try
            {
                if(decoderList == nullptr || sortedColumnNeedsPreloadingMetadata(this->currentSortedCol))
                {
                    decoder->open();
                    // decode synchronously
                    decoder->decode(DecodingState::Metadata, iconSize);
                    decoder->close();
                }
                else
                {
                    decoderList->emplace_back(std::move(decoder));
                }
            }
            catch(const std::exception& e)
            {
                throw std::logic_error("todo: handle error, display error");
            }
            
            return true;
        }
        else
        {
            QMetaObject::invokeMethod(image.data(), &Image::lookupIconFromFileType);
        }
        return false;
    }
    
    void onDirectoryChanged(const QString& path)
    {
        xThreadGuard g(q);
        if(path != this->currentDir.absolutePath())
        {
            return;
        }
        
        if(!(this->directoryWorker && this->directoryWorker->future().isFinished()))
        {
            return;
        }
        
        QFileInfoList fileInfoList = currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        
        q->beginResetModel();
        for(auto it = entries.begin(); it != entries.end();)
        {
            const QFileInfo& eInfo = SortedImageModel::image(*it)->fileInfo();
            auto result = std::find_if(fileInfoList.begin(),
                                    fileInfoList.end(),
                                    [&](const QFileInfo& other)
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
            int iconHeight = cachedIconHeight;
            QSize iconSize(iconHeight, iconHeight);
            
            // any file still in the list are new, we need to add them
            for(QFileInfo i : fileInfoList)
            {
                addSingleFile(std::move(i), iconSize, nullptr);
            }
        }
        
        sortEntries();
        if(sortOrder == Qt::DescendingOrder)
        {
            reverseEntries();
        }
        q->endResetModel();
    }
    
    bool hideRawIfNonRawAvailable(const QSharedPointer<Image>& e)
    {
        xThreadGuard g(q);
        return ((this->cachedViewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
                 && e->isRaw()
                 && (e->hasEquallyNamedJpeg() || e->hasEquallyNamedTiff());
    }
};

SortedImageModel::SortedImageModel(QObject* parent) : QAbstractTableModel(parent), d(std::make_unique<Impl>(this))
{
    this->setAutoDelete(false);
    
    d->layoutChangedTimer.setInterval(1000);
    d->layoutChangedTimer.setSingleShot(true);
    connect(&d->layoutChangedTimer, &QTimer::timeout, this, [&](){ d->forceUpdateLayout();});
    
    d->watcher = new QFileSystemWatcher(this);
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, [&](const QString& p){ d->onDirectoryChanged(p);});
    
    d->spinningIconHelper = new ProgressIndicatorHelper(this);

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

void SortedImageModel::run()
{
    int entriesProcessed = 0;
    try
    {
        d->directoryWorker->start();
        
        d->setStatusMessage(0, "Looking up directory");
        QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const int entriesToProcess = fileInfoList.size();
        if(entriesToProcess > 0)
        {
            d->directoryWorker->setProgressRange(0, entriesToProcess + 2 /* for sorting effort + starting the decoding */);
            
            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            if (d->sortedColumnNeedsPreloadingMetadata(d->currentSortedCol))
            {
                msg += " and reading EXIF data (making it quite slow)";
            }
            
            d->setStatusMessage(0, msg);
            d->entries.reserve(entriesToProcess);
            d->entries.shrink_to_fit();
            unsigned readableImages = 0;
            int iconHeight = d->cachedIconHeight;
            QSize iconSize(iconHeight, iconHeight);
            std::vector<QSharedPointer<SmartImageDecoder>> decodersToRunLater;
            decodersToRunLater.reserve(entriesToProcess);
            while (!fileInfoList.isEmpty())
            {
                do
                {
                    QFileInfo inf = fileInfoList.takeFirst();
                    if(d->addSingleFile(std::move(inf), iconSize, &decodersToRunLater))
                    {
                        ++readableImages;
                    }
                } while (false);

                d->throwIfDirectoryLoadingCancelled();
                d->setStatusMessage(entriesProcessed++, msg);
            }

            d->setStatusMessage(entriesProcessed++, "Sorting entries, please wait...");
            d->sortEntries();
            if (d->sortOrder == Qt::DescendingOrder)
            {
                d->reverseEntries();
            }

            d->throwIfDirectoryLoadingCancelled();
            if(!decodersToRunLater.empty())
            {
                d->setStatusMessage(entriesProcessed++, "Directory read, starting async decoding tasks in the background.");

                std::lock_guard<std::mutex> l(d->m);
                for(auto& decoder : decodersToRunLater)
                {
                    QSharedPointer<QFutureWatcher<DecodingState>> watcher(new QFutureWatcher<DecodingState>());
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::started,  this, [=]() { d->onBackgroundTaskStarted(watcher, decoder); });
                    watcher->moveToThread(QGuiApplication::instance()->thread());

                    // decode asynchronously
                    auto fut = decoder->decodeAsync(DecodingState::Metadata, Priority::Background, iconSize);
                    watcher->setFuture(fut);

                    d->backgroundTasks[decoder] = (watcher);
                }
                QMetaObject::invokeMethod(d->spinningIconHelper.get(), &ProgressIndicatorHelper::startRendering, Qt::QueuedConnection);
            }

            d->setStatusMessage(entriesProcessed, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
            QMetaObject::invokeMethod(this, [&](){ d->onDirectoryLoaded(); }, Qt::QueuedConnection);
        }
        else
        {
            d->directoryWorker->setProgressRange(0, 1);
            entriesProcessed++;
            if(d->currentDir.exists())
            {
                d->setStatusMessage(entriesProcessed, "Directory is empty, nothing to see here.");
            }
            else
            {
                throw std::runtime_error("Directory does not exist");
            }
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
    d->directoryWorker->finish();
}

void SortedImageModel::decodeAllImages(DecodingState state, int imageHeight)
{
    xThreadGuard(this);
    d->waitForDirectoryWorker();
    d->cancelAllBackgroundTasks();

    std::lock_guard<std::mutex> l(d->m);
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

Entry_t SortedImageModel::entry(const QModelIndex& idx) const
{
    return d->entry(idx);
}

Entry_t SortedImageModel::entry(unsigned int row) const
{
    return d->entry(row);
}

const QSharedPointer<Image>& SortedImageModel::image(const Entry_t& e)
{
    return std::get<0>(e);
}

const QSharedPointer<SmartImageDecoder>& SortedImageModel::decoder(const Entry_t& e)
{
    return std::get<1>(e);
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

    Qt::ItemFlags f = this->QAbstractTableModel::flags(index);
    f |= Qt::ItemIsUserCheckable;
    
    if(index.isValid() && (d->cachedViewFlags & static_cast<ViewFlags_t>(ViewFlag::CombineRawJpg)) != 0)
    {
        QSharedPointer<Image> e = this->image(this->entry(index));
        if(e && d->hideRawIfNonRawAvailable(e))
        {
            f &= ~(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
        }
    }
    
    return f;
}

int SortedImageModel::columnCount(const QModelIndex &) const
{
    xThreadGuard g(this);
    return Column::Count;
}

int SortedImageModel::rowCount(const QModelIndex&) const
{
    xThreadGuard g(this);

    if (d->directoryWorker && d->directoryWorker->future().isFinished())
    {
        return d->entries.size();
    }
    else
    {
        return 0;
    }
}

bool SortedImageModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    xThreadGuard g(this);

    if (index.isValid())
    {
        d->waitForDirectoryWorker();
        Entry_t e = this->entry(index);
        const QSharedPointer<Image>& i = this->image(e);
        int column = index.column();
        switch (role)
        {
        case Qt::CheckStateRole:
            i->setChecked(static_cast<Qt::CheckState>(value.toInt()));
            emit this->dataChanged(index, index, { role });
            return true;
        }
    }
    return false;
}

QVariant SortedImageModel::data(const QModelIndex& index, int role) const
{
    xThreadGuard g(this);

    if (index.isValid())
    {
        d->waitForDirectoryWorker();
        Entry_t e = this->entry(index);
        const QSharedPointer<Image>& i = this->image(e);
        const QSharedPointer<SmartImageDecoder>& dec = this->decoder(e);
        const QFileInfo fi = i->fileInfo();
        int column = index.column();
        switch (role)
        {
        case Qt::DisplayRole:
            switch (column)
            {
            case Column::FileName:
                return fi.fileName();
            case Column::FileSize:
                return QString::number(fi.size());
            case Column::DateModified:
                return fi.lastModified();
            default:
            {
                auto exif = i->exif();
                if (!exif.isNull())
                {
                    switch (column)
                    {
                    case Column::DateRecorded:
                        return exif->dateRecorded();
                    case Column::Resolution:
                        return exif->size();
                    case Column::Aperture:
                        return exif->aperture();
                    case Column::Exposure:
                        return exif->exposureTime();
                    case Column::Iso:
                        return exif->iso();
                    case Column::FocalLength:
                        return exif->focalLength();
                    case Column::Lens:
                        return exif->lens();
                    case Column::CameraModel:
                        throw std::logic_error("not yet implemented");
                    }
                }
                break;
            }
            }
            break;
        case Qt::DecorationRole:
        {
            QSharedPointer<QFutureWatcher<DecodingState>> watcher;
            {
                std::lock_guard<std::mutex> l(d->m);
                if (d->backgroundTasks.contains(dec))
                {
                    watcher = d->backgroundTasks[dec];
                }
            }
            if (watcher && watcher->isRunning())
            {
                QPixmap frame = d->spinningIconHelper->getProgressIndicator(*watcher);
                return frame;
            }
            return i->thumbnailTransformed(d->cachedIconHeight);
        }
        case Qt::ToolTipRole:
            switch(i->decodingState())
            {
                case Ready:
                    return "Decoding not yet started";
                case Cancelled:
                    return "Decoding cancelled";
                case Error:
                case Fatal:
                    return i->errorMessage();
                default:
                    return i->formatInfoString();
            }

        case Qt::TextAlignmentRole:
        {
            constexpr Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
            constexpr int a = alignment;
            return a;
        }
        case Qt::CheckStateRole:
            return i->checked();
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
        default:
            break;
        }
    }
    return QVariant();
}

bool SortedImageModel::insertRows(int row, int count, const QModelIndex& parent)
{
    xThreadGuard g(this);

    return false;

    this->beginInsertRows(parent, row, row + count - 1);

    this->endInsertRows();
}

void SortedImageModel::sort(int column, Qt::SortOrder order)
{
    xThreadGuard g(this);
    d->waitForDirectoryWorker();

    bool sortColChanged = d->currentSortedCol != column;
    bool sortOrderChanged = d->sortOrder != order;
    
    d->currentSortedCol = static_cast<Column>(column);
    d->sortOrder = order;
    
    if(d->directoryWorker && d->directoryWorker->future().isFinished())
    {
        WaitCursor w;
        
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
    }
}

void SortedImageModel::sort(Column column)
{
    xThreadGuard g(this);

    this->sort(column, d->sortOrder);
}

void SortedImageModel::sort(Qt::SortOrder order)
{
    xThreadGuard g(this);

    this->sort(d->currentSortedCol, order);
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

    d->waitForDirectoryWorker();
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](const Entry_t& other)
                            { return this->image(other).data() == img; });
    
    if(result == d->entries.end())
    {
        return QModelIndex();
    }
    
    int idx = std::distance(d->entries.begin(), result);
    return this->index(idx, 0);
}

QFileInfo SortedImageModel::fileInfo(const QModelIndex &index) const
{
    xThreadGuard g(this);

    if(index.isValid())
    {
        d->waitForDirectoryWorker();
        return this->image(this->entry(index))->fileInfo();
    }
    
    return QFileInfo();
}

QList<Entry_t> SortedImageModel::checkedEntries()
{
    QList<Entry_t> results;
    results.reserve(this->rowCount());
    for (Entry_t& e : d->entries)
    {
        auto img = this->image(e);
        Qt::CheckState chk = img->checked();
        if (chk != Qt::CheckState::Unchecked)
        {
            results.push_back(e);
        }
    }
    return results;
}
