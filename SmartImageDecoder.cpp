
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"

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
    void* cancelCallbackObject = nullptr;
    std::atomic<DecodingState> state{ DecodingState::Ready };
    DecodingState targetState;
    QString decodingMessage;
    int decodingProgress=0;

    std::mutex m;

    std::chrono::time_point<std::chrono::steady_clock> lastPreviewImageUpdate = std::chrono::steady_clock::now();
    
    QString errorMessage;
    
    // file path to the decoded input file
    QFileInfo fileInfo;
    
    // May or may not contain (a part of) the encoded input file
    // It does for embedded JPEG preview in CR2
    QByteArray encodedInputFile;
    
    // a low resolution preview image of the original full image
    QPixmap thumbnail;
    
    // same as thumbnail, but rotated according to EXIF orientation
    QPixmap thumbnailTransformed;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage image;
    
    // buffer that holds the data of the image (want to manage this resource myself, and not rely on the shared buffer by QImage; also QImage poorly handles out-of-memory situations)
    std::unique_ptr<unsigned char[]> fullImageBuffer;
    
    // size of the fully decoded image, already available in DecodingState::Metadata
    QSize size;
    
    ExifWrapper exifWrapper;
    
    Impl(const QFileInfo& url, QByteArray arr) : fileInfo(url), encodedInputFile(arr)
    {}
    
    void open(QFile& file)
    {
        if(file.isOpen())
        {
            return;
        }
        
        if (!file.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(Formatter() << "Unable to open file '" << fileInfo.absoluteFilePath().toStdString() << "'");
        }
    }

    void setImage(QImage img)
    {
        image = img;
    }
    
    void releaseFullImage()
    {
        setImage(QImage());
        fullImageBuffer.reset();
    }
};

SmartImageDecoder::SmartImageDecoder(const QFileInfo& url, QByteArray arr) : d(std::make_unique<Impl>(url, arr))
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
}

