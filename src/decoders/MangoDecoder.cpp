
#include "MangoDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <QColorSpace>

#include <mango/mango.hpp>

struct MangoDecoder::Impl
{
    std::unique_ptr<mango::image::ImageDecoder> mangoDec;
    
    QImage::Format format()
    {
        // The zero initialized, not-yet-decoded image buffer should be displayed transparently. Therefore, always use ARGB, even if this
        // would cause a performance drawback for images which do not have one, because Qt may call QPixmap::mask() internally.
        return QImage::Format_RGBA8888;
    }
};

MangoDecoder::MangoDecoder(QSharedPointer<Image> image) : SmartImageDecoder(image), d(std::make_unique<Impl>())
{}

MangoDecoder::~MangoDecoder()
{
    this->assertNotDecoding();
}

void MangoDecoder::close()
{
    d->mangoDec = nullptr;
    SmartImageDecoder::close();
}

void MangoDecoder::decodeHeader(const unsigned char *buffer, qint64 nbytes)
{
    d->mangoDec = std::make_unique<mango::image::ImageDecoder>(mango::ConstMemory(buffer, nbytes), this->image()->fileInfo().fileName().toStdString());
    if (d->mangoDec->isDecoder())
    {
        this->setDecodingMessage("Created Mango Decoder Successfully");
        mango::image::ImageHeader header = d->mangoDec->header();
        this->image()->setSize(QSize(header.width, header.height));
        (void)header.format;
    }
    else
    {
        throw std::runtime_error("Mango decoder creation failed");
    }

    // uint32_t count;
    // void *profile;
    // QColorSpace cs{QColorSpace::SRgb};
    // 
    // if(TIFFGetField(d->tiff, TIFFTAG_ICCPROFILE, &count, &profile))
    // {
    //     QByteArray iccProfile(reinterpret_cast<const char *>(profile), count);
    //     cs = QColorSpace::fromIccProfile(iccProfile);
    // }
    // 
    // this->image()->setColorSpace(cs);
}

QImage MangoDecoder::decodingLoop(QSize desiredResolution, QRect roiRect)
{
    const QRect fullImageRect = this->image()->fullResolutionRect();
    QImage image = this->allocateImageBuffer(fullImageRect.size(), d->format());
    image.setOffset(roiRect.topLeft());
    auto *dataPtrBackup = image.constBits();

    #define FORMAT_R8G8B8A8             mango::image::Format(32, mango::image::Format::UNORM, mango::image::Format::RGBA, 8, 8, 8, 8)
    static const mango::image::Format format = FORMAT_R8G8B8A8;
    auto width = fullImageRect.size().width();
    
    uint32_t *buf = const_cast<uint32_t *>(reinterpret_cast<const uint32_t *>(dataPtrBackup));
    
    mango::image::Surface surface(width, fullImageRect.size().height(), format, width * format.bytes(), buf);

    mango::image::ImageDecodeOptions options;
    options.simd = true;
    options.multithread = false;

    mango::image::ImageDecodeStatus status = d->mangoDec->decode(surface, options);
    if (!status)
    {
        throw std::runtime_error(Formatter() << "Mango decoder failed during decode: " << status.info);
    }

    // this->convertColorSpace(image, false, toFullScaleTransform);
    this->image()->setDecodedImage(image);
    this->setDecodingState(DecodingState::FullImage);
    this->setDecodingMessage("Mango decoding completed successfully.");
    this->setDecodingProgress(100);

    return image;
}

