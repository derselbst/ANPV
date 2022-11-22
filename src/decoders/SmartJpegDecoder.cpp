
#include "SmartJpegDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"

#include <vector>
#include <cstdio>
#include <QDebug>
#include <QColorSpace>
#include <csetjmp>

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

    // section below clobbered by setjmp()/longjmp(); declare all non-trivially destroyable types here
    std::unique_ptr<JOCTET, decltype(&::free)> icc_data(nullptr, free);
    QColorSpace iccProfile{ QColorSpace::SRgb };
    
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
    unsigned int icc_len;
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
    if (!roiRect.isValid())
    {
        roiRect = this->image()->fullResolutionRect();
    }
    
    auto& cinfo = d->cinfo;

    // the entire jpeg() section below is clobbered by setjmp/longjmp
    // hence, declare any objects with nontrivial destructors here
    std::vector<JSAMPLE*> bufferSetup;
    QImage image;
    QRect scaledRoi;
    QTransform currentResToFullResTrafo, fullResToCurrentRes;
    
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
    
    double scale = cinfo.scale_num * 1.0 / cinfo.scale_denom;
    if(scale > 1.0)
    {
        // do not upscale the image while decoding
        cinfo.scale_denom = cinfo.scale_num = scale = 1;
    }

    // Used to set up image size so arrays can be allocated
    jpeg_calc_output_dimensions(&cinfo);

    // update the scale because output dimensions might be a bit different to what we requested
    fullResToCurrentRes = this->fullResToPageTransform(cinfo.output_width, cinfo.output_height);

    scaledRoi = fullResToCurrentRes.mapRect(roiRect);
    Q_ASSERT(scaledRoi.isValid());
    Q_ASSERT(scaledRoi.width() <= cinfo.output_width);
    Q_ASSERT(scaledRoi.height() <= cinfo.output_height);

    // Start decompressor
    this->setDecodingMessage("Starting the JPEG decompressor");

    if (jpeg_start_decompress(&cinfo) == false)
    {
        qWarning() << "I/O suspension after jpeg_start_decompress()";
    }

    JDIMENSION xoffset = scaledRoi.x();
    JDIMENSION croppedWidth = scaledRoi.width();
    jpeg_crop_scanline(&cinfo, &xoffset, &croppedWidth);
    scaledRoi.setX(xoffset);
    scaledRoi.setWidth(croppedWidth);

    const JDIMENSION skippedScanlinesTop = scaledRoi.y();
    const JDIMENSION lastScanlineToDecode = skippedScanlinesTop + scaledRoi.height();

    image = this->allocateImageBuffer(scaledRoi.width(), scaledRoi.height(), QImage::Format_ARGB32);
    auto* dataPtrBackup = image.constBits();
    currentResToFullResTrafo = fullResToCurrentRes.inverted();
    image.setOffset(currentResToFullResTrafo.mapRect(scaledRoi).topLeft());

    this->image()->setDecodedImage(image, currentResToFullResTrafo);
    this->resetDecodedRoiRect();

    bufferSetup.resize(image.height() / cinfo.rec_outbuf_height);
    for (JDIMENSION i = 0; i < bufferSetup.size(); i++)
    {
        bufferSetup[i] = const_cast<JSAMPLE*>(image.constScanLine(i * cinfo.rec_outbuf_height));
    }
    
    this->cancelCallback();

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
    
    int progressiveGuard;
    for (progressiveGuard = 0; (!jpeg_input_complete(&cinfo)) && progressiveGuard < 1000; progressiveGuard++)
    {
        /* start a new output pass */
        jpeg_start_output(&cinfo, cinfo.input_scan_number);
        auto acuallySkipped = jpeg_skip_scanlines(&cinfo, skippedScanlinesTop);
        while (cinfo.output_scanline < lastScanlineToDecode)
        {
            auto linesRead = jpeg_read_scanlines(&cinfo, &bufferSetup[cinfo.output_scanline - skippedScanlinesTop], cinfo.rec_outbuf_height);
            this->cancelCallback();

            QRect decodedAreaOfShrinkedPage(xoffset, cinfo.output_scanline - linesRead, croppedWidth, linesRead);
            this->updateDecodedRoiRect(decodedAreaOfShrinkedPage);
        }
        
        /* terminate output pass */
        jpeg_finish_output(&cinfo);
    }
    
    jpeg_finish_decompress(&cinfo);

    Q_ASSERT(image.constBits() == dataPtrBackup);
    //Q_ASSERT(dataPtrBackup == &bufferSetup[0][0]);

// //     this->setDecodingMessage("Applying final smooth rescaling...");
// //     image = image.scaled(desiredResolution, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    this->convertColorSpace(image, false, currentResToFullResTrafo);

    if (progressiveGuard >= 1000)
    {
        // see https://libjpeg-turbo.org/pmwiki/uploads/About/TwoIssueswiththeJPEGStandard.pdf
        this->setDecodingMessage("Progressive JPEG decoding was aborted after decoding 1000 scans");
    }
    else
    {
        // call the progress monitor for a last time to report 100% to GUI
        this->setDecodingMessage("JPEG decoding completed successfully.");
    }
    d->progMgr.completed_passes = d->progMgr.total_passes;
    d->progMgr.progress_monitor((j_common_ptr)&cinfo);

    if(scale == 1 && xoffset == 0 && croppedWidth == cinfo.image_width && skippedScanlinesTop == 0 && lastScanlineToDecode == cinfo.image_height)
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

