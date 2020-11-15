
#include "SmartTiffDecoder.hpp"
#include "Formatter.hpp"

#include <cstdio>
#include <cmath>
#include <QDebug>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#include <QColorSpace>
#endif

#include "tiffio.hxx"

struct PageInfo
{
    uint32_t width;
    uint32_t height;
	uint16_t config;
    // bits per sample
    uint16_t bps;
    // sample per pixel
    uint16_t spp;
};

// Note: a lot of the code has been taken from: 
// https://github.com/qt/qtimageformats/blob/c64f19516dd2467bf5746eb24afe883bdbc15b25/src/plugins/imageformats/tiff/qtiffhandler.cpp

struct SmartTiffDecoder::Impl
{
    QString latestProgressMsg;
    
    TIFF* tiff = nullptr;
    const unsigned char* buffer = nullptr;
    qint64 offset = 0;
    qint64 nbytes = 0;
    
    PageInfo imageInfo;
    
    std::vector<uint32_t> decodedImg;
    QImage::Format format;
    
    int imagePageToDecode = 0;
    
    Impl()
    {
        TIFFSetErrorHandlerExt(myErrorHandler);
        TIFFSetWarningHandlerExt(myWarningHandler);
    }
    
    ~Impl()
    {
        if (tiff)
        {
            TIFFClose(tiff);
        }
        tiff = nullptr;
    }
    
    static void myErrorHandler(thandle_t self, const char* module, const char* fmt, va_list ap)
    {
        auto impl = static_cast<Impl*>(self);
        char buf[4096];
        std::vsnprintf(buf, sizeof(buf)/sizeof(*buf), fmt, ap);

        Formatter f;
        if(module)
        {
            f << "Error in module '" << module << "': ";
        }
        f << buf;
        
        impl->latestProgressMsg = QString(f.str().c_str());
    }
    
    static void myWarningHandler(thandle_t self, const char* module, const char* fmt, va_list ap)
    {
        auto impl = static_cast<Impl*>(self);
        char buf[4096];
        std::vsnprintf(buf, sizeof(buf)/sizeof(*buf), fmt, ap);

        Formatter f;
        if(module)
        {
            f << "Warning in module '" << module << "': ";
        }
        f << buf;
        
        impl->latestProgressMsg = QString(f.str().c_str());
    }
    
    static void convert32BitOrder(uint32_t *target, uint32_t* src, quint32 rows, quint32 width)
    {
        // swap rows from bottom to top
        quint32 to = 0;
        for (quint32 r = rows; r>0; r--)
        {
            for (quint32 c=0; c<width; c++)
            {
                uint32_t p = src[(r-1)*width + c];
                // convert between ARGB and ABGR
                target[to++] = TIFFGetA(p) << 24 |
                               TIFFGetR(p) << 16 |
                               TIFFGetG(p) <<  8 |
                               TIFFGetB(p);
            }
        }
    }
    
    static tsize_t qtiffReadProc(thandle_t fd, tdata_t buf, tsize_t size)
    {
        auto impl = static_cast<Impl*>(fd);
        
        if(impl->offset >= impl->nbytes)
        {
            return 0;
        }
        else if(impl->offset + size < impl->nbytes)
        {
            
        }
        else
        {
            size = impl->nbytes - impl->offset;
        }
        
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

        if(impl->offset >= impl->nbytes)
        {
            return -1;
        }
        
        return impl->offset;
    }

    static int qtiffCloseProc(thandle_t /*fd*/)
    {
        return 0;
    }

    static toff_t qtiffSizeProc(thandle_t fd)
    {
        return static_cast<Impl*>(fd)->nbytes;
    }

    static int qtiffMapProc(thandle_t /*fd*/, tdata_t* /*pbase*/, toff_t* /*psize*/)
    {
        return 0;
    }

    static void qtiffUnmapProc(thandle_t /*fd*/, tdata_t /*base*/, toff_t /*size*/)
    {
    }

    std::vector<PageInfo> readPageInfos()
    {
        std::vector<PageInfo> pageInfos;
        do
        {
            pageInfos.emplace_back(PageInfo{});
            
            auto& info = pageInfos.back();
            
            if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &info.width) ||
                !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &info.height))
            {
                throw std::runtime_error("Error while reading TIFF dimensions");
            }
            
            if(!TIFFGetField(tiff, TIFFTAG_PLANARCONFIG, &info.config) ||
               !TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &info.bps) ||
               !TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &info.spp))
            {
               throw std::runtime_error("Error while reading TIFF tags");
            }
        } while(TIFFReadDirectory(tiff));
        
        return pageInfos;
    }

    int findHighestResolution(std::vector<PageInfo>& pageInfo)
    {
        int ret = 0;
        uint64_t res = 0;
        for(size_t i=0; i<pageInfo.size(); i++)
        {
            auto len = pageInfo[i].width * pageInfo[i].height;
            if(res < len)
            {
                ret = i;
                res = len;
            }
        }
        return ret;
    }
    
    int findThumbnailResolution(std::vector<PageInfo>& pageInfo)
    {
        int ret = -1;
        const auto fullImgAspect = this->imageInfo.width * 1.0 / this->imageInfo.height;
        uint64_t res = pageInfo[0].width * pageInfo[0].height;
        for(size_t i=0; i<pageInfo.size(); i++)
        {
            auto len = pageInfo[i].width * pageInfo[i].height;
            
            auto aspect = pageInfo[i].width * 1.0 / pageInfo[i].height;
            if(res > len && // current resolution smaller than previous?
               std::fabs(aspect - fullImgAspect) < 1e-4 && // aspect matches?
               (pageInfo[i].width >= 50 || pageInfo[i].height >= 50)) // at least 50 px in one dimension
            {
                ret = i;
                res = len;
            }
        }
        return ret;
    }
};

