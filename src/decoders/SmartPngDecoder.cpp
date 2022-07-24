
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

    static void my_read_fn(png_structp png_ptr, png_bytep data, size_t len)
    {
        auto* self = static_cast<SmartPngDecoder::Impl*>(png_get_io_ptr(png_ptr));

        size_t remaining = (uintptr_t)self->inputBufferPtr - (uintptr_t)self->inputBufferBegin;
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
        auto* self = static_cast<SmartPngDecoder::Impl*>(png_get_error_ptr(png_ptr));

        /* TODO put your code here */
    }

    QImage::Format format()
    {
        auto bit_depth = png_get_bit_depth(this->cinfo, this->info_ptr);

        return bit_depth == 16 
            ? QImage::Format_RGBA64
            : QImage::Format_ARGB32;
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
    png_set_error_fn(cinfo, this, SmartPngDecoder::Impl::my_error_exit, SmartPngDecoder::Impl::my_output_message);
    png_set_read_fn(cinfo, this, SmartPngDecoder::Impl::my_read_fn);
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

    int number_of_passes = 1;
    if (interlace_type == PNG_INTERLACE_ADAM7)
    {
        number_of_passes = png_set_interlace_handling(cinfo);
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
    //std::vector<JSAMPLE*> bufferSetup;
    QImage image;
    
    auto& cinfo = d->cinfo;

    // the entire section below is clobbered by setjmp/longjmp
    // hence, declare any objects with nontrivial destructors here
    if (setjmp(png_jmpbuf(cinfo)))
    {
        // If we get here, the JPEG code has signaled an error.
        throw std::runtime_error("Error while decoding the PNG image");
    }

//    static_assert(sizeof(JSAMPLE) == sizeof(uint8_t), "JSAMPLE is not 8bits, which is unsupported");
//    
//    // set parameters for decompression
//    cinfo.dct_method = JDCT_ISLOW;
//    cinfo.dither_mode = JDITHER_FS;
//    cinfo.do_fancy_upsampling = true;
//    cinfo.enable_2pass_quant = false;
//    cinfo.do_block_smoothing = false;
//    
//    cinfo.scale_num = desiredResolution.width();
//    cinfo.scale_denom = roiRect.width();
//    
//    double scale = cinfo.scale_num * 1.0 / cinfo.scale_denom;
//    if(scale > 1.0)
//    {
//        // do not upscale the image while decoding
//        cinfo.scale_denom = cinfo.scale_num = scale = 1;
//    }
//
//    // Used to set up image size so arrays can be allocated
//    jpeg_calc_output_dimensions(&cinfo);
//
//    // update the scale because output dimensions might be a bit different to what we requested
//    scale = cinfo.output_width * 1.0 / cinfo.image_width;
//
//    QRect scaledRoi(std::floor(roiRect.x() * scale), std::floor(roiRect.y() * scale), std::ceil(roiRect.width() * scale), std::ceil(roiRect.height() * scale));
//    Q_ASSERT(scaledRoi.isValid());
//    Q_ASSERT(scaledRoi.width() <= cinfo.output_width);
//    Q_ASSERT(scaledRoi.height() <= cinfo.output_height);
//
//    // Start decompressor
//    this->setDecodingMessage("Starting the JPEG decompressor");
//
//    if (jpeg_start_decompress(&cinfo) == false)
//    {
//        qWarning() << "I/O suspension after jpeg_start_decompress()";
//    }
//
//    // TODO: The buffer allocation should be done after cropping the scanline
//    image = this->allocateImageBuffer(cinfo.output_width, cinfo.output_height, QImage::Format_ARGB32);
//    auto* dataPtrBackup = image.constBits();
//    this->image()->setDecodedImage(image);
//
//    JDIMENSION xoffset = scaledRoi.x();
//    JDIMENSION croppedWidth = scaledRoi.width();
//    if (xoffset > 0 && croppedWidth > 0)
//    {
//        cinfo.global_state = 205;
//        jpeg_crop_scanline(&cinfo, &xoffset, &croppedWidth);
//        cinfo.global_state = 207;
//    }
//    const JDIMENSION skippedScanlinesTop = scaledRoi.y();
//    const JDIMENSION lastScanlineToDecode = skippedScanlinesTop + scaledRoi.height();
//
//    // TODO: buffer allocation should be done here
//
//    bufferSetup.resize(cinfo.output_height / cinfo.rec_outbuf_height);
//    for (JDIMENSION i = 0; i < bufferSetup.size(); i++)
//    {
//        bufferSetup[i] = const_cast<JSAMPLE*>(image.constScanLine(i * cinfo.rec_outbuf_height));
//        bufferSetup[i] += xoffset * sizeof(uint32_t);
//    }
//    
//    this->cancelCallback();
//
//    // The library's output processing will automatically call jpeg_consume_input()
//    // whenever the output processing overtakes the input; thus, simple lockstep
//    // display requires no direct calls to jpeg_consume_input().  But by adding
//    // calls to jpeg_consume_input(), you can absorb data in advance of what is
//    // being displayed.  This has two benefits:
//    //   * You can limit buildup of unprocessed data in your input buffer.
//    //   * You can eliminate extra display passes by paying attention to the
//    //     state of the library's input processing.
////     int status;
////     do
////     {
////         status = jpeg_consume_input(&cinfo);
////     } while ((status != JPEG_SUSPENDED) && (status != JPEG_REACHED_EOI));
//
//
//    switch(cinfo.output_components)
//    {
//        case 1:
//        case 3:
//        case 4:
//            break;
//        default:
//            throw std::runtime_error(Formatter() << "Unsupported number of pixel color components: " << cinfo.output_components);
//    }
//    
//    this->setDecodingMessage("Consuming and decoding JPEG input file");
//    
//    int progressiveGuard;
//    for (progressiveGuard = 0; (!jpeg_input_complete(&cinfo)) && progressiveGuard < 1000; progressiveGuard++)
//    {
//        /* start a new output pass */
//        jpeg_start_output(&cinfo, cinfo.input_scan_number);
//        auto acuallySkipped = jpeg_skip_scanlines(&cinfo, skippedScanlinesTop);
//        auto totalLinesRead = cinfo.output_scanline;
//        while (cinfo.output_scanline < lastScanlineToDecode)
//        {
//            auto linesRead = jpeg_read_scanlines(&cinfo, bufferSetup.data()+cinfo.output_scanline, cinfo.rec_outbuf_height);
//            this->cancelCallback();
//
//            this->updatePreviewImage(QRect(xoffset, totalLinesRead, croppedWidth, linesRead));
//            totalLinesRead += linesRead;
//        }
//        
//        /* terminate output pass */
//        jpeg_finish_output(&cinfo);
//
////         if(targetState == DecodingState::PreviewImage)
////         {
////             // only a preview image was requested, which we have finished with this first pass
////             break;
////         }
//    }
//    
//    jpeg_finish_decompress(&cinfo);
//
//    Q_ASSERT(image.constBits() == dataPtrBackup);
//    //Q_ASSERT(dataPtrBackup == &bufferSetup[0][0]);
//
//// //     this->setDecodingMessage("Applying final smooth rescaling...");
//// //     image = image.scaled(desiredResolution, Qt::KeepAspectRatio, Qt::SmoothTransformation);
//
//    this->convertColorSpace(image);
//
//    if (progressiveGuard >= 1000)
//    {
//        // see https://libjpeg-turbo.org/pmwiki/uploads/About/TwoIssueswiththeJPEGStandard.pdf
//        this->setDecodingMessage("Progressive JPEG decoding was aborted after decoding 1000 scans");
//    }
//    else
//    {
//        // call the progress monitor for a last time to report 100% to GUI
//        this->setDecodingMessage("JPEG decoding completed successfully.");
//    }
//    d->progMgr.completed_passes = d->progMgr.total_passes;
//    d->progMgr.progress_monitor((j_common_ptr)&cinfo);
//
//    if(scale == 1 && xoffset == 0 && croppedWidth == cinfo.image_width && skippedScanlinesTop == 0 && lastScanlineToDecode == cinfo.image_height)
//    {
//        this->setDecodingState(DecodingState::FullImage);
//    }
//    else
//    {
//        this->setDecodingState(DecodingState::PreviewImage);
//    }

    //Q_ASSERT(image.constBits() == dataPtrBackup);
    return image;
}

void SmartPngDecoder::close()
{
    png_destroy_read_struct(&d->cinfo, &d->info_ptr, &d->einfo_ptr);
    
    SmartImageDecoder::close();
}

