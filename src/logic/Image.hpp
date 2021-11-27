
#pragma once

#include <QObject>
#include <QRunnable>
#include <QColorSpace>
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

#include "DecodingState.hpp"

class ExifWrapper;
class QMetaMethod;

/**
 * Thread-safe container for most of decoded information of an image.
 * Owned and used by the main-thread to know about most useful information.
 * Fed by the background decoding thread.
 * Will also be used for other regular files and folders.
 * In this case, DecoderFactory would simply fail to find a decoder for *this and hasDecoder() will stay false.
 */
class Image : public QObject
{
Q_OBJECT

friend class SmartImageDecoder;
friend class SmartJpegDecoder;
friend class SmartTiffDecoder;

public:
    Image(const QFileInfo&);
    ~Image() override;
    
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    
    bool hasDecoder() const;
    
    const QFileInfo& fileInfo() const;
    QSize size() const;
    
    QTransform userTransform() const;
    void setUserTransform(QTransform);
    
    QImage thumbnail();
    QPixmap thumbnailTransformed(int height);
    QIcon icon();
    void lookupIconFromFileType();
    
    QSharedPointer<ExifWrapper> exif();
    
    QColorSpace colorSpace();
    QString namedColorSpace();
    
    QString formatInfoString();

    bool isRaw();
    bool hasEquallyNamedJpeg();
    bool hasEquallyNamedTiff();

    DecodingState decodingState() const;
    QImage decodedImage();
    QString errorMessage();

signals:
    void decodingStateChanged(Image* self, quint32 newState, quint32 oldState);
    void thumbnailChanged(Image* self, QImage);
    void decodedImageChanged(Image* self, QImage img);

protected:
    void connectNotify(const QMetaMethod& signal) override;

    void setSize(QSize);
    void setThumbnail(QImage);
    void setThumbnail(QPixmap);
    void setIcon(QIcon ico);
    void setExif(QSharedPointer<ExifWrapper>);
    void setColorSpace(QColorSpace);
    
    void setDecodingState(DecodingState state);
    void setDecodedImage(QImage);
    void setErrorMessage(const QString&);
    
    void updatePreviewImage(QImage&& img);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
