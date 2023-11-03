
#include "SmartJxlDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <QDebug>
#include <QtGlobal>
#include <QColorSpace>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner_cxx.h>


struct SmartJxlDecoder::Impl
{
    static const inline JxlPixelFormat jxlFormat = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    static inline auto parallelRunner = JxlThreadParallelRunnerMake(nullptr, std::thread::hardware_concurrency());

    SmartJxlDecoder* q;
    
    JxlDecoderPtr djxl;
    JxlBasicInfo jxlInfo;
    
    const unsigned char* buffer = nullptr;
    qint64 nbytes = 0;

    unsigned char* imgBuf = nullptr;
    
    Impl(SmartJxlDecoder* q) : q(q)
    {
        this->djxl = JxlDecoderMake(nullptr);
        auto ret = JxlDecoderSetParallelRunner(this->djxl.get(), JxlThreadParallelRunner, this->parallelRunner.get());
        if (JXL_DEC_SUCCESS != ret)
        {
            qWarning() << "JxlDecoderSetParallelRunner() failed, using single threaded decoder";
        }
    }
        
    QImage::Format format()
    {
        // The zero initialized, not-yet-decoded image buffer should be displayed transparently. Therefore, always use ARGB, even if this
        // would cause a performance drawback for images which do not have one, because Qt may call QPixmap::mask() internally.
        return QImage::Format_RGBA8888;
    }

    static void decoderCallback(void* opaque, size_t x, size_t y, size_t num_pixels, const void* pixels)
    {
        auto* self = static_cast<SmartJxlDecoder::Impl*>(opaque);

        std::memcpy(&self->imgBuf[(y * self->jxlInfo.xsize + x) * self->jxlFormat.num_channels], pixels, self->jxlFormat.num_channels * num_pixels);

        self->q->updateDecodedRoiRect(QRect(x,y,num_pixels,1));
        self->q->cancelCallback();
    }
};

SmartJxlDecoder::SmartJxlDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>(this))
{}

SmartJxlDecoder::~SmartJxlDecoder()
{
    this->assertNotDecoding();
}

void SmartJxlDecoder::close()
{
    JxlDecoderReset(d->djxl.get());
    d->buffer = nullptr;
    
    SmartImageDecoder::close();
}

void SmartJxlDecoder::decodeHeader(const unsigned char* buffer, qint64 nbytes)
{
    d->buffer = buffer;
    d->nbytes = nbytes;
    
    auto ret = JxlDecoderSubscribeEvents(d->djxl.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_PREVIEW_IMAGE);
    if (JXL_DEC_SUCCESS != ret)
    {
        throw std::runtime_error("JxlDecoderSubscribeEvents() failed");
    }
    
    this->setDecodingMessage("Reading JXL Header");
    
    QImage dummy;
    this->decodeInternal(dummy);
}

QImage SmartJxlDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    JxlDecoderRewind(d->djxl.get());
    
    auto ret = JxlDecoderSubscribeEvents(d->djxl.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FRAME_PROGRESSION | JXL_DEC_FULL_IMAGE);
    if (JXL_DEC_SUCCESS != ret)
    {
        throw std::runtime_error("JxlDecoderSubscribeEvents() failed");
    }
    
    this->setDecodingMessage("Reading JXL Image");
    
    
    QImage image;
    this->decodeInternal(image);
    this->convertColorSpace(image, false);
    this->setDecodingState(DecodingState::FullImage);
    this->setDecodingMessage("JXL decoding completed successfully.");
    this->setDecodingProgress(100);

    return image;
}

