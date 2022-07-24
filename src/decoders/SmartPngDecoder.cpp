
#include "SmartPngDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"

#include <vector>
#include <cstdio>
#include <QDebug>
#include <QColorSpace>
#include <csetjmp>

#include <png.h>

#ifndef PNG_SETJMP_SUPPORTED
#error "libpng must be compiled with SETJMP support!"
#endif

struct SmartPngDecoder::Impl
{
    SmartPngDecoder* q;
    
    png_structp cinfo;
    png_infop info_ptr;
    png_infop einfo_ptr;

    const unsigned char* inputBufferBegin = nullptr;
    qint64 inputBufferLength = 0;
    const unsigned char* inputBufferPtr = nullptr;

    Impl(SmartPngDecoder* parent) : q(parent)
    {
    }

    size_t inputBufferRemaining()
    {
        return (uintptr_t)(this->inputBufferBegin + this->inputBufferLength) - (uintptr_t)this->inputBufferPtr;
    }

    static void my_read_fn(png_structp png_ptr, png_bytep data, size_t len)
    {
        auto* self = static_cast<SmartPngDecoder::Impl*>(png_get_io_ptr(png_ptr));

        size_t remaining = self->inputBufferRemaining();
        if (remaining < len)
        {
            png_error(png_ptr, "Attempted to read beyond end of file");
        }

        std::memcpy(data, self->inputBufferPtr, len);
        self->inputBufferPtr += len;
    }

    static void my_error_exit(png_structp png_ptr, png_const_charp message) noexcept
    {
        my_output_message(png_ptr, message);

        /* We can return because png_error calls the default handler, which is
         * actually OK in this case.
         */
    }
    
    static void my_output_message(png_structp png_ptr, png_const_charp message)
    {
        auto* self = static_cast<SmartPngDecoder::Impl*>(png_get_error_ptr(png_ptr));

        self->q->setDecodingMessage(message);
    }

    static void my_progress_callback(png_structp png_ptr, png_uint_32 row, int pass)
    {
        auto* self = static_cast<SmartPngDecoder::Impl*>(png_get_io_ptr(png_ptr));

        self->q->updatePreviewImage(QRect(0, row, png_get_image_width(png_ptr, self->info_ptr), 1));
        if (row % 16 == 0)
        {
            self->q->cancelCallback();
        }
        /* TODO put your code here */
    }

    QImage::Format format()
    {
        auto bit_depth = png_get_bit_depth(this->cinfo, this->info_ptr);

        return bit_depth == 16 
            ? QImage::Format_RGBA64
            : QImage::Format_RGBA8888;
    }
};

SmartPngDecoder::SmartPngDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>(this))
{}

SmartPngDecoder::~SmartPngDecoder()
{
    this->assertNotDecoding();
}


void SmartPngDecoder::decodeHeader(const unsigned char* buffer, qint64 nbytes)
{
    d->inputBufferBegin = d->inputBufferPtr = buffer;
    d->inputBufferLength = nbytes;

    auto& cinfo = d->cinfo;
    cinfo = nullptr;
    d->info_ptr = d->einfo_ptr = nullptr;

    cinfo = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_set_error_fn(cinfo, d.get(), SmartPngDecoder::Impl::my_error_exit, SmartPngDecoder::Impl::my_output_message);
    png_set_read_fn(cinfo, d.get(), SmartPngDecoder::Impl::my_read_fn);
    png_set_read_status_fn(cinfo, SmartPngDecoder::Impl::my_progress_callback);

    d->info_ptr = png_create_info_struct(cinfo);
    d->einfo_ptr = png_create_info_struct(cinfo);

    if (cinfo == nullptr || d->info_ptr == nullptr || d->einfo_ptr == nullptr)
    {
        throw std::bad_alloc();
    }

    this->setDecodingMessage("Reading PNG Header");

    // SECTION BELOW CLOBBERED BY setjmp() / longjmp()!
    // Declare all non-trivially destructable objects here.
    QColorSpace iccProfile{ QColorSpace::SRgb };
    if (setjmp(png_jmpbuf(cinfo)))
    {
        // If we get here, the JPEG code has signaled an error.
        throw std::runtime_error("Error while decoding the PNG header");
    }

    png_read_info(cinfo, d->info_ptr);

    uint32_t width, height;
    int bit_depth, color_type, interlace_type, compression_type, filter_type;
    png_get_IHDR(cinfo, d->info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(cinfo);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(cinfo);
    }

    if (png_get_valid(cinfo, d->info_ptr, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(cinfo);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(cinfo);
    }

    if (color_type == PNG_COLOR_TYPE_RGB)
    {
        png_set_filler(cinfo, 0xffff, PNG_FILLER_AFTER);
    }

    switch (interlace_type)
    {
    case PNG_INTERLACE_NONE:
    case PNG_INTERLACE_ADAM7:
        break;
    default:
        throw std::runtime_error(Formatter() << "Unsupported interlace type: " << interlace_type);
    }

    png_charp name;
    png_bytep profile;
    png_uint_32 proflen;
    if (png_get_iCCP(cinfo, d->info_ptr, &name, &compression_type, &profile, &proflen) != 0)
    {
        Q_ASSERT(compression_type == PNG_COMPRESSION_TYPE_BASE);
        iccProfile = QColorSpace::fromIccProfile(QByteArray::fromRawData(reinterpret_cast<char*>(profile), proflen));
    }
    
    this->image()->setSize(QSize(width, height));
    this->image()->setColorSpace(iccProfile);
}


QImage SmartPngDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    auto& cinfo = d->cinfo;

    auto width = png_get_image_width(cinfo, d->info_ptr);
    auto height = png_get_image_height(cinfo, d->info_ptr);

    QImage image;
    image = this->allocateImageBuffer(width, height, d->format());
    auto* dataPtrBackup = image.constBits();
    this->image()->setDecodedImage(image);

    std::vector<unsigned char*> bufferSetup;
    bufferSetup.resize(png_get_image_height(cinfo, d->info_ptr));
    for (size_t i = 0; i < bufferSetup.size(); i++)
    {
        bufferSetup[i] = const_cast<unsigned char*>(image.constScanLine(i));
    }

    // the entire section below is clobbered by setjmp/longjmp
    // hence, declare any objects with nontrivial destructors here
    if (setjmp(png_jmpbuf(cinfo)))
    {
        // If we get here, the JPEG code has signaled an error.
        throw std::runtime_error("Error while decoding the PNG image");
    }

    int numPasses = 1, interlace_type = png_get_interlace_type(cinfo, d->info_ptr);
    if (interlace_type == PNG_INTERLACE_ADAM7)
    {
        numPasses = png_set_interlace_handling(cinfo);
    }

    this->setDecodingMessage("Consuming and decoding PNG input file");

    for (int pass = 0; pass < numPasses; pass++)
    {
        png_read_rows(cinfo, bufferSetup.data(), nullptr, png_get_image_height(cinfo, d->info_ptr));
        this->cancelCallback();
    }

    Q_ASSERT(image.constBits() == dataPtrBackup);
    this->convertColorSpace(image);

    // call the progress monitor for a last time to report 100% to GUI
    this->setDecodingMessage("PNG decoding completed successfully.");
    this->setDecodingState(DecodingState::FullImage);

    Q_ASSERT(image.constBits() == dataPtrBackup);

    return image;
}

void SmartPngDecoder::close()
{
    png_destroy_read_struct(&d->cinfo, &d->info_ptr, &d->einfo_ptr);
    
    SmartImageDecoder::close();
}

