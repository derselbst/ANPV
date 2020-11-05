
#include "SmartImageDecoder.hpp"

#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include <QtDebug>

struct SmartImageDecoder::Impl
{
    std::function<void(void*)> cancelCallbackInternal;
    void* cancelCallbackObject;
    DecodingState state;
    QString errorMessage;
    
    QFile file;
    const unsigned char* fileMapped;
    
    // a low resolution preview image of the original full image
    QImage thumbnail;
    
    // the fully decoded image - might be incomplete if the state is PreviewImage
    QImage image;
    
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
    }
    
    void close()
    {
        fileMapped = nullptr;
        file.close();
    }
};

SmartImageDecoder::SmartImageDecoder(QString&& url) : d(std::make_unique<Impl>(std::move(url)))
{
    d->open();
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

void SmartImageDecoder::setDecodingState(DecodingState state)
{
    DecodingState old = d->state;
    d->state = state;
    
    emit this->decodingStateChanged(this, state, old);
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
//             d->exifReader(d->file);
            this->setDecodingState(DecodingState::Metadata);
                        
            if(d->state >= targetState)
            {
                break;
            }
            
            this->decodingLoop(targetState);
            this->setDecodingState(DecodingState::FullImage);
        } while(false);
    }
    catch(const UserCancellation& e)
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

QImage SmartImageDecoder::image()
{
    return d->image;
}

QImage SmartImageDecoder::thumbnail(QSize size)
{
    switch(d->state)
    {
    case DecodingState::Metadata:
        return d->thumbnail;
        break;
    case DecodingState::PreviewImage:
    case DecodingState::FullImage:
        if(!d->thumbnail.isNull())
        {
            return d->thumbnail;
        }
        if(!d->image.isNull())
        {
            d->thumbnail = d->image.scaled(size, Qt::KeepAspectRatio, Qt::FastTransformation);
            return d->thumbnail;
        }
        [[fallthrough]];
    default:
        return QImage();
    }
}


