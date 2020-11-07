
#include "ImageDecodeTask.hpp"
#include "UserCancellation.hpp"
#include "SmartImageDecoder.hpp"
#include "DecodingState.hpp"

#include <atomic>
#include <QDebug>


struct ImageDecodeTask::Impl
{
    std::shared_ptr<SmartImageDecoder> decoder;
    std::atomic<bool> isCancelled{false};
    
    Impl(std::shared_ptr<SmartImageDecoder> dec) : decoder(dec)
    {}
    
    static void throwIfCancelled(void* self)
    {
        if(static_cast<Impl*>(self)->isCancelled)
        {
            throw UserCancellation();
        }
    }
};

ImageDecodeTask::ImageDecodeTask(std::shared_ptr<SmartImageDecoder> dec) : d(std::make_unique<Impl>(dec))
{
    this->setAutoDelete(false);
    d->decoder->setCancellationCallback(&ImageDecodeTask::Impl::throwIfCancelled, d.get());
}

ImageDecodeTask::~ImageDecodeTask() = default;

void ImageDecodeTask::run()
{
    try
    {
        d->decoder->decode(DecodingState::FullImage);
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




