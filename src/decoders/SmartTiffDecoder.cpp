
#include "SmartTiffDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <cstring>
#include <QDebug>
#include <QColorSpace>

#include "tiff.h"
#include "tiffio.h"

struct PageInfo
{
    uint32_t width;
    uint32_t height;
    // default config is chunky
    uint16_t config = 1;
    // bits per sample, default is bool??
    uint16_t bps = 1;
    // sample per pixel, default is gray
    uint16_t spp = 1;

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
    SmartTiffDecoder *q;

    TIFF *tiff = nullptr;
    const unsigned char *buffer = nullptr;
    qint64 offset = 0;
    qint64 nbytes = 0;

    std::vector<PageInfo> pageInfos;
    QPainterPath debugTiffLayout;

    Impl(SmartTiffDecoder *q) : q(q)
    {
        // those are nasty global, non-thread-safe functions. setting custom handlers will also affect QT's internal QImage decoding.
        TIFFSetErrorHandler(nullptr);
        TIFFSetWarningHandler(nullptr);
        TIFFSetErrorHandlerExt(myErrorHandler);
        TIFFSetWarningHandlerExt(myWarningHandler);
    }

    static void myErrorHandler(thandle_t self, const char *module, const char *fmt, va_list ap)
    {
        if(::strcmp(module, TiffModule) != 0)
        {
            // Seems like this call was made from QT's internal decoding. self points to something else. Return.
            return;
        }

        auto impl = static_cast<Impl *>(self);
        char buf[4096];
        std::vsnprintf(buf, sizeof(buf) / sizeof(*buf), fmt, ap);

        Formatter f;

        if(module)
        {
            f << "Error in module '" << module << "': ";
        }

        f << buf;

        impl->q->setDecodingMessage(QString(f.str().c_str()));
    }

