
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

class ExifWrapper;
class QMetaMethod;

/**
 * Thread-safe container for most of once-decoded-never-changing information of an image.
 * Used by the main-thread to know about most useful information.
 * Fed by the background decoding thread.
 * Will also be used for other regular files and folders.
 * In this case, DecoderFactory would simply fail to find a decoder for this "image" instance.
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
    
    QTransform defaultTransform() const;
    
    QTransform userTransform() const;
    void setUserTransform(QTransform);
    
    QPixmap thumbnail();
    QPixmap thumbnailTransformed(int height);
    QIcon icon();
    void lookupIconFromFileType();
    
    QSharedPointer<ExifWrapper> exif();
    
    QColorSpace colorSpace();
    
    QString formatInfoString();

    bool isRaw();
    bool hasEquallyNamedJpeg();
    bool hasEquallyNamedTiff();

signals:
    void thumbnailChanged();

protected:
    void setHasDecoder(bool);
    void setSize(QSize);
    void setDefaultTransform(QTransform);
    void setThumbnail(QImage);
    void setThumbnail(QPixmap);
    void setIcon(QIcon ico);
    void setExif(QSharedPointer<ExifWrapper>);
    void setColorSpace(QColorSpace);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
