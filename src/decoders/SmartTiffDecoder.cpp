
#include "SmartTiffDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
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
    
    size_t nPix()
    {
        return static_cast<size_t>(this->width) * height;
    }
};

constexpr const char TiffModule[] = "SmartTiffDecoder";

// Note: a lot of the code has been taken from: 
// https://github.com/qt/qtimageformats/blob/c64f19516dd2467bf5746eb24afe883bdbc15b25/src/plugins/imageformats/tiff/qtiffhandler.cpp

struct SmartTiffDecoder::Impl
{
    SmartTiffDecoder* q;
    
    TIFF* tiff = nullptr;
    const unsigned char* buffer = nullptr;
    qint64 offset = 0;
    qint64 nbytes = 0;
    
    std::vector<PageInfo> pageInfos;

    Impl(SmartTiffDecoder* q) : q(q)
    {
        // those are nasty global, non-thread-safe functions. setting custom handlers will also affect QT's internal QImage decoding.
        TIFFSetErrorHandler(nullptr);
        TIFFSetWarningHandler(nullptr);
        TIFFSetErrorHandlerExt(myErrorHandler);
        TIFFSetWarningHandlerExt(myWarningHandler);
    }
    
    static void myErrorHandler(thandle_t self, const char* module, const char* fmt, va_list ap)
    {
        if(::strcmp(module, TiffModule) != 0)
        {
            // Seems like this call was made from QT's internal decoding. self points to something else. Return.
            return;
        }
        
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
        if(::strcmp(module, TiffModule) != 0)
        {
            // Seems like this call was made from QT's internal decoding. self points to something else. Return.
            return;
        }
        
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
    
    size_t convert32BitOrder(uint32_t *__restrict dst, const uint32_t *__restrict src, quint32 rows, quint32 width)
    {
        // swap rows from bottom to top
        size_t to = 0;
        for (qint64 r = rows; r>0; r--)
        {
            for (quint32 c=0; c<width; c++)
            {
                uint32_t p = src[(r-1)*width + c];
                // convert between ARGB and ABGR
                dst[to++] = TIFFGetA(p) << 24 |
                            TIFFGetR(p) << 16 |
                            TIFFGetG(p) <<  8 |
                            TIFFGetB(p);
            }
            q->cancelCallback();
        }
        
        return to;
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
        auto currentDirectory = TIFFCurrentDirectory(this->tiff);
        
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
        
        
        if(!TIFFSetDirectory(this->tiff, currentDirectory))
        {
            throw std::runtime_error("This should never happen: TIFFSetDirectory failed to restore previous directory");
        }
        
        return pageInfos;
    }

    static int findHighestResolution(std::vector<PageInfo>& pageInfo)
    {
        int ret = -1;
        uint64_t res = 0;
        for(size_t i=0; i<pageInfo.size(); i++)
        {
            auto len = pageInfo[i].nPix();
            if(res < len)
            {
                ret = i;
                res = len;
            }
        }
        return ret;
    }
    
    static int findThumbnailResolution(std::vector<PageInfo>& pageInfo, QSize size)
    {
        int ret = -1;
        const auto fullImgAspect = size.width() * 1.0 / size.height();
        uint64_t res = size.width() * size.height();
        for(size_t i=0; i<pageInfo.size(); i++)
        {
            auto len = pageInfo[i].nPix();
            
            auto aspect = pageInfo[i].width * 1.0 / pageInfo[i].height;
            if(res > len && // current resolution smaller than previous?
               std::fabs(aspect - fullImgAspect) < 0.1 && // aspect matches?
               // we do not have a suitable page yet, ensure to not pick every page, i.e. it should be smaller than the double of MaxIconHeight
               (ret == -1 && !(pageInfo[i].width >= ANPV::MaxIconHeight*2 || pageInfo[i].height >= ANPV::MaxIconHeight*2)
               // if we do have a suitable make, make sure it's one bigger 200px
               || (pageInfo[i].width >= 200 || pageInfo[i].height >= 200)))
            {
                ret = i;
                res = len;
            }
        }
        return ret;
    }
    
    QImage::Format format(int page)
    {
        return QImage::Format_ARGB32;
    }
    
    static int findSuitablePage(std::vector<PageInfo>& pageInfo, double targetScale, QSize size)
    {
        int ret = -1;
        double prevScale = 1;
        
        for(size_t i=0; i<pageInfo.size(); i++)
        {
            double scale  = size.width() / pageInfo[i].width;
            if(scale <= targetScale && scale >= prevScale)
            {
                ret = i;
                prevScale = scale;
            }
        }
        return ret;
    }
};

SmartTiffDecoder::SmartTiffDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>(this))
{}

