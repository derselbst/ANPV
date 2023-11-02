
#include "SmartJxlDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <QDebug>
#include <QtGlobal>
#include <QColorSpace>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>

struct PageInfo
{
    uint32_t width;
    uint32_t height;
	uint16_t config;
    // bits per sample
    uint16_t bps;
    // sample per pixel
    uint16_t spp;
    
    size_t nPix()
    {
        return static_cast<size_t>(this->width) * height;
    }
};

constexpr const char TiffModule[] = "SmartJxlDecoder";

// Note: a lot of the code has been taken from: 
// https://github.com/qt/qtimageformats/blob/c64f19516dd2467bf5746eb24afe883bdbc15b25/src/plugins/imageformats/tiff/qtiffhandler.cpp

struct SmartJxlDecoder::Impl
{
    SmartJxlDecoder* q;
    
    JxlDecoderPtr djxl;
    
    const unsigned char* buffer = nullptr;
    qint64 nbytes = 0;
    
    Impl(SmartJxlDecoder* q) : q(q)
    {
        this->djxl = JxlDecoderMake(nullptr);
    }
        
    QImage::Format format()
    {
        // The zero initialized, not-yet-decoded image buffer should be displayed transparently. Therefore, always use ARGB, even if this
        // would cause a performance drawback for images which do not have one, because Qt may call QPixmap::mask() internally.
        return QImage::Format_RGBA8888;
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

    this->setDecodingState(DecodingState::FullImage);
    this->setDecodingMessage("JXL decoding completed successfully.");
    this->setDecodingProgress(100);

    return image;
}

void SmartJxlDecoder::decodeInternal(QImage& image)
{
    JxlBasicInfo info;
    static const JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

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
                ret = JxlDecoderPreviewOutBufferSize(d->djxl.get(), &format, &buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderPreviewOutBufferSize() failed");
                
                thumb = this->allocateImageBuffer(info.preview.xsize, info.preview.ysize, d->format());
                Q_ASSERT(thumb.bytesPerLine() * thumb.height() == buffer_size);
                
                ret = JxlDecoderSetPreviewOutBuffer(d->djxl.get(), &format, const_cast<uint8_t*>(thumb.constBits()), buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderSetPreviewOutBuffer() failed");
                break;

            case JXL_DEC_PREVIEW_IMAGE:
                seen += remaining - JxlDecoderReleaseInput(d->djxl.get());
                
                this->setDecodingMessage("A preview image is available");
                this->convertColorSpace(thumb, true);
                this->image()->setThumbnail(thumb);
                goto remainderCalc;
                
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
                ret = JxlDecoderImageOutBufferSize(d->djxl.get(), &format, &buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderImageOutBufferSize() failed");
            
                image = this->allocateImageBuffer(info.xsize, info.ysize, d->format());
                Q_ASSERT(image.bytesPerLine() * image.height() == buffer_size);
                
                ret = JxlDecoderSetImageOutBuffer(d->djxl.get(), &format, const_cast<uint8_t*>(image.constBits()), buffer_size);
                if (JXL_DEC_SUCCESS != ret)
                    throw std::runtime_error("JxlDecoderSetImageOutBuffer() failed");
                
                this->image()->setDecodedImage(image);
                break;

            case JXL_DEC_NEED_MORE_INPUT:
            case JXL_DEC_SUCCESS:
            case JXL_DEC_FULL_IMAGE:
            case JXL_DEC_FRAME_PROGRESSION:
                seen += remaining - JxlDecoderReleaseInput(d->djxl.get());
                this->setDecodingMessage(QString("Flushing after %1 bytes").arg(QString::number(seen)));
//                 if (status == JXL_DEC_NEED_MORE_INPUT && JXL_DEC_SUCCESS != JxlDecoderFlushImage(d->djxl.get()))
//                 {
//                     this->setDecodingMessage("flush error (no preview yet)");
//                 }
//                 else
                {
                    this->updateDecodedRoiRect(this->image()->fullResolutionRect());
                }
            remainderCalc:
                remaining = d->nbytes - seen;
                if (remaining == 0)
                {
                    if (status == JXL_DEC_NEED_MORE_INPUT)
                        throw std::runtime_error("Decoding error, end of file reached!");
                }
                JxlDecoderSetInput(d->djxl.get(), d->buffer + seen, remaining);

                if(JXL_DEC_SUCCESS == status)
                {
                    return;
                }
                break;
 
            default:
                throw std::runtime_error(Formatter() << "Unknown decoder status: " << status);
        }
    }
}
