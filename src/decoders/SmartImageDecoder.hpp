
#pragma once

#include <QRunnable>
#include <QSize>
#include <QImage>
#include <QFuture>
#include <cstdint>

#include "DecodingState.hpp"

class ExifWrapper;
class Image;
class QPainterPath;

enum class Priority : int
{
    Background = -1,
    Normal = 0,
    Important = 1,
};

/**
 * Base class for image decoders. Not a QObject and may therefore be owned by any thread, passed around as pleased.
 */
class SmartImageDecoder : public QRunnable
{
public:
    SmartImageDecoder(QSharedPointer<Image> image);
    ~SmartImageDecoder() override;

    SmartImageDecoder(const SmartImageDecoder &) = delete;
    SmartImageDecoder &operator=(const SmartImageDecoder &) = delete;

    QSharedPointer<Image> image();

    QFuture<DecodingState> decodeAsync(DecodingState targetState, Priority prio, QSize desiredResolution = QSize(), QRect roiRect = QRect());

    // open(), init(), decode(), close(), reset() must be called by the same thread!
    // they are virtual for the purpose of unit testing.
    virtual void open();
    virtual void init();
    virtual void close();

    // desiredResolution contains the requested size of the decoded image in pixels. The decoder may return an image of any size though, as it may or may not respect this.
    // roiRect contains a rectangluar "region-of-interest" that the user wants to see. This rectangle is relative to the highest resolution page available (i.e. it's a subset of Image::fullImageRect()).
    void decode(DecodingState targetState, QSize desiredResolution = QSize(), QRect roiRect = QRect());
    void reset();

    void run() override;
    void cancelOrTake(QFuture<DecodingState> taskFuture);
    void releaseFullImage();
    QRect decodedRoiRect();

    QTransform fullResToPageTransform(const QSize &desiredResolution);
    QTransform fullResToPageTransform(unsigned w, unsigned h);

    virtual const QPainterPath* imageLayout();

protected:
    virtual void decodeHeader(const unsigned char *buffer, qint64 nbytes) = 0;
    virtual QImage decodingLoop(QSize desiredResolution, QRect roiRect) = 0;

    void cancelCallback();
    void assertNotDecoding();

    void resetDecodedRoiRect();
    void updateDecodedRoiRect(const QRect &r);

    QImage allocateImageBuffer(const QSize &s, QImage::Format format);
    QImage allocateImageBuffer(uint32_t width, uint32_t height, QImage::Format format);
    void convertColorSpace(QImage &image, bool silent = false, QTransform = QTransform());

    void setDecodingState(DecodingState state);
    void setDecodingMessage(QString &&msg);
    void setDecodingProgress(int prog);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

