
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
    Entry(QFileInfo&& info) : info(std::move(info))
    {}
    
    Entry(std::unique_ptr<SmartImageDecoder>&& d) : dec(std::move(d))
    {}
    
    ~Entry()
    {
        if(task)
        {
            DecoderFactory::globalInstance()->cancelDecodeTask(task);
            future.waitForFinished();
            task = nullptr;
        }
    }
    
    QFileInfo getFileInfo() const
    {
        return this->hasImageDecoder() ? this->dec->fileInfo() : this->info;
    }

    std::shared_ptr<SmartImageDecoder> getDecoder() const
    {
        return this->hasImageDecoder() ? this->dec : nullptr;
    }
    
    bool hasImageDecoder() const
    {
        return dec != nullptr;
    }
    
    void setTask(std::shared_ptr<ImageDecodeTask> t)
    {
        this->task = std::move(t);
    }
    
    std::shared_ptr<ImageDecodeTask> getTask()
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
    std::shared_ptr<SmartImageDecoder> dec;
    std::shared_ptr<ImageDecodeTask> task;
    QFuture<void> future;
    QFileInfo info;
};

struct SortedImageModel::Impl
{
    SortedImageModel* q;
    
    std::atomic<bool> directoryLoadingCancelled{ false };

    QFuture<void> directoryWorker;

    QDir currentDir;
    std::vector<Entry> entries;
    
    // The column which is currently sorted
    Column currentSortedCol = Column::DateRecorded;
    Qt::SortOrder sortOrder;

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
    static bool sortColumnPredicateLeftBeforeRight(const Entry& l, QFileInfo& linfo, const Entry& r, QFileInfo& rinfo)
    {
        auto ldec = l.getDecoder();
        auto rdec = r.getDecoder();

        if (ldec && rdec)
        {
            if constexpr (SortCol == Column::Resolution)
            {
                QSize lsize = ldec->size();
                QSize rsize = rdec->size();

                if (lsize.width() != rsize.width() && lsize.height() != rsize.height())
                {
                    return lsize.width() * lsize.height() < rsize.width() * rsize.height();
                }
            }
            else if constexpr (SortCol == Column::DateRecorded)
            {
                QDateTime ltime = ldec->exif()->dateRecorded();
                QDateTime rtime = rdec->exif()->dateRecorded();
                
                if (ltime != rtime)
                {
                    return ltime < rtime;
                }

            }
            else if constexpr (SortCol == Column::Aperture)
            {
            }
            else if constexpr (SortCol == Column::Exposure)
            {
            }
            else if constexpr (SortCol == Column::Iso)
            {
            }
            else if constexpr (SortCol == Column::FocalLength)
            {
            }
            else if constexpr (SortCol == Column::Lens)
            {
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
        QFileInfo linfo = l.getFileInfo();
        QFileInfo rinfo = r.getFileInfo();

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
    }

    void startImageDecoding(const QModelIndex& index, std::shared_ptr<SmartImageDecoder> dec, DecodingState targetState)
    {
        Entry& e = entries.at(index.row());
        
        if(e.getFuture().isRunning())
        {
            return;
        }
        
        QObject::connect(dec.get(), &SmartImageDecoder::decodingStateChanged, q, &SortedImageModel::onBackgroundImageTaskStateChanged);
        
        auto task = DecoderFactory::globalInstance()->createDecodeTask(dec, targetState);
        e.setTask(task);
        e.setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [=](){if(task) task->run();}));
    }
    
