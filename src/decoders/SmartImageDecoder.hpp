
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
class Image;

enum class Priority : int
{
    Background = -1,
    Normal = 0,
    Important = 1,
};

class SmartImageDecoder : public QObject, public QRunnable
{
Q_OBJECT

public:
    SmartImageDecoder(QSharedPointer<Image> image, QByteArray arr = QByteArray());
    ~SmartImageDecoder() override;
    
    SmartImageDecoder(const SmartImageDecoder&) = delete;
    SmartImageDecoder& operator=(const SmartImageDecoder&) = delete;
    
    QSharedPointer<Image> image();
    QImage decodedImage();
    QString errorMessage();
    QString latestMessage();
    DecodingState decodingState() const;

    void decode(DecodingState targetState, QSize desiredResolution = QSize(), QRect roiRect = QRect());
    QFuture<DecodingState> decodeAsync(DecodingState targetState, Priority prio, QSize desiredResolution = QSize(), QRect roiRect = QRect());
    
    void open();
    void init();
    void run() override;
    void reset();
    virtual void close();
    
protected:
    virtual void decodeHeader(const unsigned char* buffer, qint64 nbytes) = 0;
    virtual QImage decodingLoop(QSize desiredResolution, QRect roiRect) = 0;

    void connectNotify(const QMetaMethod& signal) override;
    
    void cancelCallback();
    void setDecodingState(DecodingState state);
    
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