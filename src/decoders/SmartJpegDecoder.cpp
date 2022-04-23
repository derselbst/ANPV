
#include "SmartJpegDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"

#include <vector>
#include <cstdio>
#include <QDebug>
#include <QColorSpace>
#include <csetjmp>
#include "libkexiv2/src/kexiv2previews.h"

extern "C"
{
    #include <jerror.h>
    #include <jpeglib.h>
}

struct my_error_mgr
{
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

struct SmartJpegDecoder::Impl
{
    SmartJpegDecoder* q;
    
    struct jpeg_decompress_struct cinfo = {};
    struct my_error_mgr jerr;
    struct jpeg_progress_mgr progMgr;
    
    Impl(SmartJpegDecoder* parent) : q(parent)
    {
        // We set up the normal JPEG error routines, then override error_exit.
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = &my_error_exit;
        jerr.pub.output_message = &my_output_message;
        progMgr.progress_monitor = &libjpegProgressCallback;
    }
    
    static void libjpegProgressCallback(j_common_ptr cinfo) noexcept
    {
        auto self = static_cast<Impl*>(cinfo->client_data);
        auto& p = *cinfo->progress;
        int progress = static_cast<int>((p.completed_passes * 100.) / p.total_passes);
        
        self->q->setDecodingProgress(progress);
    }
    
    static void my_error_exit(j_common_ptr cinfo) noexcept
    {
        /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
        auto myerr = reinterpret_cast<struct my_error_mgr*>(cinfo->err);

        /* Always display the message. */
        /* We could postpone this until after returning, if we chose. */
        (*cinfo->err->output_message) (cinfo);
        
        /* Return control to the setjmp point */
        longjmp(myerr->setjmp_buffer, 1);
    }
    
    static void my_output_message(j_common_ptr cinfo)
    {
        char buffer[JMSG_LENGTH_MAX];
        auto self = static_cast<Impl*>(cinfo->client_data);

        /* Create the message */
        (*cinfo->err->format_message) (cinfo, buffer);

        self->q->setDecodingMessage(buffer);
    }
};

SmartJpegDecoder::SmartJpegDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>(this))
{}

SmartJpegDecoder::~SmartJpegDecoder()
{
    this->assertNotDecoding();
}


void SmartJpegDecoder::decodeHeader(const unsigned char* buffer, qint64 nbytes)
{
    auto& cinfo = d->cinfo;
    jpeg_create_decompress(&cinfo);

    /* Tell the library to keep any APP2 data it may find */
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);

    cinfo.progress = &d->progMgr;
    cinfo.client_data = d.get();
    
    jpeg_mem_src(&cinfo, buffer, nbytes);

    this->setDecodingMessage("Reading JPEG Header");
    
    if (setjmp(d->jerr.setjmp_buffer))
    {
        // If we get here, the JPEG code has signaled an error.
        throw std::runtime_error("Error while decoding the JPEG header");
    }
    
    int ret = jpeg_read_header(&cinfo, true);
    if(ret != JPEG_HEADER_OK)
    {
        throw std::runtime_error(Formatter() << "jpeg_read_header() failed with code " << ret << ", excpeted: " << JPEG_HEADER_OK);
    }
    
    JOCTET *ptr;
    std::unique_ptr<JOCTET, decltype(&::free)> icc_data(nullptr, free);
    unsigned int icc_len;
    QColorSpace iccProfile{QColorSpace::SRgb};
    if(jpeg_read_icc_profile(&cinfo, &ptr, &icc_len))
    {
        icc_data.reset(ptr);
        iccProfile = QColorSpace::fromIccProfile(QByteArray::fromRawData(reinterpret_cast<const char *>(icc_data.get()), icc_len));
    }
    
    // set overall decompression parameters
    cinfo.buffered_image = true; /* select buffered-image mode */
    cinfo.out_color_space = JCS_EXT_BGRX;
    
    this->setDecodingMessage("Calculating output dimensions");
    
    this->image()->setSize(QSize(cinfo.image_width, cinfo.image_height));
    this->image()->setColorSpace(iccProfile);
}