    void setStatusMessage(int prog, QString msg)
    {
        emit q->directoryLoadingStatusMessage(prog, msg);
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
    this->endResetModel();
    
    d->currentDir = dir;

    d->setStatusMessage(0, "Loading Directory Entries");

    d->directoryWorker = QtConcurrent::run(QThreadPool::globalInstance(),
        [&]()
        {
            try
            {
                QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);

                const int entriesToProcess = fileInfoList.size();
                int entriesProcessed = 0;
                d->entries.reserve(entriesToProcess);
                while (!fileInfoList.isEmpty())
                {
                    do
                    {
                        QFileInfo inf = fileInfoList.takeFirst();
                        if (inf.isFile())
                        {
                            auto decoder = DecoderFactory::globalInstance()->getDecoder(inf.absoluteFilePath());
                            if (decoder)
                            {
                                if (d->sortedColumnNeedsPreloadingMetadata())
                                {
                                    decoder->decode(DecodingState::Metadata);
                                }

                                d->entries.emplace_back(Entry(std::move(decoder)));
                                break;
                            }
                        }

                        d->entries.emplace_back(Entry(std::move(inf)));

                    } while (false);

                    d->throwIfDirectoryLoadingCancelled(d.get());
                    emit directoryLoadingProgress(entriesProcessed++ * 100. / entriesToProcess);
                }

                d->setStatusMessage(99, "Sorting entries");
                d->sortEntries();
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

QFileInfo SortedImageModel::goNext(const QString& currentUrl)
{
    return this->goTo(currentUrl, 1);
}

QFileInfo SortedImageModel::goPrev(const QString& currentUrl)
{
    return this->goTo(currentUrl, -1);
}

QFileInfo SortedImageModel::goTo(const QString& currentUrl, int stepsFromCurrent)
{
    stepsFromCurrent = (d->sortOrder == Qt::DescendingOrder) ? -stepsFromCurrent : stepsFromCurrent;
    int step = (stepsFromCurrent < 0) ? -1 : 1;
    
    auto result = std::find_if(d->entries.begin(),
                               d->entries.end(),
                            [&](Entry& other)
                            { return other.getFileInfo().absoluteFilePath() == currentUrl; });
    
    if(result == d->entries.end())
    {
        qCritical() << "This should not happen: currentUrl not found.";
        return QFileInfo();
    }

    do
    {
        result += step;
        if(result == d->entries.end())
        {
            return QFileInfo();
        }
        
        if(result->hasImageDecoder() && result->getFileInfo().suffix() != "bak")
        {
            stepsFromCurrent -= step;
        }
        else
        {
            // skip unsupported files
        }
        
    } while(stepsFromCurrent);
    
    return result->getFileInfo();
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
                switch (e->getDecoder()->decodingState())
                {
                case DecodingState::Ready:
                    d->startImageDecoding(index, e->getDecoder(), DecodingState::Metadata);
                    break;

                default:
                case DecodingState::Error:
                    return QIcon::fromTheme("dialog-error");

                case DecodingState::Metadata:
                {
                    QImage thumbnail = e->getDecoder()->thumbnail();
                    if (!thumbnail.isNull())
                    {
                        return thumbnail;
                    }
                    else
                    {
                        d->startImageDecoding(index, e->getDecoder(), DecodingState::PreviewImage);
                        break;
                    }
                }
                case DecodingState::PreviewImage:
                case DecodingState::FullImage:
                {
                    QImage deepCopyImage = e->getDecoder()->image().scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    return deepCopyImage;
                }
                }
            }
            return iconProvider.icon(fileInfo);


        case Qt::ToolTipRole:
            if (e->hasImageDecoder())
            {
                switch (e->getDecoder()->decodingState())
                {
                case DecodingState::Error:
                    return QString("<b>%1</b><br><br>Latest Message was:<br>%2")
                    .arg(e->getDecoder()->errorMessage())
                    .arg(e->getDecoder()->latestMessage());
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
    d->sortOrder = order;
    this->sort(static_cast<Column>(column));
}

void SortedImageModel::sort(Column column)
{
    d->currentSortedCol = column;
    
    if(d->directoryWorker.isFinished())
    {
        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        d->sortEntries();
        QGuiApplication::restoreOverrideCursor();
    }
}

void SortedImageModel::sort(Qt::SortOrder order)
{
    if(d->directoryWorker.isFinished())
    {
        this->beginResetModel();
        d->sortOrder = order;
        this->endResetModel();
    }
    else
    {
        d->sortOrder = order;
    }
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
        if(d->entries.at(i).getDecoder().get() == dec)
        {
            QModelIndex left = this->index(i, Column::FirstValid);
            QModelIndex right = this->index(i, Column::Count - 1);
            
            // emit layout change to force view to update its flow layout
            emit this->layoutAboutToBeChanged();
            emit this->dataChanged(left, right);
            emit this->layoutChanged();
            break;
        }
    }
}
