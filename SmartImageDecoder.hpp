
#pragma once

#include "DecodingState.hpp"

#include <QObject>
#include <QSize>
#include <QImage>
#include <QFileInfo>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstdint>

class ExifWrapper;
class QMetaMethod;

class SmartImageDecoder : public QObject
{
Q_OBJECT

public:
    SmartImageDecoder(const QFileInfo&);
    virtual ~SmartImageDecoder();
    
    SmartImageDecoder(const SmartImageDecoder&) = delete;
    SmartImageDecoder& operator=(const SmartImageDecoder&) = delete;
    
    virtual QSize size() = 0;
    
    const QFileInfo& fileInfo();
    // Returns a thumbnail preview image if available
    QImage thumbnail();
    QImage image();
    QString errorMessage();
    QString latestMessage();
    void decode(DecodingState targetState);
    DecodingState decodingState();
    void releaseFullImage();
    
    ExifWrapper* exif();
    
    void setCancellationCallback(std::function<void(void*)>&& cc, void* obj);
    
protected:
    virtual void decodeHeader(const unsigned char* buffer, qint64 nbytes) = 0;
    virtual QImage decodingLoop(DecodingState state) = 0;
    virtual void close();

    void connectNotify(const QMetaMethod& signal) override;
    
    void cancelCallback();
    void setDecodingState(DecodingState state);
    void setThumbnail(QImage&& thumb);
    
    void setDecodingMessage(QString&& msg);
    void setDecodingProgress(int prog);
    void updatePreviewImage(QImage&& img);

    template<typename T>
    std::unique_ptr<T> allocateImageBuffer<T>(uint32_t width, uint32_t height)
    {
        size_t needed = width * height;
        try
        {
            this->setDecodingMessage("Allocating image output buffer");

            std::unique_ptr<T> mem(new T[needed]);

            this->setDecodingState(DecodingState::PreviewImage);
            return mem;
        }
        catch (const std::bad_alloc&)
        {
            throw std::runtime_error(Formatter() << "Unable to allocate " << needed / 1024. / 1024. << " MiB for the decoded image with dimensions " << width << "x" << height << " px");
        }
    }

private:
    struct Impl;
    std::unique_ptr<Impl> d;

signals:
    void decodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void decodingProgress(SmartImageDecoder* self, int progress, QString message);
    void imageRefined(SmartImageDecoder* self, QImage img);
};