void SmartImageDecoder::setThumbnail(QImage thumb)
{
    d->thumbnail = QPixmap::fromImage(thumb);
    d->thumbnailTransformed = d->thumbnail.transformed(d->exifWrapper.transformMatrix());
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

void SmartImageDecoder::setSize(QSize size)
{
    d->size = size;
}

void SmartImageDecoder::cancelCallback()
{
    if(d->promise->isCanceled())
    {
        throw UserCancellation();
    }
}

QFuture<DecodingState> SmartImageDecoder::decodeAsync(DecodingState targetState)
{
    xThreadGuard g(this);
    if(d->promise != nullptr && d->promise->future().isRunning())
    {
        return d->promise->future();
    }

    std::lock_guard<std::mutex> lck(d->m);
    
    d->targetState = targetState;
    d->promise = std::make_unique<QPromise<DecodingState>>();
    d->promise->setProgressRange(0, 100);
    QThreadPool::globalInstance()->start(this);

    return d->promise->future();
}

void SmartImageDecoder::run()
{
    this->decode(d->targetState);
}

void SmartImageDecoder::decode(DecodingState targetState)
{
    std::lock_guard<std::mutex> lck(d->m);

    try
    {
        if(d->promise)
        {
            d->promise->start();
        }
        this->cancelCallback();
        do
        {
            if(decodingState() != DecodingState::Error &&
            decodingState() != DecodingState::Cancelled &&
            decodingState() >= targetState)
            {
                // we already have more decoded than requested, do nothing
                break;
            }

            QSharedPointer<QFile> file(new QFile(d->fileInfo.absoluteFilePath()), [&](QFile* f){ this->close(); delete f; });
            d->open(*file.data());
            
            qint64 mapSize = file->size();
            const unsigned char* fileMapped = file->map(0, mapSize, QFileDevice::MapPrivateOption);
            
            qint64 encodedInputBufferSize = mapSize;
            const unsigned char* encodedInputBufferPtr = fileMapped;
            if(d->encodedInputFile.isEmpty())
            {
                if(fileMapped == nullptr)
                {
                    throw std::runtime_error(Formatter() << "Could not mmap() file '" << d->fileInfo.absoluteFilePath().toStdString() << "'");
                }
            }
            else
            {
                encodedInputBufferPtr = reinterpret_cast<const unsigned char*>(d->encodedInputFile.constData());
                encodedInputBufferSize = d->encodedInputFile.size();
            }
            
            this->cancelCallback();
            
            this->decodeHeader(encodedInputBufferPtr, encodedInputBufferSize);
            
            // intentionally use the original file to read EXIF data, as this may not be available in d->encodedInputBuffer
            d->exifWrapper.loadFromData(QByteArray::fromRawData(reinterpret_cast<const char*>(fileMapped), mapSize));
            if (d->thumbnail.isNull())
            {
                this->setThumbnail(d->exifWrapper.thumbnail());
            }

            this->setDecodingState(DecodingState::Metadata);
                        
            if(decodingState() >= targetState)
            {
                break;
            }
            
            QImage decodedImg = this->decodingLoop(targetState);
            d->setImage(decodedImg);
            
            // if thumbnail is still null, set it
            if (d->thumbnail.isNull())
            {
                this->setThumbnail(decodedImg.scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }

            this->setDecodingState(targetState);
        } while(false);
    }
    catch(const UserCancellation&)
    {
        this->setDecodingState(DecodingState::Cancelled);
    }
    catch(const std::exception& e)
    {
        d->errorMessage = e.what();
        this->setDecodingState(DecodingState::Error);
    }
    
    if(d->promise)
    {
        // this will not store the result if the future has been canceled already!
        d->promise->addResult(d->state.load());
        d->promise->finish();
    }
}

void SmartImageDecoder::close()
{}

void SmartImageDecoder::connectNotify(const QMetaMethod& signal)
{
    if (signal == QMetaMethod::fromSignal(&SmartImageDecoder::decodingStateChanged))
    {
        DecodingState cur = decodingState();
        emit this->decodingStateChanged(this, cur, cur);
    }

    QObject::connectNotify(signal);
}

void SmartImageDecoder::releaseFullImage()
{
    std::unique_lock<std::mutex> lck(d->m, std::defer_lock);

    if(lck.try_lock())
    {
        d->releaseFullImage();
        this->setDecodingState(DecodingState::Metadata);
    }
    else
    {
	    qInfo() << "another thread is currently decoding, ignore releasing the image";
    }
}

DecodingState SmartImageDecoder::decodingState() const
{
    return d->state;
}

const QFileInfo& SmartImageDecoder::fileInfo() const
{
    return d->fileInfo;
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

QImage SmartImageDecoder::image()
{
    xThreadGuard g(this);
    return d->image;
}

QPixmap SmartImageDecoder::thumbnail()
{
    xThreadGuard g(this);
    return d->thumbnail;
}

QPixmap SmartImageDecoder::icon()
{
    xThreadGuard g(this);
    return d->thumbnailTransformed;
}

QSize SmartImageDecoder::size()
{
    xThreadGuard g(this);

    return d->size;
}

ExifWrapper* SmartImageDecoder::exif()
{
    return &d->exifWrapper;
}

void SmartImageDecoder::setDecodingMessage(QString&& msg)
{
    if(d->decodingMessage != msg)
    {
        d->decodingMessage = std::move(msg);
        d->promise->setProgressValueAndText(d->decodingProgress, d->decodingMessage);
    }
}

void SmartImageDecoder::setDecodingProgress(int prog)
{
    if(d->decodingProgress != prog)
    {
        d->promise->setProgressValueAndText(prog , d->decodingMessage);
    }
}

void SmartImageDecoder::updatePreviewImage(QImage&& img)
{
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
        
        d->fullImageBuffer = std::move(mem);
        return reinterpret_cast<T*>(d->fullImageBuffer.get());
    }
    catch (const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << needed / 1024. / 1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
    }
}

template uint32_t* SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height);
