
#pragma once

#include "DecodingState.hpp"

#include <QObject>
#include <QSize>
#include <QImage>
#include <QFileInfo>
#include <functional>
#include <memory>


class SmartImageDecoder : public QObject
{
Q_OBJECT

public:
    SmartImageDecoder(QString&&);
    virtual ~SmartImageDecoder();

    void setCancellationCallback(std::function<void(void*)>&& cc, void* obj);
    QFileInfo fileInfo();
    // Returns a thumbnail preview image. The embedded exif thumbnail will be preferred. @p size is a hint, if no such embedded thumbnail is available, the full image will be decoded and scaled accordingly.
    QImage thumbnail(QSize size);
    QImage image();
    void decode(DecodingState targetState);
    QString errorMessage();
    
protected:
    static constexpr int DecodePreviewImageRefreshDuration = 100;
    
    virtual void decodeHeader() = 0;
    virtual void decodingLoop(DecodingState state) = 0;
    
    void fileBuf(const unsigned char** buf, qint64* size);
    void cancelCallback();
    void setDecodingState(DecodingState state);
    void setImage(QImage&& img);
    void setThumbnail(QImage&& thumb);

private:
    struct Impl;
    std::unique_ptr<Impl> d;

signals:
    void decodingStateChanged(SmartImageDecoder* self, DecodingState newState, DecodingState oldState);
    void decodingProgress(SmartImageDecoder* self, int progress, QString message);
};
