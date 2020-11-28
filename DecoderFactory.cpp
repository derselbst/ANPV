
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
    std::vector<std::shared_ptr<ImageDecodeTask>> taskContainer;
     
    void onAboutToQuit()
    {
        for(size_t i=0; i < taskContainer.size(); i++)
        {
            // disconnect all signals
            taskContainer[i]->disconnect();
            taskContainer[i]->shutdown();
        }
        
        if(!QThreadPool::globalInstance()->waitForDone(5000))
        {
            qWarning() << "Waited over 5 seconds for the thread pool to finish, giving up. I will probably crash now...";
        }
        
        taskContainer.clear();
    }
    
    void onDecodingTaskFinished(ImageDecodeTask* t)
    {
        auto result = std::find_if(taskContainer.begin(),
                                   taskContainer.end(),
                                [&](std::shared_ptr<ImageDecodeTask>& other)
                                { return other.get() == t;}
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

std::unique_ptr<SmartImageDecoder> DecoderFactory::getDecoder(QString url)
{
    QImageReader r(url);
    
    std::unique_ptr<SmartImageDecoder> sid;
    if(r.format() == "tiff")
    {
        sid = std::make_unique<SmartTiffDecoder>(std::move(url));
    }
    else if(r.format() == "jpeg")
    {
        sid = std::make_unique<SmartJpegDecoder>(std::move(url));
    }
    
    return std::move(sid);
}

void DecoderFactory::configureDecoder(SmartImageDecoder* dec, DocumentView* dc)
{
    QObject::connect(dec, &SmartImageDecoder::decodingStateChanged, dc, &DocumentView::onDecodingStateChanged);
    QObject::connect(dec, &SmartImageDecoder::decodingProgress, dc, &DocumentView::onDecodingProgress);
    QObject::connect(dec, &SmartImageDecoder::imageRefined, dc, &DocumentView::onImageRefinement);
}

std::shared_ptr<ImageDecodeTask> DecoderFactory::createDecodeTask(std::shared_ptr<SmartImageDecoder> dec, DecodingState targetState)
{
    d->taskContainer.emplace_back(std::make_shared<ImageDecodeTask>(dec, targetState));
    
    auto task = d->taskContainer.back();
    
    QObject::connect(task.get(), &ImageDecodeTask::finished,
                     this, [&](ImageDecodeTask* t){ d->onDecodingTaskFinished(t); });
    
    return task;
}

void DecoderFactory::cancelDecodeTask(std::shared_ptr<ImageDecodeTask> task)
{
    if(QThreadPool::globalInstance()->tryTake(task.get()))
    {
        
    }
    else
    {
        task->cancel();
    }
}