void SmartJxlDecoder::decodeInternal(QImage& image)
{
    JxlBasicInfo& info = d->jxlInfo;

    auto ret = JxlDecoderSetInput(d->djxl.get(), d->buffer, d->nbytes);
    if (JXL_DEC_SUCCESS != ret)
    {
        throw std::runtime_error("JxlDecoderSetInput() failed");
    }
    
    size_t remaining = d->nbytes;
    size_t seen = 0;
    size_t buffer_size;
    
    std::vector<uint8_t> icc_profile;
    
    QImage thumb;

    for (;;)
    {
        this->cancelCallback();
        JxlDecoderStatus status = JxlDecoderProcessInput(d->djxl.get());

        switch(status)
        {
            case JXL_DEC_ERROR:
                throw std::runtime_error("JXL Decoder error");
            
            case JXL_DEC_BASIC_INFO:
                ret = JxlDecoderGetBasicInfo(d->djxl.get(), &info);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderGetBasicInfo() failed");
                if (info.have_animation)
                    qWarning() << "JXL animations are not supported!";
                
                this->image()->setSize(QSize(info.xsize, info.ysize));
                
                break;
                
            case JXL_DEC_COLOR_ENCODING:
                // Get the ICC color profile of the pixel data
                ret = JxlDecoderGetICCProfileSize(d->djxl.get(),
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(1,0,0)
nullptr /* unused */ ,
#endif
                                                  JXL_COLOR_PROFILE_TARGET_ORIGINAL, &buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderGetICCProfileSize() failed");
                icc_profile.resize(buffer_size);
                ret = JxlDecoderGetColorAsICCProfile(d->djxl.get(),
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(1,0,0)
nullptr /* unused */ ,
#endif
                                                  JXL_COLOR_PROFILE_TARGET_ORIGINAL, icc_profile.data(), icc_profile.size());
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderGetColorAsICCProfile() failed");
                else
                {
                    QByteArray iccProfile(reinterpret_cast<char *>(icc_profile.data()), buffer_size);
                    QColorSpace cs = QColorSpace::fromIccProfile(iccProfile);
                    this->image()->setColorSpace(cs);
                }
                break;
                
            case JXL_DEC_NEED_PREVIEW_OUT_BUFFER:
                ret = JxlDecoderPreviewOutBufferSize(d->djxl.get(), &d->jxlFormat, &buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderPreviewOutBufferSize() failed");
                
                thumb = this->allocateImageBuffer(info.preview.xsize, info.preview.ysize, d->format());
                Q_ASSERT(thumb.bytesPerLine() * thumb.height() == buffer_size);
                
                ret = JxlDecoderSetPreviewOutBuffer(d->djxl.get(), &d->jxlFormat, const_cast<uint8_t*>(thumb.constBits()), buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderSetPreviewOutBuffer() failed");
                break;

            case JXL_DEC_PREVIEW_IMAGE:
                seen += remaining - JxlDecoderReleaseInput(d->djxl.get());
                
                this->setDecodingMessage("A preview image is available");
                this->convertColorSpace(thumb, true);
                this->image()->setThumbnail(thumb);
                break;
                
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
                ret = JxlDecoderImageOutBufferSize(d->djxl.get(), &d->jxlFormat, &buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderImageOutBufferSize() failed");
            
                image = this->allocateImageBuffer(info.xsize, info.ysize, d->format());
                Q_ASSERT(image.bytesPerLine() * image.height() == buffer_size);
                
                d->imgBuf = const_cast<uint8_t*>(image.constBits());
                ret = JxlDecoderSetImageOutCallback(d->djxl.get(), &d->jxlFormat, &d->decoderCallback, d.get());
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderSetImageOutCallback() failed");
                
                this->image()->setDecodedImage(image);
                break;

            case JXL_DEC_NEED_MORE_INPUT:
            case JXL_DEC_SUCCESS:
            case JXL_DEC_FULL_IMAGE:
            case JXL_DEC_FRAME_PROGRESSION:
                if (status == JXL_DEC_NEED_MORE_INPUT && JXL_DEC_SUCCESS != JxlDecoderFlushImage(d->djxl.get()))
                {
                    this->setDecodingMessage("flush error (no preview yet)");
                }
                else
                {
                    this->updateDecodedRoiRect(this->image()->fullResolutionRect());
                }

                if(JXL_DEC_SUCCESS == status)
                {
                    goto leaveLoop;
                }
                break;
 
            default:
                throw std::runtime_error(Formatter() << "Unknown decoder status: " << status);
        }
    }
leaveLoop:
    d->imgBuf = nullptr;
}
