
#include "SortedImageModel.hpp"

#include <QFuture>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QFileIconProvider>
#include <QStyle>
#include <QDir>
#include <algorithm>
#include <QGuiApplication>
#include <QCursor>
// #include <execution>

#include "ImageDecodeTask.hpp"
#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"


struct Entry
{
    Entry() : info(QFileInfo())
    {}
    
    Entry(QFileInfo&& info) : info(std::move(info))
    {}
    
    Entry(QSharedPointer<SmartImageDecoder> d) : dec(d)
    {}
    
    Entry(const Entry& e) = delete;
    Entry& operator=(const Entry& e) = delete;
    
    Entry(Entry&& other)
    {
        *this = std::move(other);
    }
    
    Entry& operator=(Entry&& other)
    {
        if(other.hasImageDecoder())
        {
            this->dec = other.dec;
            this->task = other.task;
            this->future = other.future;
            other.task = nullptr;
            other.dec = nullptr;
        }
        else
        {
            this->info = other.info;
        }
        
        return *this;
    }
    
    ~Entry()
    {
        if(task)
        {
            if(!DecoderFactory::globalInstance()->cancelDecodeTask(task))
            {
                future.waitForFinished();
            }
            future = QFuture<void>();
            task = nullptr;
        }
        dec = nullptr;
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
    
    void setTask(QSharedPointer<ImageDecodeTask> t)
    {
        this->task = t;
    }
    
    QSharedPointer<ImageDecodeTask>& getTask()
    {
        return task;
    }
    
    const QFuture<void>& getFuture()
    {
        return future;
    }
    
    void setFuture(QFuture<void>&& fut)
    {
        future = std::move(fut);
    }
    
private:
    QSharedPointer<SmartImageDecoder> dec;
    QSharedPointer<ImageDecodeTask> task;
    QFuture<void> future;
    QFileInfo info;
};

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::atomic<bool> directoryLoadingCancelled{ false };

    QFuture<void> directoryWorker;
    QMetaObject::Connection connectionNoMoreTasksLeft;
    
    QDir currentDir;
    std::vector<Entry> entries;
    
    // The column which is currently sorted
    Column currentSortedCol = Column::FileName;
    Qt::SortOrder sortOrder;
    
    int iconHeight = 150;

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

    static void throwIfDirectoryLoadingCancelled(void* self)
    {
        if (static_cast<Impl*>(self)->directoryLoadingCancelled)
        {
            throw UserCancellation();
        }
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

        return linfo.fileName() < rinfo.fileName();
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
    static bool topLevelSortFunction(const Entry& l, const Entry& r)
    {
        const QFileInfo& linfo = l.getFileInfo();
        const QFileInfo& rinfo = r.getFileInfo();

        bool leftIsBeforeRight =
            (linfo.isDir() && (!rinfo.isDir() || linfo.fileName() < rinfo.fileName())) ||
            (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(l, linfo, r, rinfo));
        
        return leftIsBeforeRight;
    }

    std::function<bool(const Entry&, const Entry&)> getSortFunction()
    {
        switch (currentSortedCol)
        {
        case Column::FileName:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FileName>(l, r); };
        
        case Column::FileSize:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FileSize>(l, r); };
            
        case Column::DateModified:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::DateModified>(l, r); };
            
        case Column::Resolution:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Resolution>(l, r); };

        case Column::DateRecorded:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::DateRecorded>(l, r); };

        case Column::Aperture:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Aperture>(l, r); };

        case Column::Exposure:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Exposure>(l, r); };

        case Column::Iso:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Iso>(l, r); };

        case Column::FocalLength:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::FocalLength>(l, r); };

        case Column::Lens:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::Lens>(l, r); };

        case Column::CameraModel:
            return [=](const Entry& l, const Entry& r) { return topLevelSortFunction<Column::CameraModel>(l, r); };

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
        while(itBegin != std::end(entries) && !itBegin->hasImageDecoder())
        {
            ++itBegin;
        }
        
        // find end to revert
        auto itEnd = std::end(entries) - 1;
        while(itEnd != std::begin(entries) && !itEnd->hasImageDecoder())
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
        q->beginInsertRows(QModelIndex(), 0, entries.size());
        q->endInsertRows();
    }
    
    // stop processing, delete everything and wait until finished
    void clear()
    {
        directoryLoadingCancelled = true;
        directoryWorker.waitForFinished();
        directoryLoadingCancelled = false;
        
        currentDir = QDir();
        entries.clear();
        entries.shrink_to_fit();
        
        if(connectionNoMoreTasksLeft)
        {
            q->disconnect(connectionNoMoreTasksLeft);
        }
    }

    void startImageDecoding(const QModelIndex& index, QSharedPointer<SmartImageDecoder> dec, DecodingState targetState)
    {
        Entry& e = entries.at(index.row());
        
        if(!e.getTask().isNull())
        {
            return;
        }
        
        auto task = DecoderFactory::globalInstance()->createDecodeTask(dec, targetState);
        e.setTask(task);
        e.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [=](){if(task) task->run();}));
    }
    
    void onDecodingTaskFinished(ImageDecodeTask* t)
    {
        auto result = std::find_if(entries.begin(),
                                   entries.end(),
                                [=](Entry& other)
                                { return other.getTask().data() == t;}
                                );
        if (result != entries.end())
        {
            result->setTask(nullptr);
        }
    }
    
    void setStatusMessage(int prog, QString msg)
    {
        emit q->directoryLoadingStatusMessage(prog, msg);
    }
    
    void onBackgroundImageTasksFinished()
    {
        emit q->layoutAboutToBeChanged();
        setStatusMessage(100, "All background tasks done");
        if(connectionNoMoreTasksLeft)
        {
            q->disconnect(connectionNoMoreTasksLeft);
        }
        emit q->layoutChanged();
    }
};

