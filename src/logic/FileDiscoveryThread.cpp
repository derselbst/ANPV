
#include "FileDiscoveryThread.hpp"

#include "xThreadGuard.hpp"
#include "UserCancellation.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QScopedPointer>
#include <QEventLoop>
#include <QApplication>

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
        for (const QFileInfo& i : fileInfoList)
        {
            this->data->addImageItem(i);
        }
    }
    
    void throwIfDirectoryDiscoveryCancelled()
    {
        if(this->directoryDiscovery->isCanceled())
        {
            throw UserCancellation();
        }
    }

    void waitForDirectoryDiscovery() const
    {
        if (directoryDiscovery != nullptr && !directoryDiscovery->future().isFinished())
        {
            directoryDiscovery->future().waitForFinished();
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
    d->directoryDiscovery.reset(new QPromise<DecodingState>);
    return d->directoryDiscovery->future();
}

void FileDiscoveryThread::run()
{
    QScopedPointer<QFileSystemWatcher> watcher(new QFileSystemWatcher(this));
    connect(watcher.data(), &QFileSystemWatcher::directoryChanged, this, [&](const QString& p) { d->onDirectoryChanged(p); });
    
    d->evtLoop.reset(new QEventLoop(this));
    connect(QApplication::instance(), &QApplication::aboutToQuit, d->evtLoop.data(), &QEventLoop::quit);

    int entriesProcessed = 0;
    try
    {
        d->directoryDiscovery->start();
        d->directoryDiscovery->setProgressValueAndText(0, "Looking up directory");
        
        d->discoveredFiles = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        watcher->addPath(d->currentDir.absolutePath());
        
        const int entriesToProcess = d->discoveredFiles.size();
        if (entriesToProcess > 0)
        {
            d->directoryDiscovery->setProgressRange(0, entriesToProcess);

            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            d->directoryDiscovery->setProgressValueAndText(0, msg);

            // TODO startRendering
            //QMetaObject::invokeMethod(d->spinningIconHelper.get(), &ProgressIndicatorHelper::startRendering, Qt::QueuedConnection);
            unsigned readableImages = 0;
            for (auto it = d->discoveredFiles.begin(); it != d->discoveredFiles.end(); ++it)
            {
                if (d->data->addImageItem(*it))
                {
                    ++readableImages;
                }

                d->throwIfDirectoryDiscoveryCancelled();
                d->directoryDiscovery->setProgressValueAndText(entriesProcessed++, msg);
            }

            // increase by one, to make sure we meet the 100% below, which in turn ensures that the status message 'successfully loaded' is displayed in the UI
            d->directoryDiscovery->setProgressValueAndText(entriesProcessed++, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
        }
        else
        {
            d->directoryDiscovery->setProgressRange(0, 1);
            entriesProcessed++;
            if (d->currentDir.exists())
            {
                d->directoryDiscovery->setProgressValueAndText(entriesProcessed, "Directory is empty, nothing to see here.");
            }
            else
            {
                throw std::runtime_error("Directory does not exist");
            }
        }

        d->directoryDiscovery->addResult(DecodingState::FullImage);
    }
    catch (const UserCancellation&)
    {
        d->directoryDiscovery->addResult(DecodingState::Cancelled);
    }
    catch (const std::exception& e)
    {
        d->directoryDiscovery->setProgressValueAndText(entriesProcessed, QString("Exception occurred while loading the directory: %1").arg(e.what()));
        d->directoryDiscovery->addResult(DecodingState::Error);
    }
    catch (...)
    {
        d->directoryDiscovery->setProgressValueAndText(entriesProcessed, "Fatal error occurred while loading the directory");
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
        f << "FileDiscoveryThread terminated as it caught an exception while processing event loop:\n" << 
            "Error Type: " << typeid(e).name() << "\n"
            "Error Message: \n" << e.what();
        qCritical() << f.str().c_str();
    }
    watcher->removePath(d->currentDir.absolutePath());
    d->evtLoop.reset(nullptr);
}