SmartTiffDecoder::~SmartTiffDecoder()
{
    this->assertNotDecoding();
}

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
    
    d->tiff = TIFFClientOpen(TiffModule,
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
    
    d->pageInfos = d->readPageInfos();
    auto highResPage = d->findHighestResolution(d->pageInfos);
    if(highResPage < 0)
    {
        throw std::runtime_error("This TIFF doesn't contain any directories!");
    }

    uint32_t count;
    void *profile;
    QColorSpace cs{QColorSpace::SRgb};
    if (TIFFGetField(d->tiff, TIFFTAG_ICCPROFILE, &count, &profile))
    {
        QByteArray iccProfile(reinterpret_cast<const char *>(profile), count);
        cs = QColorSpace::fromIccProfile(iccProfile);
    }
    
    QSize size(d->pageInfos[highResPage].width, d->pageInfos[highResPage].height);
    this->image()->setSize(size);
    this->image()->setColorSpace(cs);
    auto thumbnailPageToDecode = d->findThumbnailResolution(d->pageInfos, size);
    
    if(thumbnailPageToDecode >= 0)
    {
        this->setDecodingMessage((Formatter() << "Decoding TIFF thumbnail found at directory no. " << thumbnailPageToDecode).str().c_str());
        
        try
        {
            QImage thumb(d->pageInfos[thumbnailPageToDecode].width, d->pageInfos[thumbnailPageToDecode].height, d->format(thumbnailPageToDecode));
            this->decodeInternal(thumbnailPageToDecode, thumb, QRect(), 1, thumb.size());

            this->image()->setThumbnail(thumb);
        }
        catch(const std::exception& e)
        {
            QString err = QStringLiteral(
                "'%1' has a thumbnail at directory no. %2. However, an error occurred while trying to decode it: "
                "'%3'").arg(this->image()->fileInfo().fileName()).arg(QString::number(thumbnailPageToDecode)).arg(e.what());
            qWarning() << err;
            this->setDecodingMessage(std::move(err));
        }
    }
}

QImage SmartTiffDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    const QRect fullImageRect(QPoint(0,0), this->image()->size());
    
    QRect targetImageRect = fullImageRect;
    if(roiRect.isValid())
    {
        targetImageRect = targetImageRect.intersected(roiRect);
    }
    
    if(!desiredResolution.isValid())
    {
        desiredResolution = targetImageRect.size();
    }
    
    QSize desiredDecodeResolution = targetImageRect.size().scaled(desiredResolution, Qt::KeepAspectRatio);
    desiredDecodeResolution *= 1.5;
    // the desiredDecodeResolution should not be bigger than the targetImageRect
    desiredDecodeResolution = desiredDecodeResolution.boundedTo(targetImageRect.size());
    
    double desiredScaleX = targetImageRect.width() * 1.0f / desiredDecodeResolution.width();
    double desiredScaleY = targetImageRect.height() * 1.0f / desiredDecodeResolution.height();

    int imagePageToDecode = d->findSuitablePage(d->pageInfos, desiredScaleX, fullImageRect.size());

    double actualPageScaleXInverted = d->pageInfos[imagePageToDecode].width * 1.0f / fullImageRect.width();
    double actualPageScaleYInverted = d->pageInfos[imagePageToDecode].height * 1.0f / fullImageRect.height();
    
    QTransform scaleTrafo = QTransform::fromScale(actualPageScaleXInverted, actualPageScaleYInverted);
    QRect mappedRoi = scaleTrafo.mapRect(targetImageRect);
    
    uint32_t* mem = this->allocateImageBuffer<uint32_t>(d->pageInfos[imagePageToDecode].width, d->pageInfos[imagePageToDecode].height);
    QImage image(reinterpret_cast<uint8_t*>(mem), d->pageInfos[imagePageToDecode].width, d->pageInfos[imagePageToDecode].height, d->format(imagePageToDecode));
    this->decodeInternal(imagePageToDecode, image, mappedRoi, desiredScaleX, desiredResolution);

    return image;
}

