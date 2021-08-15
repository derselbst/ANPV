
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "Image.hpp"

#include <QtDebug>
#include <QPromise>
#include <QMetaMethod>
#include <QThreadPool>
#include <chrono>
#include <atomic>
#include <mutex>

struct SmartImageDecoder::Impl
{
    std::unique_ptr<QPromise<DecodingState>> promise;
    std::atomic<DecodingState> state{ DecodingState::Ready };
    DecodingState targetState;
    QSize desiredResolution;
    QRect roiRect;
    QString decodingMessage;
    int decodingProgress=0;

    std::chrono::time_point<std::chrono::steady_clock> lastPreviewImageUpdate = std::chrono::steady_clock::now();
    
    QString errorMessage;
    
    QSharedPointer<Image> image;
    
    // May or may not contain (a part of) the encoded input file
    // It does for embedded JPEG preview in CR2
    QByteArray encodedInputFile;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage decodedImage;
    
    // buffer that holds the data of the image (want to manage this resource myself, and not rely on the shared buffer by QImage; also QImage poorly handles out-of-memory situations)
    std::unique_ptr<unsigned char[]> backingImageBuffer;
    
    SmartImageDecoder* q;
    
    QSharedPointer<QFile> file;
    qint64 encodedInputBufferSize = 0;
    const unsigned char* encodedInputBufferPtr = nullptr;
    
    Impl(SmartImageDecoder* q, QSharedPointer<Image> image, QByteArray arr) : q(q), image(image), encodedInputFile(arr)
    {}
    
    void open(QFileInfo& info)
    {
        if(this->file && this->file->isOpen())
        {
            throw std::logic_error("File is already open!");
        }
        
        QSharedPointer<QFile> file(new QFile(info.absoluteFilePath()), &QObject::deleteLater);
        if (!file->open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(Formatter() << "Unable to open file '" << info.absoluteFilePath().toStdString() << "'");
        }
        this->file = file;
    }

    void setDecodedImage(QImage img)
    {
        decodedImage = img;
    }
    
    void releaseFullImage()
    {
        setDecodedImage(QImage());
        backingImageBuffer.reset();
    }
    
    void assertNotDecoding()
    {
        if(!this->promise)
        {
            return;
        }
        
        if(!this->promise->future().isFinished())
        {
            std::logic_error("Operation not allowed, decoding is still ongoing.");
        }
    }
};

SmartImageDecoder::SmartImageDecoder(QSharedPointer<Image> image, QByteArray arr) : d(std::make_unique<Impl>(this, image, arr))
{
    this->setAutoDelete(false);
}

SmartImageDecoder::~SmartImageDecoder()
{
    xThreadGuard g(this);
    
    bool taken = QThreadPool::globalInstance()->tryTake(this);
    if(!taken)
    {
        if(d->promise && !d->promise->future().isFinished())
        {
            d->promise->future().cancel();
            d->promise->future().waitForFinished();
        }
    }
    
    this->close();
}

QSharedPointer<Image> SmartImageDecoder::image()
{
    return d->image;
}

void SmartImageDecoder::setDecodingState(DecodingState state)
{
    DecodingState old = d->state;
    d->state = state;
    
    if(old != state)
    {
        emit this->decodingStateChanged(this, state, old);
    }
}

void SmartImageDecoder::cancelCallback()
{
    if(d->promise && d->promise->isCanceled())
    {
        throw UserCancellation();
    }
}

// initializes the decoder, by reading as much of the file as necessary to know about most important information
void SmartImageDecoder::init()
{
        // mmap() the file. Do NOT use MAP_PRIVATE! See https://stackoverflow.com/a/7222430
        qint64 mapSize = d->file->size();
        const unsigned char* fileMapped = d->file->map(0, mapSize, QFileDevice::NoOptions);
        
        d->encodedInputBufferSize = mapSize;
        d->encodedInputBufferPtr = fileMapped;
        if(d->encodedInputFile.isEmpty())
        {
            if(fileMapped == nullptr)
            {
                throw std::runtime_error(Formatter() << "Could not mmap() file '" << d->file->fileName().toStdString() << "', error was: " << d->file->errorString().toStdString());
            }
        }
        else
        {
            d->encodedInputBufferPtr = reinterpret_cast<const unsigned char*>(d->encodedInputFile.constData());
            d->encodedInputBufferSize = d->encodedInputFile.size();
        }
        
        this->cancelCallback();
        
        this->decodeHeader(d->encodedInputBufferPtr, d->encodedInputBufferSize);
        
        QSharedPointer<ExifWrapper> exifWrapper(new ExifWrapper());
        // intentionally use the original file to read EXIF data, as this may not be available in d->encodedInputBuffer
        exifWrapper->loadFromData(QByteArray::fromRawData(reinterpret_cast<const char*>(fileMapped), mapSize));
        this->image()->setExif(exifWrapper);
        this->image()->setDefaultTransform(exifWrapper->transformMatrix());
        this->image()->setThumbnail(exifWrapper->thumbnail());
        
        this->setDecodingState(DecodingState::Metadata);
}

