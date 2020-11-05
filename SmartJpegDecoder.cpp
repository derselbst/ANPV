
#include "SmartJpegDecoder.hpp"
#include "Formatter.hpp"

#include <vector>
#include <cstdio>
#include <QDebug>

extern "C"
{
    #include <jerror.h>
    #include <jpeglib.h>
}

struct SmartJpegDecoder::Impl
{
    SmartJpegDecoder* q;
    
    struct jpeg_decompress_struct cinfo; 
    struct jpeg_error_mgr jerr;
    struct jpeg_progress_mgr progMgr;
    
    QString latestProgressMsg;
    int latestProgress = 0;
    
    std::vector<JSAMPLE> decodedImg;
    
    Impl(SmartJpegDecoder* parent) : q(parent)
    {        
        // Setup decompression structure
        cinfo.err = jpeg_std_error(&jerr);
        
        jpeg_create_decompress(&cinfo);
        cinfo.progress = &progMgr;
        cinfo.client_data = this;
        progMgr.progress_monitor = &Impl::libjpegProgressCallback;
    }
    
    static void libjpegProgressCallback(j_common_ptr cinfo)
    {
        auto self = static_cast<Impl*>(cinfo->client_data);
        auto& p = *cinfo->progress;
        int progress = static_cast<int>((p.completed_passes * 100.) / p.total_passes);
        
        if(self->latestProgress != progress)
        {
            emit self->q->decodingProgress(self->q, progress, self->latestProgressMsg);
        }
        
        self->latestProgress = progress;
    }
    
    ~Impl()
    {
        jpeg_destroy_decompress(&cinfo);
    }
};

SmartJpegDecoder::SmartJpegDecoder(QString&& file) : SmartImageDecoder(std::move(file)), d(std::make_unique<Impl>(this))
{
}

SmartJpegDecoder::~SmartJpegDecoder() = default;

void SmartJpegDecoder::decodeHeader()
{/*
    const char* input = this->file().toUtf8().constData();
    if(d->infile == nullptr)
    {
        d->infile = fopen(input, "rb");
    }
    
    if(d->infile == nullptr)
    {
        throw std::runtime_error(Formatter() << "Unable to open " << input << " (" << strerror(errno) << ")");
    }
    
    jpeg_stdio_src(&d->cinfo, infile);
    
    */
    auto& cinfo = d->cinfo;
    
    const unsigned char* buffer;
    qint64 nbytes;
    this->fileBuf(&buffer, &nbytes);
    
    jpeg_mem_src(&cinfo, buffer, nbytes);

    d->latestProgressMsg = "Reading JPEG Header";
    int ret = jpeg_read_header(&cinfo, true);
    if(ret != JPEG_HEADER_OK)
    {
        throw std::runtime_error(Formatter() << "jpeg_read_header() failed with code " << ret << ", excpeted: " << JPEG_HEADER_OK);
    }
}

void SmartJpegDecoder::decodingLoop(DecodingState state)
{
    auto& cinfo = d->cinfo;
    
    // set overall decompression parameters
    cinfo.buffered_image = true; /* select buffered-image mode */
    cinfo.out_color_space = JCS_EXT_BGRX;
    
    d->latestProgressMsg = "Calculating output dimensions";
    // Used to set up image size so arrays can be allocated
    jpeg_calc_output_dimensions(&cinfo);
    
    d->latestProgressMsg = "Allocating memory for decoded image";
    static_assert(sizeof(JSAMPLE) == sizeof(uint8_t), "JSAMPLE is not 8bits, which is unsupported");
    std::vector<JSAMPLE*> bufferSetup;
    size_t rowStride = cinfo.output_width * sizeof(uint32_t);
    size_t needed = rowStride * cinfo.output_height;
    try
    {
        d->decodedImg.resize(needed);
        
        bufferSetup.resize(cinfo.output_height);
        for(JDIMENSION i=0; i < cinfo.output_height; i++)
        {
            bufferSetup[i] = &d->decodedImg[i * rowStride];
        }
        
        this->setImage(QImage(d->decodedImg.data(),
                              cinfo.output_width,
                              cinfo.output_height,
                              rowStride,
                              QImage::Format_RGB32));
        this->setDecodingState(DecodingState::PreviewImage);
    }
    catch(const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << needed /1024. /1024. << " MiB for the decoded image with dimensions " << cinfo.output_width << "x" << cinfo.output_height << " px");
    }
    
    // set parameters for decompression
    cinfo.dct_method = JDCT_ISLOW;
    cinfo.dither_mode = JDITHER_FS;
    cinfo.do_fancy_upsampling = true;
    cinfo.enable_2pass_quant = false;
    cinfo.do_block_smoothing = false;
    
    this->cancelCallback();

    // Start decompressor
    d->latestProgressMsg = "Starting the JPEG decompressor";
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
    
    d->latestProgressMsg = "Consuming and decoding JPEG input file";
    while (!jpeg_input_complete(&cinfo))
    {
        auto start = std::chrono::steady_clock::now();
        
        /* start a new output pass */
        jpeg_start_output(&cinfo, cinfo.input_scan_number);
        
        while (cinfo.output_scanline < cinfo.output_height)
        {
            jpeg_read_scanlines(&cinfo, bufferSetup.data()+cinfo.output_scanline, 1);
            this->cancelCallback();
            
            auto end = std::chrono::steady_clock::now();
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            if(durationMs.count() > DecodePreviewImageRefreshDuration)
            {
                start = end;
                // trigger the PreviewImage state again to update the UI
                this->setDecodingState(DecodingState::PreviewImage);
            }
        }
        
        /* terminate output pass */
        jpeg_finish_output(&cinfo);
    }
    
    jpeg_finish_decompress(&cinfo);
    
    d->latestProgressMsg = "JPEG decoding completed successfully.";
    // call the progress monitor for a last time to report 100% to GUI
    d->progMgr.completed_passes = d->progMgr.total_passes;
    d->progMgr.progress_monitor((j_common_ptr)&cinfo);
    
//     fclose(d->infile);
//     d->infile = nullptr;
}


