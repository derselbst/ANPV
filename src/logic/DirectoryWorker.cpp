
#include "DirectoryWorker.hpp"

#include "xThreadGuard.hpp"
#include "UserCancellation.hpp"
#include "ImageSectionDataContainer.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QScopedPointer>
#include <QEventLoop>
#include <QApplication>

struct DirectoryWorker::Impl
{
    DirectoryWorker* q;
    ImageSectionDataContainer* data;
    QDir currentDir;
    QFileInfoList discoveredFiles;
    
    QScopedPointer<QPromise<DecodingState>> directoryDiscovery;
    QScopedPointer<QFileSystemWatcher> watcher;

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

    void cancelAndWaitForDirectoryDiscovery() const
    {
        if (directoryDiscovery != nullptr && !directoryDiscovery->future().isFinished())
        {
            directoryDiscovery->future().cancel();
            directoryDiscovery->future().waitForFinished();
        }
    }
};


/* Constructs the thread for loading the image thumbnails with size of the thumbnails (thumbsize) and
   the image list (data). */
DirectoryWorker::DirectoryWorker(ImageSectionDataContainer* data, QObject *parent)
   : d(std::make_unique<Impl>()), QObject(parent)
{
    d->q = this;
    d->data = data;
    d->watcher.reset(new QFileSystemWatcher(this));
    connect(d->watcher.data(), &QFileSystemWatcher::directoryChanged, this, [&](const QString& p) { d->onDirectoryChanged(p); });
    connect(this, &DirectoryWorker::discoverDirectory, this, &DirectoryWorker::onDiscoverDirectory, Qt::QueuedConnection);
}

DirectoryWorker::~DirectoryWorker()
{
    d->cancelAndWaitForDirectoryDiscovery();
    d->data = nullptr;
}

QFuture<DecodingState> DirectoryWorker::changeDirAsync(const QString& dir)
{
    d->cancelAndWaitForDirectoryDiscovery();
    d->directoryDiscovery.reset(new QPromise<DecodingState>);
    emit this->discoverDirectory(dir);
    return d->directoryDiscovery->future();
}

void DirectoryWorker::onDiscoverDirectory(QString newDir)
{
    d->watcher->removePath(d->currentDir.absolutePath());
    d->currentDir = QDir(newDir);
    int entriesProcessed = 0;
    try
    {
        d->directoryDiscovery->start();
        d->directoryDiscovery->setProgressValueAndText(0, "Clearing Model");

        d->data->clear();
        d->directoryDiscovery->setProgressValueAndText(0, "Looking up directory");

        d->discoveredFiles = d->currentDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        d->watcher->addPath(d->currentDir.absolutePath());

        const int entriesToProcess = d->discoveredFiles.size();
        if (entriesToProcess > 0)
        {
            d->directoryDiscovery->setProgressRange(0, entriesToProcess);

            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            d->directoryDiscovery->setProgressValueAndText(0, msg);

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
}