SmartTiffDecoder::SmartTiffDecoder(QString&& file) : SmartImageDecoder(std::move(file)), d(std::make_unique<Impl>())
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
            return QSize(d->imageInfo.width, d->imageInfo.height);
        default:
            throw std::logic_error(Formatter() << "Wrong DecodingState: " << s);
    }
}

void SmartTiffDecoder::decodeHeader()
{
    this->fileBuf(&d->buffer, &d->nbytes);
    
    emit this->decodingProgress(this, 0, "Reading TIFF Header");
    
    d->tiff = TIFFClientOpen("foo",
                            "rm",
                            d.get(),
                            d->qtiffReadProc,
                            d->qtiffWriteProc,
                            d->qtiffSeekProc,
                            d->qtiffCloseProc,
                            d->qtiffSizeProc,
                            d->qtiffMapProc,
                            d->qtiffUnmapProc);
                            
    if(d->tiff == nullptr)
    {
        throw std::runtime_error("TIFFClientOpen() failed");
    }
    
    std::vector<PageInfo> pageInfos = d->readPageInfos();
    
    d->imagePageToDecode = d->findHighestResolution(pageInfos);
    d->imageInfo = pageInfos[d->imagePageToDecode];
    d->format = QImage::Format_RGB32;
    
    auto thumbnailPageToDecode = d->findThumbnailResolution(pageInfos);
    if(thumbnailPageToDecode >= 0)
    {
        this->d->latestProgressMsg = (Formatter() << "Decoding TIFF thumbnail found at directory no. " << thumbnailPageToDecode).str().c_str();
        emit this->decodingProgress(this, 0, this->d->latestProgressMsg);
        //TODO
    //    d->decodeImage(thumbnailPageToDecode);
    }
}

void SmartTiffDecoder::decodingLoop(DecodingState targetState)
{
    const size_t width = d->imageInfo.width;
    const size_t height = d->imageInfo.height;

    try
    {
        d->decodedImg.resize(width * height);
        this->setDecodingState(DecodingState::PreviewImage);
    }
    catch(const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << width * height /1024. /1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
    }
    
    TIFFSetDirectory(d->tiff, d->imagePageToDecode);

    this->d->latestProgressMsg = (Formatter() << "Decoding TIFF image at directory no. " << d->imagePageToDecode).str().c_str();
    emit this->decodingProgress(this, 0, this->d->latestProgressMsg);
    
    size_t rowStride = width * sizeof(uint32_t);
    auto* buf = d->decodedImg.data();
    if(TIFFIsTiled(d->tiff))
    {
        throw std::runtime_error("Tiled TIFFs are not supported currently");
    }
    else
    {
        uint32_t rowsperstrip;
        TIFFGetFieldDefaulted(d->tiff, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);
        
        std::vector<uint32_t> stripBuf(width * rowsperstrip);
        
        const auto stripCount = TIFFNumberOfStrips(d->tiff);
        for (tstrip_t strip = 0; strip < stripCount; strip++)
        {
            auto rowsDecoded = std::min<size_t>(rowsperstrip, height - strip * rowsperstrip);
            auto ret = TIFFReadRGBAStrip(d->tiff, strip * rowsperstrip, stripBuf.data());
            if(ret == 0)
            {
                throw std::runtime_error("Error while TIFFReadRGBAStrip");
            }
            else
            {
                d->convert32BitOrder(buf, stripBuf.data(), rowsDecoded, width);
                emit this->imageRefined(QImage(reinterpret_cast<uint8_t*>(d->decodedImg.data()),
                                        width,
                                        std::min<size_t>(strip * rowsperstrip, height),
                                        rowStride,
                                        d->format));
            }
            
            double progress = strip * 100.0 / stripCount;
            emit this->decodingProgress(this, progress, d->latestProgressMsg);

            buf += rowsDecoded * width;
            this->cancelCallback();
        }
    }

    QImage image(reinterpret_cast<uint8_t*>(d->decodedImg.data()), width, height, rowStride, d->format);
    
    float resX = 0;
    float resY = 0;
    uint16_t resUnit;
    if (!TIFFGetField(d->tiff, TIFFTAG_RESOLUTIONUNIT, &resUnit))
        resUnit = RESUNIT_INCH;

    if (TIFFGetField(d->tiff, TIFFTAG_XRESOLUTION, &resX)
        && TIFFGetField(d->tiff, TIFFTAG_YRESOLUTION, &resY))
    {
        switch(resUnit)
        {
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

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    uint32_t count;
    void *profile;
    if (TIFFGetField(d->tiff, TIFFTAG_ICCPROFILE, &count, &profile))
    {
        QByteArray iccProfile(reinterpret_cast<const char *>(profile), count);
        image.setColorSpace(QColorSpace::fromIccProfile(iccProfile));
    }
#endif
    
    this->setDecodingState(DecodingState::PreviewImage);
    
    this->setImage(std::move(image));
    
    d->latestProgressMsg = "TIFF decoding completed successfully.";
    emit this->decodingProgress(this, 100, d->latestProgressMsg);
}
