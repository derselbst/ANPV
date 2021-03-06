
#include "SortedImageModel.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QFileIconProvider>
#include <QStyle>
#include <QDir>
#include <QGuiApplication>
#include <QCursor>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring>

#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"


struct Entry
{
    Entry()
    {}
    
    Entry(SortedImageModel* q, QFileInfo&& info) : q(q), info(std::move(info))
    {}
    
    Entry(SortedImageModel* q, QSharedPointer<SmartImageDecoder> d) : q(q), dec(d), future(new QFutureWatcher<DecodingState>, &QObject::deleteLater)
    {}
    
    Entry(const Entry& e) = delete;
    Entry& operator=(const Entry& e) = delete;
    
    Entry(Entry&& other) = delete;
    Entry& operator=(Entry&& other) = delete;
    
    ~Entry()
    {
        if(future && future->isRunning())
        {
            future->cancel();
            future->waitForFinished();
            // We must emit finished() manually here, because the next setFuture() call would prevent finished() signal to be emitted for this current future.
            emit future->finished();
            // Prevent emitting the finished signal twice...
            future->setFuture(QFuture<DecodingState>());
            future = nullptr;
        }
        if(dec)
        {
            // explicitly disconnect our signals
            // most decoder will be destroyed after set to null, but some may survive because e.g. their image is still opened in DocumentView
            dec->disconnect(q);
            dec = nullptr;
        }
    }
    
    const QFileInfo& getFileInfo() const
    {
        return this->hasImageDecoder() ? this->dec->fileInfo() : this->info;
    }

    const QSharedPointer<SmartImageDecoder>& getDecoder() const
    {
        return this->dec;
    }
    
    bool hasImageDecoder() const
    {
        return dec != nullptr;
    }
    
