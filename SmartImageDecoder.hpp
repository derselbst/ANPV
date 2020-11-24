
#pragma once

#include "DecodingState.hpp"

#include <QObject>
#include <QSize>
#include <QImage>
#include <QFileInfo>
#include <functional>
#include <memory>

class ExifWrapper;

class SmartImageDecoder : public QObject
{
Q_OBJECT

public:
    SmartImageDecoder(QString&&);
    virtual ~SmartImageDecoder();

    virtual QSize size() = 0;
    
    QFileInfo fileInfo();
    // Returns a thumbnail preview image if available
    QImage thumbnail();
    QImage image();
    QString errorMessage();
    void decode(DecodingState targetState);
    
    ExifWrapper* exif();
    
    void setCancellationCallback(std::function<void(void*)>&& cc, void* obj);
    
protected:
    virtual void decodeHeader() = 0;
    virtual void decodingLoop(DecodingState state) = 0;
    
    DecodingState decodingState();
    void fileBuf(const unsigned char** buf, qint64* size);
    void cancelCallback();
    void setDecodingState(DecodingState state);
    void setImage(QImage&& img);
    void setThumbnail(QImage&& thumb);
    
    void setDecodingMessage(QString&& msg);
    void setDecodingProgress(int prog);
    void updatePreviewImage(QImage&& img);

private:
    struct Impl;
    std::unique_ptr<Impl> d;

signals:
    void decodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void decodingProgress(SmartImageDecoder* self, int progress, QString message);
    void imageRefined(SmartImageDecoder* self, QImage img);
};
