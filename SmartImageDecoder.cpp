
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include <QtDebug>
#include <QMetaMethod>
#include <chrono>
#include <atomic>
#include <mutex>

struct SmartImageDecoder::Impl
{
    std::function<void(void*)> cancelCallbackInternal;
    void* cancelCallbackObject = nullptr;
    std::atomic<DecodingState> state{ DecodingState::Ready };
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
    QImage thumbnail;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage image;
    
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

    void setImage(QImage&& img)
    {
    qWarning() << "need to apply exif transformation!";
        image = img;
    }
};

SmartImageDecoder::SmartImageDecoder(const QFileInfo& url, QByteArray arr) : d(std::make_unique<Impl>(url, arr))
{}

SmartImageDecoder::~SmartImageDecoder() = default;

void SmartImageDecoder::setCancellationCallback(std::function<void(void*)>&& cc, void* obj)
{
    d->cancelCallbackInternal = std::move(cc);
    d->cancelCallbackObject = obj;
}

void SmartImageDecoder::setThumbnail(QImage&& thumb)
{
    qWarning() << "need to apply exif transformation!";
    d->thumbnail = std::move(thumb);
}
    
void SmartImageDecoder::cancelCallback()
{
    if(d->cancelCallbackInternal)
    {
        d->cancelCallbackInternal(d->cancelCallbackObject);
    }
}

DecodingState SmartImageDecoder::decodingState()
{
    return d->state;
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

void SmartImageDecoder::decode(DecodingState targetState)
{
    std::lock_guard<std::mutex> lck(d->m);

    if(decodingState() != DecodingState::Error &&
       decodingState() != DecodingState::Cancelled &&
       decodingState() >= targetState)
    {
        // we already have more decoded than requested, do nothing
        return;
    }

    try
    {
        do
        {
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
            if (this->thumbnail().isNull())
            {
                this->setThumbnail(d->exifWrapper.thumbnail());
            }

            this->setDecodingState(DecodingState::Metadata);
                        
            if(decodingState() >= targetState)
            {
                break;
            }
            
            QImage decodedImg = this->decodingLoop(targetState);
            d->setImage(std::move(decodedImg));

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
        d->setImage(QImage());
        this->setDecodingState(DecodingState::Metadata);
    }
    else
    {
	    qInfo() << "another thread is currently decoding, ignore releasing the image";
    }
}

const QFileInfo& SmartImageDecoder::fileInfo()
{
    return d->fileInfo;
}

QString SmartImageDecoder::latestMessage()
{
    return d->decodingMessage;
}

QString SmartImageDecoder::errorMessage()
{
    return d->errorMessage;
}

QImage SmartImageDecoder::image()
{
    return d->image;
}

QImage SmartImageDecoder::thumbnail()
{
    return d->thumbnail;
}

QSize SmartImageDecoder::size()
{
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
        emit this->decodingProgress(this, d->decodingProgress, d->decodingMessage);
    }
}

void SmartImageDecoder::setDecodingProgress(int prog)
{
    if(d->decodingProgress != prog)
    {
        d->decodingProgress = prog;
        emit this->decodingProgress(this, prog, d->decodingMessage);
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
std::unique_ptr<T[]> SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height)
{
    size_t needed = width * height;
    try
    {
        this->setDecodingMessage("Allocating image output buffer");

        std::unique_ptr<T[]> mem(new T[needed]);

        this->setDecodingState(DecodingState::PreviewImage);
        return mem;
    }
    catch (const std::bad_alloc&)
    {
        throw std::runtime_error(Formatter() << "Unable to allocate " << needed / 1024. / 1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
    }
}

template std::unique_ptr<uint32_t[]> SmartImageDecoder::allocateImageBuffer(uint32_t width, uint32_t height);
