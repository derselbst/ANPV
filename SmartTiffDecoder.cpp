
#include "SmartTiffDecoder.hpp"
#include "Formatter.hpp"

#include <QDebug>
// #include <QColorSpace>

#include "tiffio.hxx"

struct SmartTiffDecoder::Impl
{
    SmartTiffDecoder* q;
    QString latestProgressMsg;
    
    TIFF* tiff = nullptr;
    const unsigned char* buffer = nullptr;
    qint64 offset = 0;
    qint64 nbytes = 0;
    
    std::vector<uint32_t> decodedImg;
    
    int currentImageLayer = 0;
    int totalImageLayers;
    
    uint32_t width;
    uint32_t height;
    
    Impl(SmartTiffDecoder* parent) : q(parent)
    {}
    
    static void convert32BitOrder(uint32_t *target, quint32 width)
    {
        for (quint32 x=0; x<width; ++x)
        {
            uint32 p = target[x];
            // convert between ARGB and ABGR
            target[x] = TIFFGetA(p) << 24 |
                        TIFFGetR(p) << 16 |
                        TIFFGetG(p) <<  8 |
                        TIFFGetB(p);
        }
    }
    
    static tsize_t qtiffReadProc(thandle_t fd, tdata_t buf, tsize_t size)
    {
        auto impl = static_cast<Impl*>(fd);
        
        memcpy(buf, &impl->buffer[impl->offset], size);
        
        impl->offset += size;
        
        return size;
    }

    static tsize_t qtiffWriteProc(thandle_t fd, tdata_t buf, tsize_t size)
    {
        return 0;
    }

    static toff_t qtiffSeekProc(thandle_t fd, toff_t off, int whence)
    {
        auto impl = static_cast<Impl*>(fd);
        
        switch (whence) {
        case SEEK_SET:
            impl->offset = off;
            break;
        case SEEK_CUR:
            impl->offset += off;
            break;
        case SEEK_END:
            impl->offset = impl->nbytes + off;
            break;
        }

        return impl->offset;
    }

    static int qtiffCloseProc(thandle_t /*fd*/)
    {
        return 0;
    }

    static toff_t qtiffSizeProc(thandle_t fd)
    {
        return static_cast<Impl*>(fd)->offset;
    }

    static int qtiffMapProc(thandle_t /*fd*/, tdata_t* /*pbase*/, toff_t* /*psize*/)
    {
        return 0;
    }

    static void qtiffUnmapProc(thandle_t /*fd*/, tdata_t /*base*/, toff_t /*size*/)
    {
    }

    ~Impl()
    {
        if (tiff)
        {
            TIFFClose(tiff);
        }
        tiff = nullptr;
    }
};

SmartTiffDecoder::SmartTiffDecoder(QString&& file) : SmartImageDecoder(std::move(file)), d(std::make_unique<Impl>(this))
{
}

SmartTiffDecoder::~SmartTiffDecoder() = default;

QSize SmartTiffDecoder::size()
{
    auto s = this->decodingState();
    switch(s)
    {
        case DecodingState::Metadata:
        case DecodingState::PreviewImage:
        case DecodingState::FullImage:
            qWarning() << "TODO APPLY EXIF TRNASOFMRTA";
            return QSize(d->width, d->height);
        default:
            throw std::logic_error(Formatter() << "Wrong DecodingState: " << s);
    }
}

void SmartTiffDecoder::decodeHeader()
{
    this->fileBuf(&d->buffer, &d->nbytes);
    
    d->latestProgressMsg = "Reading TIFF Header";
    
    d->tiff = TIFFClientOpen("foo",
                            "r",
                            d.get(),
                            d->qtiffReadProc,
                            d->qtiffWriteProc,
                            d->qtiffSeekProc,
                            d->qtiffCloseProc,
                            d->qtiffSizeProc,
                            d->qtiffMapProc,
                            d->qtiffUnmapProc);
    
    TIFFSetDirectory(d->tiff, d->currentImageLayer);
    
    
    if (!TIFFGetField(d->tiff, TIFFTAG_IMAGEWIDTH, &d->width) ||
        !TIFFGetField(d->tiff, TIFFTAG_IMAGELENGTH, &d->height))
    {
        throw std::runtime_error("Error while reading TIFF dimensions");
    }
}

void SmartTiffDecoder::decodingLoop(DecodingState targetState)
{
    const quint32 width = d->width;
    const quint32 height = d->height;

    d->decodedImg.resize(width * height);
    QImage image(reinterpret_cast<uint8_t*>(d->decodedImg.data()),
                width,
                height,
                width * sizeof(uint32_t),
                QImage::Format_ARGB32);
    
    
    TIFFSetDirectory(tif, 0);
    
    do
    {
    
	uint16 config;
	TIFFGetField(d->tiff, TIFFTAG_PLANARCONFIG, &config);
    
    // sample per pixel
    uint32 spp;
	TIFFGetField(input_file, TIFFTAG_SAMPLESPERPIXEL, &spp);
    
        if (config == PLANARCONFIG_CONTIG)
        {
            for(r = 0; r < height; r++){
                TIFFReadScanline(tif, scanline, r, s);

                for(c = 0; c < info->width; c++)
                {		
                    image[image_offset + info->width * r + c] = *(scanline + c);
                }

            }
        }
        else if (config == PLANARCONFIG_SEPARATE){
            for(s = 0; s < spp; s++){
                for(r = 0; r < height; r++){
                    TIFFReadScanline(tif, scanline, r, s);
                    for(c = 0; c < info->width; c++)
                    {
                        image[image_offset + info->width * r + c] = *(scanline + c);
                    }

                }
            }
        }
        image_offset += info->image_size/sizeof(uint16);
    } while (TIFFReadDirectory(tif));

    
    
    
    
    
    
    constexpr int stopOnError = 1;
    if (TIFFReadRGBAImage(d->tiff, width, height, d->decodedImg.data(), stopOnError))
    {
        for (uint32 y=0; y<height; ++y)
            d->convert32BitOrder(reinterpret_cast<uint32_t*>(image.scanLine(y)), width);
    }
    else
    {
        throw std::runtime_error(Formatter() << d->latestProgressMsg.toStdString());
    }
    
    float resX = 0;
    float resY = 0;
    uint16 resUnit;
    if (!TIFFGetField(d->tiff, TIFFTAG_RESOLUTIONUNIT, &resUnit))
        resUnit = RESUNIT_INCH;

    if (TIFFGetField(d->tiff, TIFFTAG_XRESOLUTION, &resX)
        && TIFFGetField(d->tiff, TIFFTAG_YRESOLUTION, &resY))
    {

        switch(resUnit) {
        case RESUNIT_CENTIMETER:
            image.setDotsPerMeterX(qRound(resX * 100));
            image.setDotsPerMeterY(qRound(resY * 100));
            break;
        case RESUNIT_INCH:
            image.setDotsPerMeterX(qRound(resX * (100 / 2.54)));
            image.setDotsPerMeterY(qRound(resY * (100 / 2.54)));
            break;
        default:
            // do nothing as defaults have already
            // been set within the QImage class
            break;
        }
    }

    uint32 count;
//     void *profile;
//     if (TIFFGetField(d->tiff, TIFFTAG_ICCPROFILE, &count, &profile))
//     {
//         QByteArray iccProfile(reinterpret_cast<const char *>(profile), count);
//         image.setColorSpace(QColorSpace::fromIccProfile(iccProfile));
//     }
    
    this->setDecodingState(DecodingState::PreviewImage);
    
    this->setImage(std::move(image));
}
