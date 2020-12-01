
#include "ImageDecodeTask.hpp"
#include "UserCancellation.hpp"
#include "SmartImageDecoder.hpp"

#include <atomic>
#include <QDebug>


struct ImageDecodeTask::Impl
{
    QSharedPointer<SmartImageDecoder> decoder;
    
    DecodingState targetState;
    
    std::atomic<bool> isCancelled{false};
    
    Impl(QSharedPointer<SmartImageDecoder>&& dec, DecodingState t) : decoder(std::move(dec)), targetState(t)
    {}
    
    static void throwIfCancelled(void* self)
    {
        if(static_cast<Impl*>(self)->isCancelled)
        {
            throw UserCancellation();
        }
    }
};

ImageDecodeTask::ImageDecodeTask(QSharedPointer<SmartImageDecoder>&& dec, DecodingState targetState) : d(std::make_unique<Impl>(std::move(dec), targetState))
{
    this->setAutoDelete(false);
    d->decoder->setCancellationCallback(&ImageDecodeTask::Impl::throwIfCancelled, d.get());
}

ImageDecodeTask::~ImageDecodeTask() = default;

void ImageDecodeTask::run()
{
    try
    {
        d->decoder->decode(d->targetState);
    }
    catch(...)
    {
        qCritical() << "Exception caught in ImageDecodeTask::run()";
    }
    
    emit finished(this);
}

void ImageDecodeTask::cancel() noexcept
{
    d->isCancelled = true;
}

// should only be called from main thread!
void ImageDecodeTask::shutdown() noexcept
{
    d->decoder->disconnect();
    this->cancel();
}
