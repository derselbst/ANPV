
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include <QtDebug>
#include <chrono>

struct SmartImageDecoder::Impl
{
    std::function<void(void*)> cancelCallbackInternal;
    void* cancelCallbackObject;
    DecodingState state;
    QString decodingMessage;
    int decodingProgress=0;
    
    std::chrono::time_point<std::chrono::steady_clock> lastPreviewImageUpdate = std::chrono::steady_clock::now();
    
    QString errorMessage;
    
    QFile file;
    const unsigned char* fileMapped;
    QByteArray byteArray;
    
    // a low resolution preview image of the original full image
    QImage thumbnail;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage image;
    
    ExifWrapper exifWrapper;
    
    Impl(QString&& url) : file(std::move(url))
    {}
    
    void open()
    {
        QFileInfo info(file);
        
        if(file.isOpen())
        {
            return;
        }
        
        if (!file.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(Formatter() << "Unable to open file '" << info.absoluteFilePath().toStdString() << "'");
        }
        
        fileMapped = file.map(0, file.size(), QFileDevice::MapPrivateOption);
        if(fileMapped == nullptr)
        {
            throw std::runtime_error(Formatter() << "Could not mmap() file '" << info.absoluteFilePath().toStdString() << "'");
        }
        
        byteArray = QByteArray::fromRawData(reinterpret_cast<const char*>(fileMapped), file.size());
    }
    
    void close()
    {
        byteArray.clear();
        fileMapped = nullptr;
        file.close();
    }
};

SmartImageDecoder::SmartImageDecoder(QString&& url) : d(std::make_unique<Impl>(std::move(url)))
{
//     d->open();
//     d->close();
}

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
    try
    {
        do
        {
            d->open();
            
            this->cancelCallback();
            
            this->setDecodingState(DecodingState::Ready);
            
            this->decodeHeader();
            d->exifWrapper.loadFromData(d->byteArray);
            this->setThumbnail(d->exifWrapper.thumbnail());
            
            this->setDecodingState(DecodingState::Metadata);
                        
            if(d->state >= targetState)
            {
                break;
            }
            
            this->decodingLoop(targetState);
            this->setDecodingState(DecodingState::FullImage);
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
    
    d->close();
}

void SmartImageDecoder::fileBuf(const unsigned char** buf, qint64* size)
{
    *buf = d->fileMapped;
    *size = d->file.size();
}

QFileInfo SmartImageDecoder::fileInfo()
{
    return QFileInfo(d->file);
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
