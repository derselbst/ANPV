
#include "FileDiscoveryThread.hpp"

#include <xThreadGuard.hpp>

#include <QDir>
#include <QFileSystemWatcher>

struct FileDiscoveryThread::Impl
{
    FileDiscoveryThread* q;
    ImageSectionDataContainer* data;
    QDir currentDir;
    QFileInfoList discoveredFiles;
    QPointer<QFileSystemWatcher> watcher;

    void onDirectoryChanged(const QString& path)
    {
        xThreadGuard g(q);
        if (path != this->currentDir.absolutePath())
        {
            return;
        }

        QFileInfoList fileInfoList = currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);

        for (auto it = discoveredFiles.begin(); it != discoveredFiles.end();)
        {
            const QFileInfo& eInfo = (*it);
            if (!eInfo.exists())
            {
                // file doesn't exist, probably deleted
                
                // TODO: this is ugly, think of a better way using events / connections
                /*if (img->checked() != Qt::Unchecked)
                {
                    this->numberOfCheckedImages--;
                }*/
                this->data->removeImageItem(eInfo);
                it = discoveredFiles.erase(it);
                continue;
            }

            auto result = std::find_if(fileInfoList.begin(),
                fileInfoList.end(),
                [&](const QFileInfo& other)
                { return eInfo == other; });

            if (result != fileInfoList.end())
            {
                // we already know about that file, remove it from the current list
                fileInfoList.erase(result);
            }
            else
            {
                // shouldn't happen??
            }
            ++it;
        }

        if (!fileInfoList.isEmpty())
        {
            int iconHeight = cachedIconHeight;
            QSize iconSize(iconHeight, iconHeight);

            // any file still in the list are new, we need to add them
            for (QFileInfo i : fileInfoList)
            {
                addSingleFile(std::move(i), iconSize, nullptr);
            }
        }
    }
};


/* Constructs the thread for loading the image thumbnails with size of the thumbnails (thumbsize) and
   the image list (data). */
FileDiscoveryThread::FileDiscoveryThread(ImageSectionDataContainer* data, QObject *parent)
   : d(std::make_unique<Impl>()), QThread(parent)
{
    d->q = this;
    d->data = data;
}

FileDiscoveryThread::~FileDiscoveryThread() = default;

void FileDiscoveryThread::run()
{
    d->discoveredFiles = d->currentDir.entryInfoList();



    d->watcher = new QFileSystemWatcher(this);
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, [&](const QString& p) { d->onDirectoryChanged(p); });
}


QFuture<DecodingState> FileDiscoveryThread::changeDirAsync(const QString& dir)
{
    xThreadGuard g(this);

    d->watcher->removePath(d->currentDir.absolutePath());
    d->currentDir = QDir(dir);
    return d->directoryWorker->future();
}

void FileDiscoveryThread::run()
{
    int entriesProcessed = 0;
    try
    {
        d->directoryWorker->start();

        d->setStatusMessage(0, "Looking up directory");
        d->discoveredFiles = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const int entriesToProcess = d->discoveredFiles.size();
        if (entriesToProcess > 0)
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
            for (auto it = d->discoveredFiles.begin(); it != d->discoveredFiles.end(); ++it)
            {
                do
                {
                    QFileInfo inf = fileInfoList.takeFirst();
                    if (d->addSingleFile(std::move(inf), iconSize, &decodersToRunLater))
                    {
                        ++readableImages;
                    }
                } while (false);

                d->throwIfDirectoryLoadingCancelled();
                d->setStatusMessage(entriesProcessed++, msg);
            }

            // TODO: optimize away explicit sorting, by using heap insert in first place
            d->setStatusMessage(entriesProcessed++, "Sorting entries, please wait...");
            d->sortEntries();
            if (d->sortOrder == Qt::DescendingOrder)
            {
                d->reverseEntries();
            }

            d->throwIfDirectoryLoadingCancelled();
            if (!decodersToRunLater.empty())
            {
                d->setStatusMessage(entriesProcessed++, "Directory read, starting async decoding tasks in the background.");

                std::lock_guard<std::recursive_mutex> l(d->m);
                for (auto& decoder : decodersToRunLater)
                {
                    QSharedPointer<QFutureWatcher<DecodingState>> watcher(new QFutureWatcher<DecodingState>());
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::finished, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::canceled, this, [=]() { d->onBackgroundTaskFinished(watcher, decoder); });
                    connect(watcher.get(), &QFutureWatcher<DecodingState>::started, this, [=]() { d->onBackgroundTaskStarted(watcher, decoder); });
                    watcher->moveToThread(QGuiApplication::instance()->thread());

                    // decode asynchronously
                    auto fut = decoder->decodeAsync(DecodingState::Metadata, Priority::Background, iconSize);
                    watcher->setFuture(fut);

                    d->backgroundTasks[decoder] = (watcher);
                }
                QMetaObject::invokeMethod(d->spinningIconHelper.get(), &ProgressIndicatorHelper::startRendering, Qt::QueuedConnection);
            }
            else
            {
                // increase by one, to make sure we meet the 100% below, which in turn ensures that the status message 'successfully loaded' is displayed in the UI
                entriesProcessed++;
            }

            d->setStatusMessage(entriesProcessed, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
            QMetaObject::invokeMethod(this, [&]() { d->onDirectoryLoaded(); }, Qt::QueuedConnection);
        }
        else
        {
            d->directoryWorker->setProgressRange(0, 1);
            entriesProcessed++;
            if (d->currentDir.exists())
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
