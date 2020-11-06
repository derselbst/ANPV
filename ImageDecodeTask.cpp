
#include "ImageDecodeTask.hpp"
#include "UserCancellation.hpp"
#include "SmartImageDecoder.hpp"
#include "DecodingState.hpp"

#include <atomic>


struct ImageDecodeTask::Impl
{
    SmartImageDecoder* decoder;
    std::atomic<bool> isCancelled{false};
    
    Impl(SmartImageDecoder* dec) : decoder(dec)
    {}
    
    static void throwIfCancelled(void* self)
    {
        if(static_cast<Impl*>(self)->isCancelled)
        {
            throw UserCancellation();
        }
    }
};

ImageDecodeTask::ImageDecodeTask(SmartImageDecoder* dec) : d(std::make_unique<Impl>(dec))
{
    this->setAutoDelete(false);
    d->decoder->setCancellationCallback(&ImageDecodeTask::Impl::throwIfCancelled, d.get());
}

ImageDecodeTask::~ImageDecodeTask() = default;

void ImageDecodeTask::run()
{
    d->decoder->decode(DecodingState::FullImage);
}

void ImageDecodeTask::cancel() noexcept
{
    d->isCancelled = true;
}




