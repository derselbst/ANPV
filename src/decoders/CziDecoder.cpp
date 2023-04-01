
#include "CziDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <QDebug>
#include <QtGlobal>
#include <QColorSpace>

#include <libCZI/libCZI.h>

struct CziDecoder::Impl
{
    CziDecoder* q;

    std::shared_ptr<libCZI::ICZIReader> cziReader;
    std::shared_ptr<const void> pseudoSharedDataPtr;
    std::shared_ptr<libCZI::IBitmapData> mcComposite;

    std::vector <libCZI::PyramidStatistics::PyramidLayerStatistics> pyramidLayers;


    Impl(CziDecoder* q) : q(q)
    {
    }


    QImage::Format format(int activeChanCount)
    {
        // The zero initialized, not-yet-decoded image buffer should be displayed transparently. Therefore, always use ARGB, even if this
        // would cause a performance drawback for images which do not have one, because Qt may call QPixmap::mask() internally.
        return QImage::Format_ARGB32;
    }


    class MyBitmapData : public libCZI::IBitmapData
    {
        libCZI::PixelType pixT;
        libCZI::IntSize size;

        void* ptrData;
        std::uint32_t   stride;
        std::uint64_t   byteCount;


    public:
        MyBitmapData(libCZI::PixelType pixt, libCZI::IntSize size, void* ptrData, std::uint32_t stride, std::uint64_t bytes)
            :pixT(pixt), size(size), ptrData(ptrData), stride(stride), byteCount(bytes)
        {}

        libCZI::PixelType       GetPixelType() const override
        {
            return this->pixT;
        }

        libCZI::IntSize         GetSize() const override
        {
            return this->size;
        }

        libCZI::BitmapLockInfo  Lock() override
        {
            return { ptrData, ptrData, stride, byteCount };
        }

        void            Unlock() override
        {}
    };

    std::shared_ptr<libCZI::IBitmapData> MyCreateBitmap(QImage& outImage, libCZI::PixelType pixeltype, std::uint32_t width, std::uint32_t height, std::uint32_t stride = 0, std::uint32_t extraRows = 0, std::uint32_t extraColumns = 0)
    {
        QImage::Format format;
        switch (pixeltype)
        {
        case libCZI::PixelType::Bgra32:
            format = QImage::Format_ARGB32;
            break;
        case libCZI::PixelType::Bgr24:
            format = QImage::Format_RGB32;
            break;
        default:
            throw std::logic_error("CZI Pixel type not implemented");
        }

        outImage = q->allocateImageBuffer(width, height, format);

        auto* dataPtr = outImage.constBits();
        uint8_t* buf = const_cast<uint8_t*>((dataPtr));

        libCZI::IntSize is{ width,height };

        return std::make_shared<MyBitmapData>(pixeltype, is, buf, outImage.bytesPerLine(), (height + extraRows * 2ULL) * outImage.bytesPerLine());
    }
    
};

CziDecoder::CziDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>(this))
{
    d->cziReader = libCZI::CreateCZIReader();
    
}

CziDecoder::~CziDecoder()
{
    this->assertNotDecoding();
}

void CziDecoder::close()
{
    if (d->mcComposite)
    {
  //      d->mcComposite->Unlock();
//        d->mcComposite = nullptr;
    }
    d->pseudoSharedDataPtr = nullptr;
    d->cziReader->Close();
    SmartImageDecoder::close();
}

void CziDecoder::decodeHeader(const unsigned char* buffer, qint64 nbytes)
{
    // construct a shared_ptr without deleter
    d->pseudoSharedDataPtr.reset(buffer, [](const void*) {});

    auto inputStream = libCZI::CreateStreamFromMemory(d->pseudoSharedDataPtr, nbytes);

    this->setDecodingMessage("Parsing CZI Image");
    d->cziReader->Open(inputStream);


    this->setDecodingMessage("Reading CZI Metadata");
    auto metadataSegment = d->cziReader->ReadMetadataSegment();

    const auto metadata = metadataSegment->CreateMetaFromMetadataSegment();
    const auto docInfo = metadata->GetDocumentInfo();
    auto displaySettings = docInfo->GetDisplaySettings();

    auto zDimInfo = docInfo->GetDimensionZInfo();
    if (zDimInfo != nullptr)
    {
        QString err = QStringLiteral("'%1' contains unsupported Z dimension").arg(this->image()->fileInfo().fileName());
        qWarning() << err;
    }

    auto cDimInfo = docInfo->GetDimensionChannelsInfo();
    if (cDimInfo == nullptr)
    {
        throw std::runtime_error("No information about channels could be obtained");
    }

    auto chanCount = cDimInfo->GetChannelCount();
    switch (chanCount)
    {
    case 1:
    case 3:
    case 4:
        break;
    default:
        throw std::runtime_error(Formatter() << "A channel count of " << chanCount << " is unsupported!");
    }

    auto stat = d->cziReader->GetStatistics();
    auto pyramidStat= d->cziReader->GetPyramidStatistics();

    this->image()->setSize(QSize(stat.boundingBoxLayer0Only.w, stat.boundingBoxLayer0Only.h));
    auto& scenes = pyramidStat.scenePyramidStatistics;
    for (auto& [key, val] : scenes)
    {
        if (key == std::numeric_limits<int>::max())
        {
            qWarning() << "CZI contains invalid scenes";
            break;
        }
    }
    if (scenes.size() != 1)
    {
        qWarning() << "CZI contains more than one scene, which is not supported currently";
    }
    d->pyramidLayers = scenes.begin()->second;
#if 0
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
        catch(const std::exception& e)
        {
            QString err = QStringLiteral(
                "'%1' has a thumbnail at directory no. %2. However, an error occurred while trying to decode it: "
                "'%3'").arg(this->image()->fileInfo().fileName()).arg(QString::number(thumbnailPageToDecode)).arg(e.what());
            qWarning() << err;
            this->setDecodingMessage(std::move(err));
        }
    }