SortedImageModel::SortedImageModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Impl>(this))
{
    connect(this, &SortedImageModel::directoryLoaded, this, [&](){ d->onDirectoryLoaded(); });
}

SortedImageModel::~SortedImageModel() = default;

void SortedImageModel::changeDirAsync(const QDir& dir)
{
    d->setStatusMessage(0, "Waiting for previous directory parsing to finish...");
    
    this->beginResetModel();
    d->clear();
    d->connectionNoMoreTasksLeft = connect(DecoderFactory::globalInstance(), &DecoderFactory::noMoreTasksLeft, this, [&](){ d->onBackgroundImageTasksFinished(); });
    this->endResetModel();
    
    d->currentDir = dir;

    d->directoryWorker = QtConcurrent::run(QThreadPool::globalInstance(),
        [&]()
        {
            try
            {
                QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);

                const int entriesToProcess = fileInfoList.size();
                int entriesProcessed = 0;

                QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
                if (d->sortedColumnNeedsPreloadingMetadata())
                {
                    msg += " and reading EXIF data (making it quite slow)";
                }
                d->setStatusMessage(0, msg);
                d->entries.reserve(entriesToProcess);
                while (!fileInfoList.isEmpty())
                {
                    do
                    {
                        QFileInfo inf = fileInfoList.takeFirst();
                        if (inf.isFile())
                        {
                            auto decoder = DecoderFactory::globalInstance()->getDecoder(inf);
                            if (decoder)
                            {
                                if (d->sortedColumnNeedsPreloadingMetadata())
                                {
                                    decoder->decode(DecodingState::Metadata);
                                }

                                connect(decoder.data(), &SmartImageDecoder::decodingStateChanged, this, &SortedImageModel::onBackgroundImageTaskStateChanged);
                                d->entries.push_back(decoder);
                                break;
                            }
                        }

                        d->entries.emplace_back(std::move(inf));

                    } while (false);

                    d->throwIfDirectoryLoadingCancelled(d.get());
                    emit directoryLoadingProgress(entriesProcessed++ * 100. / entriesToProcess);
                }

                d->setStatusMessage(99, "Sorting entries");
                d->sortEntries();
                if(d->sortOrder == Qt::DescendingOrder)
                {
                    d->reverseEntries();
                }
                emit directoryLoaded();
            }
            catch (const UserCancellation&)
            {
                // intentionally not failed()
                emit directoryLoaded();
            }
            catch (const std::exception& e)
            {
                emit directoryLoadingFailed("Fatal error occurred while loading the directory", e.what());
            }
        });
}

QSharedPointer<SmartImageDecoder> SortedImageModel::goTo(const QString& currentUrl, int stepsFromCurrent, QModelIndex& idxOut)
{
    int step = (stepsFromCurrent < 0) ? -1 : 1;
    
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](Entry& other)
                            { return other.getFileInfo().absoluteFilePath() == currentUrl; });
    
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
        
        e = &d->entries[idx];
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
    if (d->directoryWorker.isFinished())
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
        QFileIconProvider iconProvider;
        const Entry* e = &d->entries.at(index.row());
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
                    d->startImageDecoding(index, e->getDecoder(), DecodingState::Metadata);
                    break;

                default:
                case DecodingState::Error:
                    return QIcon::fromTheme("dialog-error");

                case DecodingState::Metadata:
                case DecodingState::PreviewImage:
                case DecodingState::FullImage:
                {
                    QImage thumbnail = e->getDecoder()->thumbnail();
                    if (!thumbnail.isNull())
                    {
                        return thumbnail.scaledToHeight(iconHeight());
                    }
                }
                }
            }
            return iconProvider.icon(fileInfo).pixmap(iconHeight(),iconHeight()).scaledToHeight(iconHeight());

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
                    return e->getDecoder()->exif()->formatToString();
                    break;
    
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
    
    if(d->directoryWorker.isFinished())
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
                            [&](Entry& other)
                            { return other.getFileInfo() == info; });
    
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
        return d->entries.at(index.row()).getFileInfo();
    }
    
    return QFileInfo();
}

void SortedImageModel::onBackgroundImageTaskStateChanged(SmartImageDecoder* dec, quint32 newState, quint32)
{
    if(newState == DecodingState::Ready)
    {
        // ignore ready state
        return;
    }
    
    for(size_t i = 0; i < d->entries.size(); i++)
    {
        if(d->entries.at(i).getDecoder().data() == dec)
        {
            QModelIndex left = this->index(i, Column::FirstValid);
            QModelIndex right = this->index(i, Column::Count - 1);

            emit this->dataChanged(left, right);
            break;
        }
    }
}
