
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"
#include "ImageDecodeTask.hpp"

#include <KDCRAW/KDcraw>
#include <QFile>
#include <QImageReader>
#include <QThreadPool>
#include <QDebug>
#include <vector>
#include <mutex>

struct DecoderFactory::Impl
{
    DecoderFactory* q;
    
    // a container were we store all tasks that need to be processed
    //
    // This is used when shutting down the application to cancel any pending tasks
    std::vector<QSharedPointer<ImageDecodeTask>> taskContainer;
    
    std::mutex m;
    
    Impl(DecoderFactory* q) : q(q)
    {}
    
    ~Impl()
    {
        this->onAboutToQuit();
    }
    
    void onAboutToQuit()
    {
        std::lock_guard<std::mutex> l(this->m);
        
        for(size_t i=0; i < taskContainer.size(); i++)
        {
            (void)QThreadPool::globalInstance()->tryTake(taskContainer[i].data());
            // disconnect all signals
            taskContainer[i]->disconnect();
            taskContainer[i]->shutdown();
        }
        
        taskContainer.clear();
    }
    
    void onDecodingTaskFinished(ImageDecodeTask* t)
    {
        std::unique_lock<std::mutex> l(this->m);
        
        auto result = std::find_if(taskContainer.begin(),
                                   taskContainer.end(),
                                [&](const QSharedPointer<ImageDecodeTask>& other)
                                { return other.data() == t;}
                                );
        if (result != taskContainer.end())
        {
            taskContainer.erase(result);

            if(taskContainer.empty())
            {
                l.unlock();
                emit q->noMoreTasksLeft();
            }
        }
        else
        {
            qWarning() << "ImageDecodeTask '" << t << "' not found in container.";
        }
    }
};

DecoderFactory::DecoderFactory() : QObject(nullptr), d(std::make_unique<Impl>(this))
{
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [&](){ d->onAboutToQuit(); });
}

DecoderFactory::~DecoderFactory() = default;

DecoderFactory* DecoderFactory::globalInstance()
{
    static DecoderFactory fac;
    return &fac;
}

QSharedPointer<SmartImageDecoder> DecoderFactory::getDecoder(const QFileInfo& url)
{
    const QByteArray formatHint = url.fileName().section(QLatin1Char('.'), -1).toLocal8Bit().toLower();
    
    QSharedPointer<SmartImageDecoder> sid(nullptr, &QObject::deleteLater);
    QImageReader r(url.absoluteFilePath());
    
    if(KDcrawIface::KDcraw::rawFilesList().contains(QString::fromLatin1(formatHint)))
    {
        QByteArray previewData;

        // use KDcraw for getting the embedded preview
        bool ret = KDcrawIface::KDcraw::loadEmbeddedPreview(previewData, url.absoluteFilePath());

        if (!ret)
        {
            // if the embedded preview loading failed, load half preview instead.
            // That's slower but it works even for images containing
            // small (160x120px) or none embedded preview.
            if (!KDcrawIface::KDcraw::loadHalfPreview(previewData, url.absoluteFilePath()))
            {
                qWarning() << "unable to get half preview for " << url.absoluteFilePath();
                return nullptr;
            }
        }
        
        sid.reset(new SmartJpegDecoder(url, previewData));
    }
    else if(r.format() == "tiff")
    {
        sid.reset(new SmartTiffDecoder(url));
    }
    else if(r.format() == "jpeg")
    {
        sid.reset(new SmartJpegDecoder(url));
    }
    
    return sid;
}

void DecoderFactory::configureDecoder(SmartImageDecoder* dec, DocumentView* dc)
{
    QObject::connect(dec, &SmartImageDecoder::decodingStateChanged, dc, &DocumentView::onDecodingStateChanged);
    QObject::connect(dec, &SmartImageDecoder::decodingProgress, dc, &DocumentView::onDecodingProgress);
    QObject::connect(dec, &SmartImageDecoder::imageRefined, dc, &DocumentView::onImageRefinement);
}

QSharedPointer<ImageDecodeTask> DecoderFactory::createDecodeTask(QSharedPointer<SmartImageDecoder> dec, DecodingState targetState)
{
    QSharedPointer<ImageDecodeTask> task;
    
    {
        std::lock_guard<std::mutex> l(d->m);
        
        d->taskContainer.emplace_back(new ImageDecodeTask(dec, targetState), &QObject::deleteLater);
        task = d->taskContainer.back();
    }
    
    QObject::connect(task.data(), &ImageDecodeTask::finished,
                     this, [&](ImageDecodeTask* t){ d->onDecodingTaskFinished(t); },
                     Qt::DirectConnection // directly call the slot, we have a mutex for sync
                    );
    
    return task;
}

bool DecoderFactory::cancelDecodeTask(QSharedPointer<ImageDecodeTask>& task)
{
    // cancel the task in any case, because QFuture::waitForFinished might start to running tasks
    // if they haven't been started yet...
    task->cancel();
    
    bool taken = QThreadPool::globalInstance()->tryTake(task.data());
    if(taken)
    {
        // task not started yet, manually emit finished signal
        emit task->finished(task.data());
    }
    
    return taken;
}