#endif
}

QImage CziDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    // get the display-setting from the document's metadata
    auto mds = d->cziReader->ReadMetadataSegment();
    auto md = mds->CreateMetaFromMetadataSegment();
    auto docInfo = md->GetDocumentInfo();
    auto dsplSettings = docInfo->GetDisplaySettings();

    libCZI::IntRect roi{ roiRect.x(), roiRect.y(), roiRect.width(), roiRect.height() };
    QTransform scaleTrafo = this->fullResToPageTransform(desiredResolution.width(), desiredResolution.height());

    QSize fullSize = this->image()->fullResolutionRect().size();
    float zoom = libCZI::Utils::CalcZoom(libCZI::IntSize(fullSize.width(), fullSize.height()), libCZI::IntSize(desiredResolution.width(), desiredResolution.height()));
    zoom = std::max(scaleTrafo.m11(), scaleTrafo.m22());
    auto& pyLayer = d->pyramidLayers[0].layerInfo;

    // get the tile-composite for all channels (which are marked 'active' in the display-settings)
    std::vector<std::shared_ptr<libCZI::IBitmapData>> actvChBms;
    int index = 0;  // index counting only the active channels
    std::map<int, int> activeChNoToChIdx;   // we need to keep track which 'active channels" corresponds to which channel index
    auto accessor = d->cziReader->CreateSingleChannelPyramidLayerTileAccessor();
    libCZI::CDisplaySettingsHelper::EnumEnabledChannels(dsplSettings.get(),
        [&](int chIdx)->bool
        {
            libCZI::CDimCoordinate planeCoord{ { libCZI::DimensionIndex::C, chIdx } };
            actvChBms.emplace_back(accessor->Get(roi, &planeCoord, pyLayer, nullptr));
            activeChNoToChIdx[chIdx] = index++;
            return true;
        });

    // initialize the helper with the display-settings and provide the pixeltypes 
    // (for each active channel)
    libCZI::CDisplaySettingsHelper dsplHlp;
    dsplHlp.Initialize(dsplSettings.get(),
        [&](int chIdx)->libCZI::PixelType { return actvChBms[activeChNoToChIdx[chIdx]]->GetPixelType(); });




    auto srcBitmapsIterator = std::begin(actvChBms);

    QImage image;
    std::shared_ptr<libCZI::IBitmapData> destBitmap = d->MyCreateBitmap(image, libCZI::PixelType::Bgra32, (*srcBitmapsIterator)->GetWidth(), (*srcBitmapsIterator)->GetHeight());

    image.setOffset(roiRect.topLeft());

    QTransform toFullScaleTransform = scaleTrafo.inverted();
    this->image()->setDecodedImage(image, toFullScaleTransform);
    this->resetDecodedRoiRect();


    std::vector<libCZI::IBitmapData*> vecBm;
    auto channelCount = dsplHlp.GetActiveChannelsCount();
    vecBm.reserve(channelCount);
    for (int i = 0; i < channelCount; ++i)
    {
        vecBm.emplace_back((*srcBitmapsIterator).get());
        ++srcBitmapsIterator;
    }

    // pass the tile-composites we just created (and the display-settings for the those active 
    //  channels) into the multi-channel-composor-function
    libCZI::Compositors::ComposeMultiChannel_Bgra32(destBitmap.get(),
        255,
        channelCount,
        vecBm.data(),
        dsplHlp.GetChannelInfosArray());

    

    //this->updateDecodedRoiRect(QRect(image.offset(), image.size()));

    return image;
#if 0
    const QRect fullImageRect = this->image()->fullResolutionRect();
    
    QRect targetImageRect = fullImageRect;
    if(!roiRect.isEmpty())
    {
        QRect intersect = targetImageRect.intersected(roiRect);
        if (!intersect.isEmpty())
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
    if (imagePageToDecode < 0)
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
    
    image.setOffset(roiRect.topLeft());
    
    QTransform toFullScaleTransform = scaleTrafo.inverted();
    this->image()->setDecodedImage(image, toFullScaleTransform);
    this->resetDecodedRoiRect();
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
#endif
}