void SmartTiffDecoder::decodeInternal(int imagePageToDecode, QImage& image, QRect roi, double desiredDecodeScale, QSize desiredResolution)
{
    const unsigned width = d->pageInfos[imagePageToDecode].width;
    const unsigned height = d->pageInfos[imagePageToDecode].height;
    
    if(!roi.isValid())
    {
        // roi's coordinates are native to imagePageToDecode
        roi = QRect(0, 0, width, height);
    }
    
    TIFFSetDirectory(d->tiff, imagePageToDecode);

    uint16_t samplesPerPixel = d->pageInfos[imagePageToDecode].spp;
    uint16_t bitsPerSample = d->pageInfos[imagePageToDecode].bps;
        
    uint16_t comp;
    TIFFGetField(d->tiff, TIFFTAG_COMPRESSION, &comp);
    
    uint16_t planar;
    TIFFGetField(d->tiff, TIFFTAG_PLANARCONFIG, &planar);
    
    this->setDecodingMessage((Formatter() << "Decoding TIFF image at directory no. " << imagePageToDecode).str().c_str());
    
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
                QRect tile(x,y,tw,tl);
                if(!tile.intersects(roi))
                {
                    continue;
                }
            
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
                        d->convert32BitOrder(&buf[(y+i)*width + x], &tileBuf[(tl-i-1)*tw], 1, widthToCopy);
                    }
                    
                    this->updatePreviewImage(QImage(reinterpret_cast<const uint8_t*>(mem),
                                            width,
                                            height,
                                            width * sizeof(uint32_t),
                                            d->format(imagePageToDecode)));
                    
                    double progress = (y * tw + x) * 100.0 / d->pageInfos[imagePageToDecode].nPix();
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
        
        const auto stripCount = TIFFNumberOfStrips(d->tiff);
        
        if(comp == COMPRESSION_NONE &&
            samplesPerPixel == 4 /* RGBA */ &&
            planar == 1 &&
            bitsPerSample==8)
        {
            // image is uncompressed, use a shortcut for quick displaying
            
            const auto stripCount = TIFFNumberOfStrips(d->tiff);
            
            uint64_t *stripOffset = nullptr;
            TIFFGetField(d->tiff, TIFFTAG_STRIPOFFSETS, &stripOffset);
            
            if(stripCount == 0)
            {
                throw std::runtime_error("This should never happen: TIFFNumberOfStrips() returned zero??");
            }
            
            auto initialOffset = stripOffset[0];
            if(stripCount >= 2)
            {
                auto stripLen = stripOffset[1] - initialOffset;
                size_t nOffsets = stripCount;
                for(size_t s = 2; s< nOffsets;s++)
                {
                    if(stripOffset[s] != stripLen * s + initialOffset)
                    {
                        this->setDecodingMessage("TIFF Strips are not contiguous. Cannot use fast decoding hack. Trying regular, slow decoding instead.");
                        goto gehtnich;
                    }
                }
            }
            
            this->setDecodingMessage("Uh, it's an uncompressed 8-bit RGBA TIFF. Using fast decoding hack. This may take a few seconds and cannot be cancelled... ");
            
            const uint8_t* rawRgb = d->buffer + initialOffset;
            
            QImage rawImage(rawRgb,
                            width,
                            height,
                            width * samplesPerPixel,
                            QImage::Format_RGBA8888);
            image = rawImage.scaled(roi.size() / desiredDecodeScale, Qt::KeepAspectRatio, Qt::FastTransformation);
            image = image.scaled(desiredResolution, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        else
        {
gehtnich:
            uint32_t* mem = reinterpret_cast<uint32_t*>(image.bits());
            auto* buf = mem;
            
            std::vector<uint32_t> stripBuf(width * rowsperstrip);
            std::vector<uint32_t> stripBufUncrustified(width * rowsperstrip);
            for (tstrip_t strip = 0; strip < stripCount; strip++)
            {
                uint32_t rowsDecoded = std::min<size_t>(rowsperstrip, height - strip * rowsperstrip);
                auto ret = TIFFReadRGBAStrip(d->tiff, strip * rowsperstrip, stripBuf.data());
                if(ret == 0)
                {
                    throw std::runtime_error("Error while TIFFReadRGBAStrip");
                }
                else
                {
                    d->convert32BitOrder(stripBufUncrustified.data(), stripBuf.data(), rowsDecoded, width);

                    QImage stripImg(reinterpret_cast<uint8_t*>(stripBufUncrustified.data()),
                                    width,
                                    rowsDecoded,
                                    width * sizeof(uint32_t),
                                    d->format(imagePageToDecode));
                    stripImg = stripImg.scaledToWidth(image.width(), Qt::FastTransformation);
                    
                    size_t pixelsToCpy = stripImg.width() * stripImg.height();
                    ::memcpy(buf, stripImg.constBits(), pixelsToCpy * sizeof(uint32_t));
                    
                    buf += pixelsToCpy;
                    
                    this->updatePreviewImage(QImage(reinterpret_cast<const uint8_t*>(mem),
                                            width,
                                            std::min(strip*rowsperstrip, height),
                                            width * sizeof(uint32_t),
                                            d->format(imagePageToDecode)));
                    
                    double progress = strip * 100.0 / stripCount;
                    this->setDecodingProgress(progress);
                }
            }
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

    image.setColorSpace(this->image()->colorSpace());
    this->setDecodingMessage("Transforming colorspace...");
    image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));

    this->setDecodingMessage("TIFF decoding completed successfully.");
    this->setDecodingProgress(100);
}