QImage SmartJpegDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    // the entire jpeg() section below is clobbered by setjmp/longjmp
    // hence, declare any objects with nontrivial destructors here
    std::vector<JSAMPLE*> bufferSetup;
    QImage image;
    
    auto& cinfo = d->cinfo;
    
    if (setjmp(d->jerr.setjmp_buffer))
    {
        // If we get here, the JPEG code has signaled an error.
        throw std::runtime_error("Error while decoding the JPEG image");
    }

    static_assert(sizeof(JSAMPLE) == sizeof(uint8_t), "JSAMPLE is not 8bits, which is unsupported");
    
    // set parameters for decompression
    cinfo.dct_method = JDCT_ISLOW;
    cinfo.dither_mode = JDITHER_FS;
    cinfo.do_fancy_upsampling = true;
    cinfo.enable_2pass_quant = false;
    cinfo.do_block_smoothing = false;
    
    cinfo.scale_num = desiredResolution.width();
    cinfo.scale_denom = roiRect.width();
    
    if(cinfo.scale_num * 1.0 / cinfo.scale_denom > 1.0)
    {
        // do not upscale the image while decoding
        cinfo.scale_denom = cinfo.scale_num = 1;
    }
    
    // Used to set up image size so arrays can be allocated
    jpeg_calc_output_dimensions(&cinfo);

    image = this->allocateImageBuffer(cinfo.output_width, cinfo.output_height, QImage::Format_RGB32);
    auto* dataPtrBackup = image.constBits();
    this->image()->setDecodedImage(image);

    bufferSetup.resize(cinfo.output_height / cinfo.rec_outbuf_height);
    for(JDIMENSION i=0; i < bufferSetup.size(); i++)
    {
        bufferSetup[i] = const_cast<JSAMPLE*>(reinterpret_cast<const JSAMPLE*>(image.constScanLine(i * cinfo.rec_outbuf_height)));
    }
    
    this->cancelCallback();

    // Start decompressor
    this->setDecodingMessage("Starting the JPEG decompressor");
    
    if (jpeg_start_decompress(&cinfo) == false)
    {
        qWarning() << "I/O suspension after jpeg_start_decompress()";
    }
    
    // The library's output processing will automatically call jpeg_consume_input()
    // whenever the output processing overtakes the input; thus, simple lockstep
    // display requires no direct calls to jpeg_consume_input().  But by adding
    // calls to jpeg_consume_input(), you can absorb data in advance of what is
    // being displayed.  This has two benefits:
    //   * You can limit buildup of unprocessed data in your input buffer.
    //   * You can eliminate extra display passes by paying attention to the
    //     state of the library's input processing.
//     int status;
//     do
//     {
//         status = jpeg_consume_input(&cinfo);
//     } while ((status != JPEG_SUSPENDED) && (status != JPEG_REACHED_EOI));


    switch(cinfo.output_components)
    {
        case 1:
        case 3:
        case 4:
            break;
        default:
            throw std::runtime_error(Formatter() << "Unsupported number of pixel color components: " << cinfo.output_components);
    }
    
    this->setDecodingMessage("Consuming and decoding JPEG input file");
    
    const size_t rowStride = cinfo.output_width * sizeof(uint32_t);
    auto totalLinesRead = cinfo.output_scanline;
    while (!jpeg_input_complete(&cinfo))
    {
        /* start a new output pass */
        jpeg_start_output(&cinfo, cinfo.input_scan_number);
        
        while (cinfo.output_scanline < cinfo.output_height)
        {
            auto linesRead = jpeg_read_scanlines(&cinfo, bufferSetup.data()+cinfo.output_scanline, cinfo.rec_outbuf_height);
            this->cancelCallback();

            this->updatePreviewImage(QRect(0, totalLinesRead, cinfo.output_width, linesRead));
            totalLinesRead += linesRead;
        }
        
        /* terminate output pass */
        jpeg_finish_output(&cinfo);

//         if(targetState == DecodingState::PreviewImage)
//         {
//             // only a preview image was requested, which we have finished with this first pass
//             break;
//         }
    }
    
    jpeg_finish_decompress(&cinfo);

    Q_ASSERT(image.constBits() == dataPtrBackup);
    Q_ASSERT(dataPtrBackup == &bufferSetup[0][0]);

// //     this->setDecodingMessage("Applying final smooth rescaling...");
// //     image = image.scaled(desiredResolution, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    this->convertColorSpace(image);

    // call the progress monitor for a last time to report 100% to GUI
    this->setDecodingMessage("JPEG decoding completed successfully.");
    d->progMgr.completed_passes = d->progMgr.total_passes;
    d->progMgr.progress_monitor((j_common_ptr)&cinfo);

    if(cinfo.output_width == cinfo.image_width && cinfo.output_height == cinfo.image_height)
    {
        this->setDecodingState(DecodingState::FullImage);
    }
    else
    {
        this->setDecodingState(DecodingState::PreviewImage);
    }

    Q_ASSERT(image.constBits() == dataPtrBackup);
    return image;
}

void SmartJpegDecoder::close()
{
    jpeg_destroy_decompress(&d->cinfo);
    
    SmartImageDecoder::close();
}