void CziDecoder::decodeInternal(int imagePageToDecode, QImage& image, QRect roi, QTransform currentPageToFullResTransform, QSize desiredResolution, bool quiet)
{
#if 0
    const auto& width = d->pageInfos[imagePageToDecode].width;
    const auto& height = d->pageInfos[imagePageToDecode].height;
    
    if(!roi.isValid())
    {
        // roi's coordinates are native to imagePageToDecode
        roi = QRect(0, 0, width, height);
    }
    
    Q_ASSERT(roi.size() == image.size());

    TIFFSetDirectory(d->tiff, imagePageToDecode);

    uint16_t samplesPerPixel = d->pageInfos[imagePageToDecode].spp;
    uint16_t bitsPerSample = d->pageInfos[imagePageToDecode].bps;

    uint16_t comp;
    if(!TIFFGetField(d->tiff, TIFFTAG_COMPRESSION, &comp))
    {
        throw std::runtime_error("Failed to read TIFFTAG_COMPRESSION");
    }
    if(!TIFFIsCODECConfigured(comp))
    {
        throw std::runtime_error(Formatter() << "Codec " << (int)comp << " is not supported by libtiff");
    }
#if 0
    uint16_t planar;
    TIFFGetField(d->tiff, TIFFTAG_PLANARCONFIG, &planar);
#endif
    this->setDecodingMessage((Formatter() << "Decoding TIFF image at directory no. " << imagePageToDecode).str().c_str());

    auto* dataPtrBackup = image.constBits();
    uint32_t* buf = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(dataPtrBackup));
    if(TIFFIsTiled(d->tiff))
    {   
        uint32_t tw,tl;
        if(!TIFFGetField(d->tiff, TIFFTAG_TILEWIDTH, &tw) || !TIFFGetField(d->tiff, TIFFTAG_TILELENGTH, &tl))
        {
            throw std::runtime_error("Failed to read tile size");
        }
    
        std::vector<uint32_t> tileBuf(tw * tl);
        
        unsigned destRowIncr = 0;
        for (uint32_t y = 0, destRow = 0; y < height; y += tl, destRow += destRowIncr)
        {
            for (uint32_t x = 0, destCol=0; x < width; x += tw)
            {
                const unsigned linesToCopy = std::min(tl, height - y);
                const unsigned widthToCopy = std::min(tw, width - x);
                QRect tileRect(x,y,widthToCopy,linesToCopy);
                
                QRect areaToCopy = tileRect.intersected(roi);
                if(areaToCopy.isEmpty())
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
                    const unsigned linesToSkipFromTop = y < areaToCopy.y() ? areaToCopy.y() - y : 0;
                    const unsigned widthToSkipFromLeft = x < areaToCopy.x() ? areaToCopy.x() - x : 0;
                    for (unsigned i = 0; i < (unsigned)areaToCopy.height(); i++)
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

                    if (!quiet)
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
            throw std::runtime_error("Failed to read RowsPerStip. Not a TIFF file?");
        }
        
        const auto stripCount = TIFFNumberOfStrips(d->tiff);
#if 0
        if(comp == COMPRESSION_NONE &&
            samplesPerPixel == 4 /* RGBA */ &&
            planar == 1 &&
            bitsPerSample==8)
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
            
            size_t rowStride = size_t(width) * samplesPerPixel;
            const uint8_t* rawRgb = d->buffer + initialOffset;
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
            for (tstrip_t strip = 0, destRow=0; strip < stripCount; strip++)
            {
                const uint32_t rowsToDecode = std::min<size_t>(rowsperstrip, height - strip * rowsperstrip);
                const unsigned y = (strip*rowsperstrip);
                QRect stripRect(0,y,width,rowsToDecode);
                
                QRect areaToCopy = stripRect.intersected(roi);
                if(areaToCopy.isEmpty())
                {
                    continue;
                }
                
                auto ret = TIFFReadRGBAStrip(d->tiff, strip * rowsperstrip, stripBuf.data());
                if(ret == 0)
                {
                    throw std::runtime_error("Error while TIFFReadRGBAStrip");
                }
                else
                {
                    d->convert32BitOrder(stripBufUncrustified.data(), stripBuf.data(), rowsToDecode, width);

                    const unsigned linesToSkipFromTop = y < areaToCopy.y() ? areaToCopy.y() - y : 0;
                    for (unsigned i = 0; i < (unsigned)areaToCopy.height(); i++)
                    {
                        ::memcpy(&buf[size_t(destRow++) * image.width() + 0], &stripBufUncrustified.data()[(i + linesToSkipFromTop) * width + areaToCopy.x()], areaToCopy.width() * sizeof(uint32_t));
                    }

                    if (!quiet)
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
#endif
}
