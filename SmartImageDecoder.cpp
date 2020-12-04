
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
    
    QFileInfo fileInfo;
    
    // a low resolution preview image of the original full image
    QImage thumbnail;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage image;
    
    ExifWrapper exifWrapper;
    
    Impl(const QFileInfo& url) : fileInfo(url)
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
        image = img;
    }
};

SmartImageDecoder::SmartImageDecoder(const QFileInfo& url) : d(std::make_unique<Impl>(url))
{}

SmartImageDecoder::~SmartImageDecoder() = default;

void SmartImageDecoder::setCancellationCallback(std::function<void(void*)>&& cc, void* obj)
{
    d->cancelCallbackInternal = std::move(cc);
    d->cancelCallbackObject = obj;
}

void SmartImageDecoder::setImage(QImage&& img)
{
    qWarning() << "need to apply exif transformation!";
    d->image = std::move(img);
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
            
            const qint64 mapSize = file->size();
            const unsigned char* fileMapped = file->map(0, mapSize, QFileDevice::MapPrivateOption);
            if(fileMapped == nullptr)
            {
                throw std::runtime_error(Formatter() << "Could not mmap() file '" << d->fileInfo.absoluteFilePath().toStdString() << "'");
            }
            
            this->cancelCallback();
            
            this->decodeHeader(fileMapped, mapSize);
            d->exifWrapper.loadFromData(QByteArray::fromRawData(reinterpret_cast<const char*>(fileMapped), mapSize));
            if (this->thumbnail().isNull)
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
	    // another thread is currently decoding, ignore releasing the image
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
