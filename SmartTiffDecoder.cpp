
#include "SmartTiffDecoder.hpp"
#include "Formatter.hpp"

#include <cstdio>
#include <cmath>
#include <QDebug>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#include <QColorSpace>
#endif

#include "tiff.h"
#include "tiffio.h"

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
    SmartTiffDecoder* q;
    
    TIFF* tiff = nullptr;
    const unsigned char* buffer = nullptr;
    qint64 offset = 0;
    qint64 nbytes = 0;
    
    PageInfo imageInfo;
    
    QImage::Format format;
    
    int imagePageToDecode = 0;
    
    Impl(SmartTiffDecoder* q) : q(q)
    {
        TIFFSetErrorHandler(nullptr);
        TIFFSetWarningHandler(nullptr);
        TIFFSetErrorHandlerExt(myErrorHandler);
        TIFFSetWarningHandlerExt(myWarningHandler);
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
        
        impl->q->setDecodingMessage(QString(f.str().c_str()));
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
        
        impl->q->setDecodingMessage(QString(f.str().c_str()));
    }
    
    static void convert32BitOrder(uint32_t *__restrict target, uint32_t *__restrict src, quint32 rows, quint32 width)
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

    static tsize_t qtiffWriteProc(thandle_t, tdata_t, tsize_t)
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

SmartTiffDecoder::SmartTiffDecoder(const QFileInfo& file, QByteArray arr) : SmartImageDecoder(file, arr), d(std::make_unique<Impl>(this))
{}

SmartTiffDecoder::~SmartTiffDecoder() = default;

void SmartTiffDecoder::close()
{
    if (d->tiff)
    {
        TIFFClose(d->tiff);
    }
    d->tiff = nullptr;
    d->buffer = nullptr;
    
    SmartImageDecoder::close();
}

void SmartTiffDecoder::decodeHeader(const unsigned char* buffer, qint64 nbytes)
{
    d->buffer = buffer;
    d->nbytes = nbytes;
    d->offset = 0;
    
    this->setDecodingMessage("Reading TIFF Header");
    
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
    
    this->setDecodingMessage("Parsing TIFF Image Directories");
    
    std::vector<PageInfo> pageInfos = d->readPageInfos();
    
    d->imagePageToDecode = d->findHighestResolution(pageInfos);
    d->imageInfo = pageInfos[d->imagePageToDecode];
    d->format = QImage::Format_ARGB32;
 
    this->setSize(QSize(d->imageInfo.width, d->imageInfo.height));
    
    auto thumbnailPageToDecode = d->findThumbnailResolution(pageInfos);
    if(thumbnailPageToDecode >= 0)
    {
        this->setDecodingMessage((Formatter() << "Decoding TIFF thumbnail found at directory no. " << thumbnailPageToDecode).str().c_str());
        qInfo() << (Formatter() << "Decoding TIFF thumbnail found at directory no. " << thumbnailPageToDecode).str().c_str();
        
        QImage thumb(pageInfos[thumbnailPageToDecode].width, pageInfos[thumbnailPageToDecode].height, d->format);
        this->decodeInternal(thumbnailPageToDecode, thumb, true);
        this->setThumbnail(thumb);
    }
}

QImage SmartTiffDecoder::decodingLoop(DecodingState)
{
    const size_t width = d->imageInfo.width;
    const size_t height = d->imageInfo.height;

    uint32_t* mem = this->allocateImageBuffer<uint32_t>(width, height);
    QImage image(reinterpret_cast<uint8_t*>(mem), width, height, d->format);
    this->decodeInternal(d->imagePageToDecode, image, false);
    return image;
}

void SmartTiffDecoder::decodeInternal(int imagePageToDecode, QImage& image, bool silent)
{
    const unsigned width = image.width();
    const unsigned height = image.height();
    
    TIFFSetDirectory(d->tiff, imagePageToDecode);

    if(!silent)
    {
        this->setDecodingMessage((Formatter() << "Decoding TIFF image at directory no. " << imagePageToDecode).str().c_str());
    }
    
    const size_t rowStride = width * sizeof(uint32_t);
    if(TIFFIsTiled(d->tiff))
    {
        uint32_t* mem = reinterpret_cast<uint32_t*>(image.bits());
        auto* buf = mem;
        
        uint32_t tw,tl;
        TIFFGetField(d->tiff, TIFFTAG_TILEWIDTH, &tw);
        TIFFGetField(d->tiff, TIFFTAG_TILELENGTH, &tl);
    
        std::vector<uint32_t> tileBuf(tw * tl);
        
        for (unsigned y = 0; y < height; y += tl)
        {
            for (unsigned x = 0; x < width; x += tw)
            {
                auto ret = TIFFReadRGBATile(d->tiff, x, y, tileBuf.data());
                if(ret == 0)
                {
                    throw std::runtime_error("Error while TIFFReadRGBATile");
                }
                else
                {
                    unsigned linesToCopy = std::min(tl, height - y);
                    unsigned widthToCopy = std::min(tw, width - x);
                    for (unsigned i = 0; i < linesToCopy; i++)
                    {
                        // brainfuck ahead...
                        d->convert32BitOrder(&buf[(y+i)*width + x], &tileBuf[(linesToCopy-i-1)*tw], 1, widthToCopy);
                    }
                    
                    this->updatePreviewImage(QImage(reinterpret_cast<const uint8_t*>(mem),
                                            width,
                                            height,
                                            width * sizeof(uint32_t),
                                            d->format));
                    
                    double progress = y * x * 100.0 / (height * width);
                    this->setDecodingProgress(progress);
                }
            }
        }
    }
    else
    {
        uint32_t rowsperstrip;
        if(!TIFFGetField(d->tiff, TIFFTAG_ROWSPERSTRIP, &rowsperstrip))
        {
            throw std::runtime_error("Failed to read RowsPerStip. Not a TIFF file?");
        }
        
        std::vector<uint32_t> stripBuf(width * rowsperstrip);

        uint32_t* mem = reinterpret_cast<uint32_t*>(image.bits());
        auto* buf = mem;
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
                
                if(!silent)
                {
                    this->updatePreviewImage(QImage(reinterpret_cast<const uint8_t*>(mem),
                                            width,
                                            std::min<size_t>(strip * rowsperstrip, height),
                                            rowStride,
                                            d->format));
                }
            }
            
            if(!silent)
            {
                double progress = strip * 100.0 / stripCount;
                this->setDecodingProgress(progress);
            }
            
            buf += rowsDecoded * width;
            this->cancelCallback();
        }
    }

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
    
    if(!silent)
    {
        this->setDecodingProgress(100);
        this->setDecodingMessage("TIFF decoding completed successfully.");
    }
}
