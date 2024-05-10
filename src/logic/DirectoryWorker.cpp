
#include "DirectoryWorker.hpp"

#include "xThreadGuard.hpp"
#include "UserCancellation.hpp"
#include "ImageSectionDataContainer.hpp"
#include "LibRawHelper.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QScopedPointer>
#include <QEventLoop>
#include <QApplication>
#include <QPromise>
#include <QTimer>

#include <QStringView>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QUtf8StringView>
#endif

#include <filesystem>
#include <unordered_map>


uint qHash(const QFileInfo& inf)
{
    return qHash(inf.fileName());
}

struct DirectoryWorker::Impl
{
    DirectoryWorker *q;
    ImageSectionDataContainer *data;
    QDir currentDir;
    QSet<QFileInfo> delayedQueue;
    QTimer delayedProcessing;

    // This list contains the fileInfos of all files in the model. We do not loop over the model itself to avoid aquiring the lock.
    QFileInfoList discoveredFiles;

    QScopedPointer<QPromise<DecodingState>> directoryDiscovery;
    QScopedPointer<QFileSystemWatcher> watcher;

    using FileMap = std::unordered_map<std::filesystem::path::string_type /* filename without extension */, std::vector<std::filesystem::path::string_type> /* extension(s) */>;

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
            QFileInfo& eInfo = (*it);

            // due to caching, call stat() as exists might otherwise return outdated garbage
            eInfo.stat();

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
                {
                    return eInfo == other;
                });

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

        while (!fileInfoList.empty())
        {
            this->delayedQueue.insert(fileInfoList.takeFirst());
        }
        QMetaObject::invokeMethod(&this->delayedProcessing, QOverload<>::of(&QTimer::start));
    }

    void onDelayedProcessing()
    {
        // any file still in the list are (probably) new, we need to add them
        for(QFileInfo i : this->delayedQueue)
        {
            i.stat();
            if (!i.exists())
            {
                // file doesn't exist, probably deleted
                this->data->removeImageItem(i);
            }
            else
            {
                this->discoveredFiles.push_back(i);
                this->data->addImageItem(i);
            }
        }
        this->delayedQueue.clear();
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
        if(directoryDiscovery != nullptr && !directoryDiscovery->future().isFinished())
        {
            directoryDiscovery->future().cancel();
            directoryDiscovery->future().waitForFinished();
        }
    }

    FileMap readDirectoryEntries()
    {
        if (!this->currentDir.isReadable())
        {
            throw std::runtime_error("Cannot read directory!");
        }

        auto dirBegin = std::filesystem::directory_iterator(this->currentDir.filesystemAbsolutePath());
        auto dirSize = std::distance(dirBegin, std::filesystem::directory_iterator{});

        FileMap fileMap;
        fileMap.reserve(dirSize);

        QElapsedTimer t;
        t.start();

        // dirBegin will become invalidated somehow by std::distance :(
        dirBegin = std::filesystem::directory_iterator(this->currentDir.filesystemAbsolutePath());
        for (const auto& file : dirBegin)
        {
            auto& path = file.path();
            
            // keep track of the discovered files in a flat list with Qt data types (could be optimized away, by using the FileMap consistently)
            discoveredFiles.push_back(QFileInfo(path));

            // create a map which allows us to more easily match RAWs and JPEGs
            FileMap::key_type filename = path.filename();
            auto dotPos = filename.find_last_of('.');
            FileMap::key_type filenameWithoutExt = filename.substr(0, dotPos);
            auto& ext = fileMap[std::move(filenameWithoutExt)];
            
            if(dotPos != filename.npos)
            {
                FileMap::key_type extension;
                try
                {
                    extension = filename.substr(dotPos + 1);
                }
                catch(const std::out_of_range& e)
                {
                }
                
                ext.push_back(std::move(extension));
            }
            
            if (t.elapsed() > 100)
            {
                this->throwIfDirectoryDiscoveryCancelled();
                t.restart();
            }
        }

        return fileMap;
    }

};


/* Constructs the thread for loading the image thumbnails with size of the thumbnails (thumbsize) and
   the image list (data). */
