
#include "OrderedFileSystemModel.hpp"

#include <QFuture>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QFileIconProvider>
#include <QStyle>
#include <QDir>
#include <algorithm>
// #include <execution>

#include "ImageDecodeTask.hpp"
#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"

enum Column : int
{
    FileName,
    FileSize,
    Resolution,
    DateRecorded,
    Aperture,
    Exposure,
    Iso,
    FocalLength,
    Lens,
    CameraModel,
    Count // must be last!
};

struct Entry
{
    Entry(QFileInfo&& info) : info(std::move(info))
    {}
    
    Entry(std::unique_ptr<SmartImageDecoder>&& d) : dec(std::move(d))
    {}
    
    QFileInfo getFileInfo() const
    {
        return this->hasImageDecoder() ? this->dec->fileInfo() : this->info;
    }

    SmartImageDecoder* getDecoder() const
    {
        return this->hasImageDecoder() ? this->dec.get() : nullptr;
    }
    
    bool hasImageDecoder() const
    {
        return dec != nullptr;
    }
    
private:
    std::unique_ptr<SmartImageDecoder> dec;
    QFileInfo info;
};

struct OrderedFileSystemModel::Impl
{
    OrderedFileSystemModel* q;
    
    std::atomic<bool> directoryLoadingCancelled{ false };

    QFuture<void> directoryWorker;

    QDir currentDir;
    std::vector<Entry> entries;
    
    // The column which is currently sorted
    Column currentSortedCol = Column::FileName;
    Qt::SortOrder sortOrder;

    Impl(OrderedFileSystemModel* parent) : q(parent)
    {}
    
    // returns true if the column that is sorted against requires us to preload the image metadata
    // before we insert the items into the model
    bool sortedColumnNeedsPreloadingMetadata()
    {
        if (currentSortedCol == Column::FileName || currentSortedCol == Column::FileSize)
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
        auto* ldec = l.getDecoder();
        auto* rdec = r.getDecoder();

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
            else
            {
//                 static_assert("Unknown column to sort for", !sizeof(char));
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
            (linfo.isDir() && (linfo.fileName() < rinfo.fileName())) ||
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
    
    void clear()
    {
        q->beginResetModel();

        currentDir = QDir();
        entries.clear();
        entries.shrink_to_fit();

        q->endResetModel();
    }

    void startImageDecoding(SmartImageDecoder* dec, DecodingState targetState)
    {

    }
    
    void setStatusMessage(QString msg)
    {
        emit q->directoryLoadingStatusMessage(msg);
    }
};

OrderedFileSystemModel::OrderedFileSystemModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Impl>(this))
{
    connect(this, &OrderedFileSystemModel::directoryLoaded, this, [&](){ d->onDirectoryLoaded(); });
}

OrderedFileSystemModel::~OrderedFileSystemModel() = default;

void OrderedFileSystemModel::changeDirAsync(const QDir& dir)
{
    d->directoryLoadingCancelled = true;
    d->setStatusMessage("Waiting for previous directory parsing to finish...");
    d->directoryWorker.waitForFinished();
    d->directoryLoadingCancelled = false;

    d->clear();
    d->currentDir = dir;

    d->setStatusMessage("Loading Directory Entries");

    d->directoryWorker = QtConcurrent::run(QThreadPool::globalInstance(),
        [&]()
        {
            try
            {
                QFileInfoList fileInfoList = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDot);

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
                                decoder->setCancellationCallback(&OrderedFileSystemModel::Impl::throwIfDirectoryLoadingCancelled, d.get());

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
                    emit directoryLoadingProgress(++entriesProcessed * 100. / entriesToProcess);
                }

                d->sortEntries();
                emit directoryLoaded();
            }
            catch (const UserCancellation&)
            {
                qInfo() << "Directory loading cancelled";
            }
            catch (const std::exception& e)
            {
                emit directoryLoadingFailed("Fatal error occurred while loading the directory.", e.what());
            }
        });
}



int OrderedFileSystemModel::columnCount(const QModelIndex &) const
{
    return Column::Count;
}

int OrderedFileSystemModel::rowCount(const QModelIndex&) const
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

QVariant OrderedFileSystemModel::data(const QModelIndex& index, int role) const
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
                    d->startImageDecoding(e->getDecoder(), DecodingState::Metadata);
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
                        d->startImageDecoding(e->getDecoder(), DecodingState::PreviewImage);
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
                    return e->getDecoder()->errorMessage();
                default:
                    break;
                }
            }
            return QVariant();

        case Qt::TextAlignmentRole:
            if (index.column() == Column::FileName)
            {
                const Qt::Alignment alignment = Qt::AlignRight | Qt::AlignVCenter;
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

bool OrderedFileSystemModel::insertRows(int row, int count, const QModelIndex& parent)
{
    return false;

    this->beginInsertRows(parent, row, row + count - 1);

    this->endInsertRows();
}

void OrderedFileSystemModel::sort(int column, Qt::SortOrder order)
{
    if (order == Qt::DescendingOrder)
    {
        qWarning() << "Descending sort order not supported yet";
    }

    d->currentSortedCol = static_cast<Column>(column);
    d->sortOrder = order;
    d->sortEntries();
}

QFileInfo OrderedFileSystemModel::fileInfo(const QModelIndex &index) const
{
    if(index.isValid())
    {
        return d->entries.at(index.row()).getFileInfo();
    }
    
    return QFileInfo();
}
