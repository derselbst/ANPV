
#pragma once

#include "DecodingState.hpp"

#include <QObject>
#include <QRunnable>
#include <QSize>
#include <QPixmap>
#include <QImage>
#include <QString>
#include <QFileInfo>
#include <QFuture>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstdint>

class ExifWrapper;
class QMetaMethod;

class SmartImageDecoder : public QObject, public QRunnable
{
Q_OBJECT

public:
    SmartImageDecoder(const QFileInfo&, QByteArray arr = QByteArray());
    ~SmartImageDecoder() override;
    
    SmartImageDecoder(const SmartImageDecoder&) = delete;
    SmartImageDecoder& operator=(const SmartImageDecoder&) = delete;
    
    const QFileInfo& fileInfo() const;
    QSize size();
    QPixmap thumbnail();
    QPixmap icon(int height);
    QImage image();
    QString errorMessage();
    QString latestMessage();
    void decode(DecodingState targetState, QSize desiredResolution = QSize(), QRect roiRect = QRect());
    QFuture<DecodingState> decodeAsync(DecodingState targetState, int prio, QSize desiredResolution = QSize(), QRect roiRect = QRect());
    DecodingState decodingState() const;
    void releaseFullImage();
    
    ExifWrapper* exif();
    
    void run() override;
    
protected:
    virtual void decodeHeader(const unsigned char* buffer, qint64 nbytes) = 0;
    virtual QImage decodingLoop(DecodingState state, QSize desiredResolution, QRect roiRect) = 0;
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
    T* allocateImageBuffer(uint32_t width, uint32_t height);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;

signals:
    void decodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void imageRefined(SmartImageDecoder* self, QImage img);
};
