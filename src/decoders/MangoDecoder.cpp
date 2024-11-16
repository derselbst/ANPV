
#include "UserCancellation.hpp"
#include "MangoDecoder.hpp"
#include "Formatter.hpp"
#include "Image.hpp"
#include "ANPV.hpp"

#include <QColorSpace>
#include <chrono>

#include <mango/mango.hpp>

using namespace std::chrono_literals;


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
        
        QColorSpace cs{QColorSpace::SRgb};
        auto icc = d->mangoDec->icc();
        if(icc.address != nullptr && icc.size > 0)
        {
            QByteArray iccProfile(reinterpret_cast<const char *>(icc.address), icc.size);
            cs = QColorSpace::fromIccProfile(iccProfile);
        }
        this->image()->setColorSpace(cs);
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
    this->image()->setDecodedImage(image);
    auto *dataPtrBackup = image.constBits();

    #define FORMAT_R8G8B8A8             mango::image::Format(32, mango::image::Format::UNORM, mango::image::Format::RGBA, 8, 8, 8, 8)
    static const mango::image::Format format = FORMAT_R8G8B8A8;
    auto width = fullImageRect.size().width();
    
    uint32_t *buf = const_cast<uint32_t *>(reinterpret_cast<const uint32_t *>(dataPtrBackup));
    
    mango::image::Surface surface(width, fullImageRect.size().height(), format, width * format.bytes(), buf);

    mango::image::ImageDecodeOptions options;
    options.simd = true;
    options.multithread = false;

    this->cancelCallback();

    mango::image::ImageDecodeStatus result;
    if (d->mangoDec->isAsyncDecoder())
    {
        size_t pixelsDecoded = 0;

        auto future = d->mangoDec->launch(
            [&](const mango::image::ImageDecodeRect& rect)
            {
                QRect qrect(rect.x, rect.y, rect.width, rect.height);
                this->updateDecodedRoiRect(qrect);

                qDebug() << "Mango update rect: " << qrect;

                pixelsDecoded += rect.width * (size_t)rect.height;
                int progress = static_cast<int>(pixelsDecoded * 100.0 / ((size_t)surface.width * surface.height));

                this->setDecodingProgress(std::min(progress, 99));
            }, surface, options);

        std::future_status status = std::future_status::timeout;
        while (status == std::future_status::timeout)
        {
            try
            {
                this->cancelCallback();
            }
            catch (const UserCancellation& c)
            {
                d->mangoDec->cancel();
                throw;
            }
            status = future.wait_for(200ms);
        }

        result = future.get();
    }
    else
    {
        result = d->mangoDec->decode(surface, options);
    }
    
    if (!result)
    {
        throw std::runtime_error(Formatter() << "Mango decoder failed during decode: " << result.info);
    }

    this->convertColorSpace(image, false);
    this->setDecodingState(DecodingState::FullImage);
    this->setDecodingMessage("Mango decoding completed successfully.");
    this->setDecodingProgress(100);

    Q_ASSERT(image.constBits() == dataPtrBackup);

    return image;
}

