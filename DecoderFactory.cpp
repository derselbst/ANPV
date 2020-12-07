
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"
#include "ImageDecodeTask.hpp"

#include <QFile>
#include <QImageReader>
#include <QThreadPool>
#include <QDebug>
#include <vector>

struct DecoderFactory::Impl
{
    // a container were we store all tasks that need to be processed
    //
    // This is used when shutting down the application to cancel any pending tasks
    std::vector<QSharedPointer<ImageDecodeTask>> taskContainer;
     
    void onAboutToQuit()
    {
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
        auto result = std::find_if(taskContainer.begin(),
                                   taskContainer.end(),
                                [&](const QSharedPointer<ImageDecodeTask>& other)
                                { return other.data() == t;}
                                );
        if (result != taskContainer.end())
        {
            taskContainer.erase(result);
        }
        else
        {
            qWarning() << "ImageDecodeTask '" << t << "' not found in container.";
        }
    }
};

DecoderFactory::DecoderFactory() : QObject(nullptr), d(std::make_unique<Impl>())
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
    QImageReader r(url.absoluteFilePath());
    
    QSharedPointer<SmartImageDecoder> sid(nullptr, &QObject::deleteLater);
    if(r.format() == "tiff")
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
    d->taskContainer.emplace_back(new ImageDecodeTask(dec, targetState), &QObject::deleteLater);
    
    auto task = d->taskContainer.back();
    
    QObject::connect(task.data(), &ImageDecodeTask::finished,
                     [&](ImageDecodeTask* t){ d->onDecodingTaskFinished(t); });
    
    return task;
}

void DecoderFactory::cancelDecodeTask(QSharedPointer<ImageDecodeTask>& task)
{
    if(QThreadPool::globalInstance()->tryTake(task.data()))
    {
        // task not started yet, manually emit finished signal
        emit task->finished(task.data());
    }
    else
    {
        task->cancel();
    }
}