DirectoryWorker::DirectoryWorker(ImageSectionDataContainer *data, QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->data = data;
    d->watcher.reset(new QFileSystemWatcher(this));
    connect(d->watcher.data(), &QFileSystemWatcher::directoryChanged, this, [&](const QString & p)
    {
        d->onDirectoryChanged(p);
    });
    connect(this, &DirectoryWorker::discoverDirectory, this, &DirectoryWorker::onDiscoverDirectory, Qt::QueuedConnection);

    d->delayedProcessing.setSingleShot(true);
    d->delayedProcessing.setInterval(1000);
    connect(&d->delayedProcessing, &QTimer::timeout, this, [&]()
    {
        d->onDelayedProcessing();
    });
}

DirectoryWorker::~DirectoryWorker()
{
    d->cancelAndWaitForDirectoryDiscovery();
    d->data = nullptr;
}

QFuture<DecodingState> DirectoryWorker::changeDirAsync(const QString &dir)
{
    d->cancelAndWaitForDirectoryDiscovery();
    d->directoryDiscovery.reset(new QPromise<DecodingState>);
    d->discoveredFiles.clear();
    d->delayedProcessing.stop();
    d->delayedQueue.clear();
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

        auto fileMap = d->readDirectoryEntries();
        d->watcher->addPath(d->currentDir.absolutePath());

        const int entriesToProcess = d->discoveredFiles.size();

        if(entriesToProcess > 0)
        {
            d->directoryDiscovery->setProgressRange(0, entriesToProcess+1);

            QString msg = QString("Loading %1 directory entries").arg(entriesToProcess);
            d->directoryDiscovery->setProgressValueAndText(0, msg);

            unsigned readableImages = 0;
            QFileInfoList similarFiles;

            for(auto it = fileMap.begin(); it != fileMap.end(); ++it)
            {
                auto& val = *it;
                auto& filename = val.first;
                auto& ext = val.second;

                auto encodingAwareQStringAppender = []<typename T>(const T& str, QString& dest)
                {
                    // constexpr if will only discard the false branch in a template function:
                    // https://en.cppreference.com/w/cpp/language/if
                    if constexpr(std::is_same_v<T, std::wstring>)
                    {
                        dest.append(QStringView(str));
                    }
                    else
                    {
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
                        // Will implicitly construct a QString from UTF8 and then append it
                        dest.append(str.c_str());
#else
                        // Constructs the StringView and appends it directly
                        dest.append(QUtf8StringView(str));
#endif
                    }
                };

                QString fname;
                encodingAwareQStringAppender(filename, fname);
                if(ext.size() != 0)
                {
                    for (auto& e : ext)
                    {
                        QString f(fname);
                        f.append('.');
                        encodingAwareQStringAppender(e, f);
                        similarFiles.push_back(QFileInfo(d->currentDir, f));
                    }
                }
                else
                {
                    similarFiles.push_back(QFileInfo(d->currentDir, fname));
                }

                readableImages += d->data->addImageItem(similarFiles);
                entriesProcessed += similarFiles.size();

                similarFiles.clear();

                d->throwIfDirectoryDiscoveryCancelled();
                d->directoryDiscovery->setProgressValueAndText(entriesProcessed, msg);
            }

            // increase by one, to make sure we meet the 100% below, which in turn ensures that the status message 'successfully loaded' is displayed in the UI
            d->directoryDiscovery->setProgressValueAndText(++entriesProcessed, QString("Directory successfully loaded; discovered %1 readable images of a total of %2 entries").arg(readableImages).arg(entriesToProcess));
        }
        else
        {
            d->directoryDiscovery->setProgressRange(0, 1);

            if(d->currentDir.exists())
            {
                d->directoryDiscovery->setProgressValueAndText(++entriesProcessed, "Directory is empty, nothing to see here.");
            }
            else
            {
                throw std::runtime_error("Directory does not exist");
            }
        }

        d->directoryDiscovery->addResult(DecodingState::FullImage);
    }
    catch(const UserCancellation &)
    {
        d->directoryDiscovery->addResult(DecodingState::Cancelled);
    }
    catch(const std::exception &e)
    {
        d->directoryDiscovery->setProgressValueAndText(entriesProcessed, QString("Exception occurred while loading the directory: %1").arg(e.what()));
        d->directoryDiscovery->addResult(DecodingState::Error);
    }
    catch(...)
    {
        d->directoryDiscovery->setProgressValueAndText(entriesProcessed, "Fatal error occurred while loading the directory");
        d->directoryDiscovery->addResult(DecodingState::Error);
    }

    d->directoryDiscovery->finish();
}
