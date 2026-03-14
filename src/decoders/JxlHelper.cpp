
#include "JxlHelper.hpp"
#include "Formatter.hpp"

#include <stdexcept>

#include <jxl/decode_cxx.h>

namespace JxlHelper
{

std::vector<uint8_t> decodeCodestream(const uint8_t *data, size_t dataSize, uint32_t &outWidth, uint32_t &outHeight)
{
    auto dec = JxlDecoderMake(nullptr);

    if(!dec)
    {
        throw std::runtime_error("JxlDecoderMake() failed");
    }

    if(JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
    {
        throw std::runtime_error("JxlDecoderSubscribeEvents() failed");
    }

    if(JxlDecoderSetInput(dec.get(), data, dataSize) != JXL_DEC_SUCCESS)
    {
        throw std::runtime_error("JxlDecoderSetInput() failed");
    }

    JxlDecoderCloseInput(dec.get());

    std::vector<uint8_t> pixels;

    for(;;)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        switch(status)
        {
        case JXL_DEC_BASIC_INFO:
        {
            JxlBasicInfo info;

            if(JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderGetBasicInfo() failed");
            }

            outWidth = info.xsize;
            outHeight = info.ysize;
            break;
        }

        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
        {
            size_t buffer_size;

            if(JxlDecoderImageOutBufferSize(dec.get(), &jxlFormat, &buffer_size) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderImageOutBufferSize() failed");
            }

            pixels.resize(buffer_size);

            if(JxlDecoderSetImageOutBuffer(dec.get(), &jxlFormat, pixels.data(), pixels.size()) != JXL_DEC_SUCCESS)
            {
                throw std::runtime_error("JxlDecoderSetImageOutBuffer() failed");
            }

            break;
        }

        case JXL_DEC_FULL_IMAGE:
            return pixels;

        case JXL_DEC_SUCCESS:
            return pixels;

        case JXL_DEC_ERROR:
            throw std::runtime_error("JXL decoder error");

        default:
            throw std::runtime_error(Formatter() << "Unexpected JXL decoder status: " << status);
        }
    }
}

void convertRGBAtoARGB32(uint32_t *__restrict dst, const uint8_t *__restrict src, uint32_t count)
{
    for(uint32_t i = 0; i < count; i++)
    {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        dst[i] = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
}

} // namespace JxlHelper