    static void myWarningHandler(thandle_t self, const char *module, const char *fmt, va_list ap)
    {
        if(::strcmp(module, TiffModule) != 0)
        {
            // Seems like this call was made from QT's internal decoding. self points to something else. Return.
            return;
        }

        auto impl = static_cast<Impl *>(self);
        char buf[4096];
        std::vsnprintf(buf, sizeof(buf) / sizeof(*buf), fmt, ap);

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

        for(qint64 r = rows; r > 0; r--)
        {
            for(quint32 c = 0; c < width; c++)
            {
                uint32_t p = src[(r - 1) * width + c];
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
        auto impl = static_cast<Impl *>(fd);

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
        auto impl = static_cast<Impl *>(fd);

        switch(whence)
        {
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
        return static_cast<Impl *>(fd)->nbytes;
    }

    static int qtiffMapProc(thandle_t /*fd*/, tdata_t * /*pbase*/, toff_t * /*psize*/)
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

            auto &info = pageInfos.back();

            if(!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &info.width) ||
                    !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &info.height))
            {
                throw std::runtime_error("Error while reading TIFF dimensions");
            }

            // these tags are optional, they may or may not be available
            TIFFGetField(tiff, TIFFTAG_PLANARCONFIG, &info.config);
            TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &info.bps);
            TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &info.spp);
        }
        while(TIFFReadDirectory(tiff));


        if(!TIFFSetDirectory(this->tiff, currentDirectory))
        {
            throw std::runtime_error("This should never happen: TIFFSetDirectory failed to restore previous directory");
        }

        return pageInfos;
    }

    static int findHighestResolution(std::vector<PageInfo> &pageInfo)
    {
        int ret = -1;
        uint64_t res = 0;

        for(size_t i = 0; i < pageInfo.size(); i++)
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

    static int findThumbnailResolution(std::vector<PageInfo> &pageInfo, int highResPage)
    {
        int ret = -1;
        const auto fullImgAspect = pageInfo[highResPage].width * 1.0 / pageInfo[highResPage].height;
        auto res = pageInfo[highResPage].nPix();

        for(size_t i = 0; i < pageInfo.size(); i++)
        {
            auto len = pageInfo[i].nPix();

            auto aspect = pageInfo[i].width * 1.0 / pageInfo[i].height;

            if(res > len && // current resolution smaller than previous?
                    std::fabs(aspect - fullImgAspect) < 0.1 && // aspect matches?
                    // we do not have a suitable page yet, ensure to not pick every page, i.e. it should be smaller than the double of MaxIconHeight
                    ((ret == -1 && !(pageInfo[i].width >= ANPV::MaxIconHeight * 2 || pageInfo[i].height >= ANPV::MaxIconHeight * 2))
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
        // The zero initialized, not-yet-decoded image buffer should be displayed transparently. Therefore, always use ARGB, even if this
        // would cause a performance drawback for images which do not have one, because Qt may call QPixmap::mask() internally.
        return QImage::Format_ARGB32;
    }

    static int findSuitablePage(std::vector<PageInfo> &pageInfo, double targetScale, QSize size)
    {
        int ret = -1;
        double prevScale = 1;

        for(size_t i = 0; i < pageInfo.size(); i++)
        {
            double scale = size.width() / pageInfo[i].width;

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
    if(d->tiff)
    {
        TIFFClose(d->tiff);
    }

    d->tiff = nullptr;
    d->buffer = nullptr;

    SmartImageDecoder::close();
}

void SmartTiffDecoder::decodeHeader(const unsigned char *buffer, qint64 nbytes)
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

    if(TIFFGetField(d->tiff, TIFFTAG_ICCPROFILE, &count, &profile))
    {
        QByteArray iccProfile = QByteArray::fromRawData(reinterpret_cast<const char *>(profile), count);
        cs = QColorSpace::fromIccProfile(iccProfile);
    }

    this->image()->setSize(QSize(d->pageInfos[highResPage].width, d->pageInfos[highResPage].height));
    this->image()->setColorSpace(cs);
    auto thumbnailPageToDecode = d->findThumbnailResolution(d->pageInfos, highResPage);

    if(thumbnailPageToDecode >= 0)
    {
        this->setDecodingMessage((Formatter() << "Decoding TIFF thumbnail found at directory no. " << thumbnailPageToDecode).str().c_str());

        try
        {
            QImage thumb(d->pageInfos[thumbnailPageToDecode].width, d->pageInfos[thumbnailPageToDecode].height, d->format(thumbnailPageToDecode));
            this->decodeInternal(thumbnailPageToDecode, thumb, QRect(), QTransform(), thumb.size(), true);

            this->convertColorSpace(thumb, true);
            this->image()->setThumbnail(thumb);
        }
        catch(const std::exception &e)
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
    const QRect fullImageRect = this->image()->fullResolutionRect();

    QRect targetImageRect = fullImageRect;

    if(!roiRect.isEmpty())
    {
        QRect intersect = targetImageRect.intersected(roiRect);

        if(!intersect.isEmpty())
        {
            targetImageRect = intersect;
        }
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

    int imagePageToDecode = d->findSuitablePage(d->pageInfos, desiredScaleX, fullImageRect.size());

    if(imagePageToDecode < 0)
    {
        throw std::runtime_error("Unable to find a suitable TIFF directory to decode.");
    }

    QTransform scaleTrafo = this->fullResToPageTransform(d->pageInfos[imagePageToDecode].width, d->pageInfos[imagePageToDecode].height);
    QRect mappedRoi = scaleTrafo.mapRect(targetImageRect);

    QImage image = this->allocateImageBuffer(mappedRoi.size(), d->format(imagePageToDecode));

    // RESOLUTIONUNIT must be read and set now (and not in decodeInternal), because QImage::setDotsPerMeterXY() calls detach() and therefore copies the entire image!!!
    float resX = 0;
    float resY = 0;
    uint16_t resUnit;

    if(!TIFFGetField(d->tiff, TIFFTAG_RESOLUTIONUNIT, &resUnit))
    {
        resUnit = RESUNIT_INCH;
    }

    if(TIFFGetField(d->tiff, TIFFTAG_XRESOLUTION, &resX)
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

    image.setOffset(roiRect.topLeft());

    QTransform toFullScaleTransform = scaleTrafo.inverted();
    this->image()->setDecodedImage(image, toFullScaleTransform);
    d->debugTiffLayout.clear();
    this->decodeInternal(imagePageToDecode, image, mappedRoi, toFullScaleTransform, desiredResolution, false);
    this->convertColorSpace(image, false, toFullScaleTransform);

    bool fullImageDecoded = (imagePageToDecode == d->findHighestResolution(d->pageInfos)); // We have decoded the highest resolution available
    fullImageDecoded &= ((unsigned)image.width() >= d->pageInfos[imagePageToDecode].width && (unsigned)image.height() >= d->pageInfos[imagePageToDecode].height); // we have not used the fast decoding hack
    fullImageDecoded &= (this->decodedRoiRect() == fullImageRect); // the region we've decoded actually matches the region of the full image

    if(fullImageDecoded)
    {
        this->setDecodingState(DecodingState::FullImage);
    }
    else
    {
        this->setDecodingState(DecodingState::PreviewImage);
    }

    return image;
}

void SmartTiffDecoder::decodeInternal(int imagePageToDecode, QImage &image, QRect roi, QTransform currentPageToFullResTransform, QSize desiredResolution, bool quiet)
{
    const auto &width = d->pageInfos[imagePageToDecode].width;
    const auto &height = d->pageInfos[imagePageToDecode].height;

    if(!roi.isValid())
    {
        // roi's coordinates are native to imagePageToDecode
        roi = QRect(0, 0, width, height);
    }

    Q_ASSERT(roi.size() == image.size());

    TIFFSetDirectory(d->tiff, imagePageToDecode);

    uint16_t samplesPerPixel = d->pageInfos[imagePageToDecode].spp;
    uint16_t bitsPerSample = d->pageInfos[imagePageToDecode].bps;

    // default is no compression
    uint16_t comp = 1;
    TIFFGetField(d->tiff, TIFFTAG_COMPRESSION, &comp);

    if(!TIFFIsCODECConfigured(comp))
    {
        throw std::runtime_error(Formatter() << "Codec " << (int)comp << " is not supported by libtiff");
    }

#if 0
    uint16_t planar;
    TIFFGetField(d->tiff, TIFFTAG_PLANARCONFIG, &planar);
#endif
    this->setDecodingMessage((Formatter() << "Decoding TIFF image at directory no. " << imagePageToDecode).str().c_str());

    auto *dataPtrBackup = image.constBits();
    uint32_t *buf = const_cast<uint32_t *>(reinterpret_cast<const uint32_t *>(dataPtrBackup));

    if(TIFFIsTiled(d->tiff))
    {
        uint32_t tw, tl;

        if(!TIFFGetField(d->tiff, TIFFTAG_TILEWIDTH, &tw) || !TIFFGetField(d->tiff, TIFFTAG_TILELENGTH, &tl))
        {
            throw std::runtime_error("Failed to read tile size");
        }

        std::vector<uint32_t> tileBuf(tw * tl);

        unsigned destRowIncr = 0;

        for(uint32_t y = 0, destRow = 0; y < height; y += tl, destRow += destRowIncr)
        {
            for(uint32_t x = 0, destCol = 0; x < width; x += tw)
            {
                const unsigned linesToCopy = std::min(tl, height - y);
                const unsigned widthToCopy = std::min(tw, width - x);
                QRect tileRect(x, y, widthToCopy, linesToCopy);

                QRect areaToCopy = tileRect.intersected(roi);
                if(areaToCopy.isEmpty())
                {
                    continue;
                }

                d->debugTiffLayout.addRect(currentPageToFullResTransform.mapRect(tileRect));

                auto ret = TIFFReadRGBATile(d->tiff, x, y, tileBuf.data());

                if(ret == 0)
                {
                    throw std::runtime_error("Error while TIFFReadRGBATile");
                }
                else
                {
                    const unsigned linesToSkipFromTop = y < areaToCopy.y() ? areaToCopy.y() - y : 0;
                    const unsigned widthToSkipFromLeft = x < areaToCopy.x() ? areaToCopy.x() - x : 0;

                    for(unsigned i = 0; i < (unsigned)areaToCopy.height(); i++)
                    {
                        // brainfuck ahead...
                        // determine the destinationRow to write to, make it size_t to avoid 32bit overflow for panorama images when multiplying by image.width() below
                        size_t dr = destRow + i;
                        // the source row to read from, we need to start from the bottom (i.e. last pixel row of the tile), -1 because tl is a size but we need an index
                        unsigned srcRow = tl - 1 - (i + linesToSkipFromTop);
                        // the source column to read from, if a tile intersects to the left of areaToCopy, we need to skip widthToSkip pixels, if a tile intersects at the right, we start with with the first pixel
                        unsigned srcCol = widthToSkipFromLeft;
                        d->convert32BitOrder(&buf[dr * image.width() + destCol], &tileBuf[srcRow * tw + srcCol], 1, areaToCopy.width());
                    }

                    destCol += areaToCopy.width();
                    destRowIncr = areaToCopy.height();

                    if(!quiet)
                    {
                        this->updateDecodedRoiRect(areaToCopy);

                        double progress = (y * tw + x) * 100.0 / d->pageInfos[imagePageToDecode].nPix();
                        this->setDecodingProgress(progress);
                    }
                }
            }
        }

        Q_ASSERT(image.constBits() == dataPtrBackup);
    }
    else
    {
        uint32_t rowsperstrip;

        if(!TIFFGetField(d->tiff, TIFFTAG_ROWSPERSTRIP, &rowsperstrip))
        {
            // intentionally fail and do not rely on the default value here, to get a predetermined breaking point for e.g. Canon RAWs
            throw std::runtime_error("Failed to read RowsPerStip. Not a TIFF file?");
        }

        const auto stripCount = TIFFNumberOfStrips(d->tiff);
#if 0

        if(comp == COMPRESSION_NONE &&
                samplesPerPixel == 4 /* RGBA */ &&
                planar == 1 &&
                bitsPerSample == 8)
        {
            // image is uncompressed, use a shortcut for quick displaying

            const auto stripCount = TIFFNumberOfStrips(d->tiff);

            uint64_t *stripOffset = nullptr;
            TIFFGetField(d->tiff, TIFFTAG_STRIPOFFSETS, &stripOffset);

            if(stripCount == 0 || stripOffset == nullptr)
            {
                throw std::runtime_error("This should never happen: TIFFNumberOfStrips() returned zero??");
            }

            auto initialOffset = stripOffset[0];

            if(stripCount >= 2)
            {
                auto stripLen = stripOffset[1] - initialOffset;
                size_t nOffsets = stripCount;

                for(size_t s = 2; s < nOffsets; s++)
                {
                    if(stripOffset[s] != stripLen * s + initialOffset)
                    {
                        this->setDecodingMessage("TIFF Strips are not contiguous. Cannot use fast decoding hack. Trying regular, slow decoding instead.");
                        goto gehtnich;
                    }
                }
            }

            this->setDecodingMessage("Uh, it's an uncompressed 8-bit RGBA TIFF. Using fast decoding hack. This may take a few seconds and cannot be cancelled... ");

            size_t rowStride = size_t(width) * samplesPerPixel;
            const uint8_t *rawRgb = d->buffer + initialOffset;
            rawRgb += roi.y() * rowStride;
            rawRgb += roi.x() * samplesPerPixel;

            QImage rawImage(rawRgb,
                            roi.width(),
                            roi.height(),
                            rowStride,
                            QImage::Format_RGBA8888);
            image = rawImage.scaled(roi.size() / desiredDecodeScale, Qt::KeepAspectRatio, Qt::FastTransformation);
            image = image.scaled(desiredResolution, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            this->image()->setDecodedImage(image);
            this->updateDecodedRoiRect(roi);
        }
        else
#endif
        {
gehtnich:
            std::vector<uint32_t> stripBuf(width * rowsperstrip);
            std::vector<uint32_t> stripBufUncrustified(width * rowsperstrip);

            for(tstrip_t strip = 0, destRow = 0; strip < stripCount; strip++)
            {
                const uint32_t rowsToDecode = std::min<size_t>(rowsperstrip, height - strip * rowsperstrip);
                const unsigned y = (strip * rowsperstrip);
                QRect stripRect(0, y, width, rowsToDecode);

                QRect areaToCopy = stripRect.intersected(roi);
                if(areaToCopy.isEmpty())
                {
                    continue;
                }

                d->debugTiffLayout.addRect(currentPageToFullResTransform.mapRect(stripRect));

                auto ret = TIFFReadRGBAStrip(d->tiff, strip * rowsperstrip, stripBuf.data());

                if(ret == 0)
                {
                    throw std::runtime_error("Error while TIFFReadRGBAStrip");
                }
                else
                {
                    d->convert32BitOrder(stripBufUncrustified.data(), stripBuf.data(), rowsToDecode, width);

                    const unsigned linesToSkipFromTop = y < areaToCopy.y() ? areaToCopy.y() - y : 0;

                    for(unsigned i = 0; i < (unsigned)areaToCopy.height(); i++)
                    {
                        ::memcpy(&buf[size_t(destRow++) * image.width() + 0], &stripBufUncrustified.data()[(i + linesToSkipFromTop) * width + areaToCopy.x()], areaToCopy.width() * sizeof(uint32_t));
                    }

                    if(!quiet)
                    {
                        this->updateDecodedRoiRect(areaToCopy);

                        double progress = strip * 100.0 / stripCount;
                        this->setDecodingProgress(progress);
                    }
                }
            }

            Q_ASSERT(image.constBits() == dataPtrBackup);
        }
    }

    this->setDecodingMessage("TIFF decoding completed successfully.");
    this->setDecodingProgress(100);
}


const QPainterPath* SmartTiffDecoder::imageLayout()
{
    return &d->debugTiffLayout;
}