QFuture<DecodingState> SmartImageDecoder::decodeAsync(DecodingState targetState, Priority prio, QSize desiredResolution, QRect roiRect)
{
    xThreadGuard g(this);
    d->assertNotDecoding();
    
    d->targetState = targetState;
    d->desiredResolution = desiredResolution;
    d->roiRect = roiRect;
    d->promise = std::make_unique<QPromise<DecodingState>>();
    d->promise->setProgressRange(0, 100);
    QThreadPool::globalInstance()->start(this, static_cast<int>(prio));

    return d->promise->future();
}

void SmartImageDecoder::run()
{
    this->decode(d->targetState, d->desiredResolution, d->roiRect);
}

void SmartImageDecoder::decode(DecodingState targetState, QSize desiredResolution, QRect roiRect)
{
    try
    {
        if(d->promise)
        {
            d->promise->start();
        }
        this->cancelCallback();
        do
        {
            if(decodingState() != DecodingState::Metadata)
            {
                // metadata has not been read yet or try to recover previous error
                this->init();
            }
            
            QImage decodedImg = this->decodingLoop(targetState, desiredResolution, roiRect);
            d->setDecodedImage(decodedImg);
            
            // if thumbnail is still null and no roi has been given, set it
            if (this->image()->thumbnail().isNull() && !roiRect.isValid())
            {
                this->image()->setThumbnail(decodedImg.scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }

            this->setDecodingState(targetState);
        } while(false);
    }
    catch(const UserCancellation&)
    {
        this->setDecodingState(DecodingState::Cancelled);
        this->close();
    }
    catch(const std::exception& e)
    {
        d->errorMessage = e.what();
        this->setDecodingState(DecodingState::Error);
        this->close();
    }
    
    if(d->promise)
    {
        // this will not store the result if the future has been canceled already!
        d->promise->addResult(d->state.load());
        d->promise->finish();
    }
}

void SmartImageDecoder::close()
{
    d->assertNotDecoding();

    d->encodedInputBufferSize = 0;
    d->encodedInputBufferPtr = nullptr;
    d->file->close();
    d->file = nullptr;
}

void SmartImageDecoder::connectNotify(const QMetaMethod& signal)
{
    if (signal == QMetaMethod::fromSignal(&SmartImageDecoder::decodingStateChanged))
    {
        DecodingState cur = decodingState();
        emit this->decodingStateChanged(this, cur, cur);
    }

    QObject::connectNotify(signal);
}

void SmartImageDecoder::reset()
{
    d->assertNotDecoding();
    d->releaseFullImage();
    this->setDecodingState(DecodingState::Metadata);
}

DecodingState SmartImageDecoder::decodingState() const
{
    return d->state;
}

QString SmartImageDecoder::latestMessage()
{
    xThreadGuard g(this);
    return d->decodingMessage;
}

QString SmartImageDecoder::errorMessage()
{
    xThreadGuard g(this);
    return d->errorMessage;
}

QImage SmartImageDecoder::decodedImage()
{
    xThreadGuard g(this);
    return d->decodedImage;
}

void SmartImageDecoder::setDecodingMessage(QString&& msg)
{
    if(this->signalsBlocked())
    {
        return;
    }

    if(d->promise && d->decodingMessage != msg)
    {
        d->decodingMessage = std::move(msg);
        d->promise->setProgressValueAndText(d->decodingProgress, d->decodingMessage);
    }
}

void SmartImageDecoder::setDecodingProgress(int prog)
{
    if(this->signalsBlocked())
    {
        return;
    }

    if(d->promise && d->decodingProgress != prog)
    {
        d->promise->setProgressValueAndText(prog , d->decodingMessage);
    }
}

void SmartImageDecoder::updatePreviewImage(QImage&& img)
{
    if(this->signalsBlocked())
    {
        return;
    }

    constexpr int DecodePreviewImageRefreshDuration = 100;
    
    auto now = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - d->lastPreviewImageUpdate);
    if(durationMs.count() > DecodePreviewImageRefreshDuration)
    {
        emit this->imageRefined(this, std::move(img));
        d->lastPreviewImageUpdate = now;
    }
}

template<typename T>
T* SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height)
{
    d->releaseFullImage();

    size_t needed = size_t(width) * height * sizeof(T);
    try
    {
        this->setDecodingMessage("Allocating image output buffer");

        std::unique_ptr<unsigned char[]> mem(new unsigned char[needed]);

        // enter the PreviewImage state, even if the image is currently blank, so listeners can start listening for decoding updates
        this->setDecodingState(DecodingState::PreviewImage);
        
        d->backingImageBuffer = std::move(mem);
        return reinterpret_cast<T*>(d->backingImageBuffer.get());
    }
    catch (const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << needed / 1024. / 1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
    }
}

template uint32_t* SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height);
