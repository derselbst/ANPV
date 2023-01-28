
#include "FileDiscoveryThread.hpp"

#include <xThreadGuard.hpp>

#include <QDir>
#include <QFileSystemWatcher>
#include <QScopedPointer>

struct FileDiscoveryThread::Impl
{
    FileDiscoveryThread* q;
    ImageSectionDataContainer* data;
    QDir currentDir;
    QFileInfoList discoveredFiles;
    
    QScopedPointer<QPromise<DecodingState>> directoryDiscovery;
    QScopedPointer<QEventLoop> evtLoop;

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

        // any file still in the list are new, we need to add them
        for (QFileInfo i : fileInfoList)
        {
            addImageItem(i, nullptr);
        }
    }
    
    void throwIfDirectoryDiscoveryCancelled()
    {
        if(this->directoryDiscovery->isCanceled())
        {
            throw UserCancellation();
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

QFuture<DecodingState> FileDiscoveryThread::changeDirAsync(const QString& dir)
{
    xThreadGuard g(this);

    if(d->evtLoop)
    {
        d->evtLoop->quit();
    }
    d->currentDir = QDir(dir);
    d->directoryWorker = new QPromise<DecodingState>;
    return d->directoryDiscovery->future();
}

void FileDiscoveryThread::run()
{
    QScopedPointer<QFileSystemWatcher> watcher = new QFileSystemWatcher(this);
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, [&](const QString& p) { d->onDirectoryChanged(p); });
    
    d->evtLoop = new QEventLoop(this);
    connect(QApplication::instance(), &QApplication::aboutToQuit, &d->evtLoop, &QEventLoop::quit);

    int entriesProcessed = 0;
    try
    {
        d->directoryDiscovery->start();

        d->setStatusMessage(0, "Looking up directory");
        
        d->discoveredFiles = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        d->watcher->addPath(d->currentDir.absolutePath());
        
        const int entriesToProcess = d->discoveredFiles.size();
        if (entriesToProcess > 0)
        {
            d->directoryDiscovery->setProgressRange(0, entriesToProcess + 2 /* for sorting effort + starting the decoding */);

            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            if (d->sortedColumnNeedsPreloadingMetadata(d->currentSortedCol))
            {
                msg += " and reading EXIF data (making it quite slow)";
            }

            d->setStatusMessage(0, msg);
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
                    if (d->data->addImageItem(inf, &decodersToRunLater))
                    {
                        ++readableImages;
                    }
                } while (false);

                d->throwIfDirectoryDiscoveryCancelled();
                d->setStatusMessage(entriesProcessed++, msg);
            }

            d->setStatusMessage(entriesProcessed++, "Sorting entries, please wait...");
            d->sortEntries();
            if (d->sortOrder == Qt::DescendingOrder)
            {
                d->reverseEntries();
            }

            d->throwIfDirectoryDiscoveryCancelled();
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
            d->directoryDiscovery->setProgressRange(0, 1);
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

        d->directoryDiscovery->addResult(DecodingState::FullImage);
        d->watcher->addPath(d->currentDir.absolutePath());
    }
    catch (const UserCancellation&)
    {
        d->directoryDiscovery->addResult(DecodingState::Cancelled);
    }
    catch (const std::exception& e)
    {
        d->setStatusMessage(entriesProcessed, QString("Exception occurred while loading the directory: %1").arg(e.what()));
        d->directoryDiscovery->addResult(DecodingState::Error);
    }
    catch (...)
    {
        d->setStatusMessage(entriesProcessed, "Fatal error occurred while loading the directory");
        d->directoryDiscovery->addResult(DecodingState::Error);
    }
    d->directoryDiscovery->finish();
    
    try
    {
        d->evtLoop->exec();
    }
    catch(const std::exception& e)
    {
        Formatter f;
        f << << "FileDiscoveryThread terminated as it caught an exception while processing event loop:\n" << 
            "Error Type: " << typeid(e).name() << "\n"
            "Error Message: \n" << e.what();
        qCritical() << f.str().c_str();
    }
    d->watcher->removePath(d->currentDir.absolutePath());
    d->evtLoop = nullptr;
}