    QFutureWatcher<DecodingState>* getFutureWatcher()
    {
        return future.data();
    }
    
private:
    SortedImageModel* q = nullptr;
    QSharedPointer<SmartImageDecoder> dec;
    QSharedPointer<QFutureWatcher<DecodingState>> future;
    QFileInfo info;
};

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::atomic<int> runningBackgroundTasks{0};
    std::unique_ptr<QPromise<DecodingState>> directoryWorker;
    
    QFileSystemWatcher* watcher = nullptr;
    QDir currentDir;
    std::vector<std::unique_ptr<Entry>> entries;
    
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
    static bool sortColumnPredicateLeftBeforeRight(const Entry& l, const QFileInfo& linfo, const Entry& r, const QFileInfo& rinfo)
    {
        auto& ldec = l.getDecoder();
        auto& rdec = r.getDecoder();

        if (ldec && rdec)
        {
            if constexpr (SortCol == Column::Resolution)
            {
                QSize lsize = ldec->size();
                QSize rsize = rdec->size();

                if (lsize.isValid() && rsize.isValid())
                {
                    if (lsize.width() != rsize.width() && lsize.height() != rsize.height())
                    {
                        return lsize.width() * lsize.height() < rsize.width() * rsize.height();
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
            else if constexpr (SortCol == Column::DateRecorded)
            {
                QDateTime ltime = ldec->exif()->dateRecorded();
                QDateTime rtime = rdec->exif()->dateRecorded();
                
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
                
                ldec->exif()->aperture(lap);
                rdec->exif()->aperture(rap);
                
                if(lap != rap)
                {
                    return lap < rap;
                }
            }
            else if constexpr (SortCol == Column::Exposure)
            {
                double lex,rex;
                lex = rex = std::numeric_limits<double>::max();
                
                ldec->exif()->exposureTime(lex);
                rdec->exif()->exposureTime(rex);
                
                if(lex != rex)
                {
                    return lex < rex;
                }
            }
            else if constexpr (SortCol == Column::Iso)
            {
                long liso,riso;
                liso = riso = std::numeric_limits<long>::max();
                
                ldec->exif()->iso(liso);
                rdec->exif()->iso(riso);
                
                if(liso != riso)
                {
                    return liso < riso;
                }
            }
            else if constexpr (SortCol == Column::FocalLength)
            {
                double ll,rl;
                ll = rl = std::numeric_limits<double>::max();
                
                ldec->exif()->focalLength(ll);
                rdec->exif()->focalLength(rl);
                
                if(ll != rl)
                {
                    return ll < rl;
                }
            }
            else if constexpr (SortCol == Column::Lens)
            {
                QString ll,rl;
                
                ll = ldec->exif()->lens();
                rl = rdec->exif()->lens();
                
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
            else
            {
//                 static_assert("Unknown column to sort for");
            }
        }
        else if (ldec && !rdec)
        {
            return true; // l before r
        }
        else if (!ldec && rdec)
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
    static bool topLevelSortFunction(const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r)
    {
        const QFileInfo& linfo = l->getFileInfo();
        const QFileInfo& rinfo = r->getFileInfo();

        bool leftIsBeforeRight =
            (linfo.isDir() && (!rinfo.isDir() || compareFileName(linfo, rinfo))) ||
            (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(*l, linfo, *r, rinfo));
        
        return leftIsBeforeRight;
    }

    std::function<bool(const std::unique_ptr<Entry>&, const std::unique_ptr<Entry>&)> getSortFunction()
    {
        switch (currentSortedCol)
        {
        case Column::FileName:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::FileName>(l, r); };
        
        case Column::FileSize:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::FileSize>(l, r); };
            
        case Column::DateModified:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::DateModified>(l, r); };
            
        case Column::Resolution:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::Resolution>(l, r); };

        case Column::DateRecorded:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::DateRecorded>(l, r); };

        case Column::Aperture:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::Aperture>(l, r); };

        case Column::Exposure:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::Exposure>(l, r); };

        case Column::Iso:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::Iso>(l, r); };

        case Column::FocalLength:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::FocalLength>(l, r); };

        case Column::Lens:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::Lens>(l, r); };

        case Column::CameraModel:
            return [=](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) { return topLevelSortFunction<Column::CameraModel>(l, r); };

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
        
        // find the beginning to revert
        auto itBegin = std::begin(entries);
        while(itBegin != std::end(entries) && !(*itBegin)->hasImageDecoder())
        {
            ++itBegin;
        }
        
        // find end to revert
        auto itEnd = std::end(entries) - 1;
        while(itEnd != std::begin(entries) && !(*itEnd)->hasImageDecoder())
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

    void startImageDecoding(Entry& e, DecodingState targetState)
    {
        xThreadGuard g(q);
        
        auto* watch = e.getFutureWatcher();
        if(watch->isFinished()) // check whether the future has finished and whether the finished signal has been emitted
        {
            if(runningBackgroundTasks++ == 0)
            {
                qInfo() << "OVERRIDE";
                QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            }
            watch->setFuture(e.getDecoder()->decodeAsync(targetState).then(
                [&](DecodingState s)
                {
                    e.getDecoder()->icon(q->iconHeight());
                    return s;
                }
            ));
        }
    }

    void onBackgroundImageTaskStateChanged(size_t idx, quint32 newState, quint32)
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

    void onDecodingTaskFinished()
    {
        xThreadGuard g(q);
        if(!--runningBackgroundTasks)
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

    QString formatToolTipInfo(const QSharedPointer<SmartImageDecoder>& sid)
    {
        QString exifStr = sid->exif()->formatToString();
        
        if(!exifStr.isEmpty())
        {
            exifStr = QString("<b>===EXIF===</b><br><br>") + exifStr + "<br><br>";
        }
        
        static const char *const sizeUnit[] = {" Bytes", " KiB", " MiB", " <b>GiB</b>"};
        float fsize = sid->fileInfo().size();
        int i;
        for(i = 0; i<4 && fsize > 1024; i++)
        {
            fsize /= 1024.f;
        }
        
        exifStr += QString("<b>===stat()===</b><br><br>");
        exifStr += "File Size: ";
        exifStr += QString::number(fsize, 'f', 2) + sizeUnit[i];
        exifStr += "<br><br>";
        
        QDateTime t = sid->fileInfo().fileTime(QFileDevice::FileBirthTime);
        if(t.isValid())
        {
            exifStr += "Created on:<br>";
            exifStr += t.toString("  yyyy-MM-dd (dddd)<br>");
            exifStr += t.toString("  hh:mm:ss<br><br>");
        }
        
        t = sid->fileInfo().fileTime(QFileDevice::FileModificationTime);
        if(t.isValid())
        {
            exifStr += "Modified on:<br>";
            exifStr += t.toString("yyyy-MM-dd (dddd)<br>");
            exifStr += t.toString("hh:mm:ss");
        }
        
        return exifStr;
    }
    
    bool addSingleFile(QFileInfo&& inf)
    {
        if (inf.isFile())
        {
            auto decoder = DecoderFactory::globalInstance()->getDecoder(std::move(inf));
            if (decoder)
            {
                if (sortedColumnNeedsPreloadingMetadata())
                {
                    decoder->decode(DecodingState::Metadata);
                }

                auto e = std::make_unique<Entry>(q, decoder);
                e->getDecoder()->moveToThread(QGuiApplication::instance()->thread());
                e->getFutureWatcher()->moveToThread(QGuiApplication::instance()->thread());
                
                auto idx = entries.size();
                connect(e->getDecoder().data(), &SmartImageDecoder::decodingStateChanged, q, [=](SmartImageDecoder*, quint32 newState, quint32 old){ onBackgroundImageTaskStateChanged(idx, newState, old); });
                connect(e->getFutureWatcher(), &QFutureWatcher<DecodingState>::finished, q, [&](){ onDecodingTaskFinished(); });
                
                entries.push_back(std::move(e));
                return true;
            }
        }

        entries.emplace_back(std::make_unique<Entry>(q, std::move(inf)));
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
            QFileInfo eInfo = (*it)->getFileInfo();
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

    QThreadPool::globalInstance()->start(this);

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

QSharedPointer<SmartImageDecoder> SortedImageModel::goTo(const QString& currentUrl, int stepsFromCurrent, QModelIndex& idxOut)
{
    int step = (stepsFromCurrent < 0) ? -1 : 1;
    
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](std::unique_ptr<Entry>& other)
                            { return other->getFileInfo().absoluteFilePath() == currentUrl; });
    
    if(result == d->entries.end())
    {
        qCritical() << "This should not happen: currentUrl not found.";
        return nullptr;
    }
    
    int size = d->entries.size();
    int idx = std::distance(d->entries.begin(), result);
    const Entry* e;
    do
    {
        if(idx >= size - step || // idx + step >= size
            idx < -step) // idx + step < 0
        {
            return nullptr;
        }
        
        idx += step;
        
        e = d->entries[idx].get();
        QFileInfo eInfo = e->getFileInfo();
        if(e->hasImageDecoder() && eInfo.suffix() != "bak")
        {
            stepsFromCurrent -= step;
        }
        else
        {
            // skip unsupported files
        }
        
    } while(stepsFromCurrent);
    
    idxOut = this->index(idx, 0);
    return e->getDecoder();
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
        const std::unique_ptr<Entry>& e = d->entries.at(index.row());
        QFileInfo fileInfo = e->getFileInfo();

        switch (role)
        {
        case Qt::DisplayRole:
            return fileInfo.fileName();

        case Qt::DecorationRole:
            if (e->hasImageDecoder())
            {
                auto state = e->getDecoder()->decodingState();
                switch (state)
                {
                case DecodingState::Ready:
                    d->startImageDecoding(*e, DecodingState::Metadata);
                    break;

                default:
                case DecodingState::Error:
                    return QIcon::fromTheme("dialog-error");

                case DecodingState::Metadata:
                case DecodingState::PreviewImage:
                case DecodingState::FullImage:
                {
                    QPixmap thumbnail = e->getDecoder()->icon(iconHeight());
                    if (!thumbnail.isNull())
                    {
                        return thumbnail;
                    }
                }
                }
            }
            return d->iconProvider.icon(fileInfo).pixmap(iconHeight(),iconHeight()).scaledToHeight(iconHeight());

        case Qt::ToolTipRole:
            if (e->hasImageDecoder())
            {
                switch (e->getDecoder()->decodingState())
                {
                case DecodingState::Error:
                    return QString("<b>%1</b><br><br>Latest Message was:<br>%2")
                    .arg(e->getDecoder()->errorMessage())
                    .arg(e->getDecoder()->latestMessage());
                    
                case DecodingState::Metadata:
                case DecodingState::PreviewImage:
                case DecodingState::FullImage:
                    return d->formatToolTipInfo(e->getDecoder());
    
                default:
                    break;
                }
            }
            return QVariant();

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
                            [&](std::unique_ptr<Entry>& other)
                            { return other->getFileInfo() == info; });
    
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
        return d->entries.at(index.row())->getFileInfo();
    }
    
    return QFileInfo();
}

QSharedPointer<SmartImageDecoder> SortedImageModel::decoder(const QModelIndex &index)
{
    if(index.isValid())
    {
        return d->entries.at(index.row())->getDecoder();
    }
    
    return nullptr;
}
