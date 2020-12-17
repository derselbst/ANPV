
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
    SmartImageDecoder(const QFileInfo&, QByteArray arr = QByteArray());
    ~SmartImageDecoder() override;
    
    SmartImageDecoder(const SmartImageDecoder&) = delete;
    SmartImageDecoder& operator=(const SmartImageDecoder&) = delete;
    
    const QFileInfo& fileInfo();
    QSize size();
    // Returns a thumbnail preview image if available
    QImage thumbnail(bool applyExifTrans);
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
    void setThumbnail(QImage thumb);
    void setSize(QSize s);
    
    void setDecodingMessage(QString&& msg);
    void setDecodingProgress(int prog);
    void updatePreviewImage(QImage&& img);

    template<typename T>
    std::unique_ptr<T[]> allocateImageBuffer(uint32_t width, uint32_t height);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;

signals:
    void decodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void decodingProgress(SmartImageDecoder* self, int progress, QString message);
    void imageRefined(SmartImageDecoder* self, QImage img);
};
