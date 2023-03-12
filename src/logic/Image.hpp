
#pragma once

#include <QObject>
#include <QRunnable>
#include <QColorSpace>
#include <QSize>
#include <QPixmap>
#include <QImage>
#include <QIcon>
#include <QString>
#include <QFileInfo>
#include <QFuture>
#include <QTransform>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstdint>

#include "DecodingState.hpp"
#include "AfPointOverlay.hpp"

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
friend class SmartPngDecoder;
friend class SmartTiffDecoder;
friend class CziDecoder;
friend class MySleepyImageDecoder;

public:
    Image(const QFileInfo&);
    ~Image() override;
    
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    
    bool hasDecoder() const;
    
    const QFileInfo& fileInfo() const;
    QSize size() const;
    QRect fullResolutionRect() const;
    
    QTransform userTransform() const;
    void setUserTransform(QTransform);

    Qt::CheckState checked();
    void setChecked(Qt::CheckState b);
    
    QImage thumbnail();
    QPixmap thumbnailTransformed(int height);
    QIcon icon();
    
    QSharedPointer<ExifWrapper> exif();
    
    QColorSpace colorSpace();
    QString namedColorSpace();
    
    QString formatInfoString();

    QString fileExtension() const;
    bool isRaw() const;
    bool hasEquallyNamedJpeg() const;
    bool hasEquallyNamedTiff() const;

    DecodingState decodingState() const;
    QImage decodedImage();
    QString errorMessage();

    std::optional<std::tuple<std::vector<AfPoint>, QSize>> cachedAutoFocusPoints();

public slots:
    void lookupIconFromFileType();

signals:
    void decodingStateChanged(Image* self, quint32 newState, quint32 oldState);
    void thumbnailChanged(Image* self, QImage);
    void decodedImageChanged(Image* self, QImage img, QTransform sc);
    void previewImageUpdated(Image* self, QRect r);
    void checkStateChanged(Image* self, int newState, int oldState);

protected:
    void connectNotify(const QMetaMethod& signal) override;

    void setSize(QSize);
    void setThumbnail(QImage);
    void setIcon(QIcon ico);
    void setExif(QSharedPointer<ExifWrapper>);
    void setColorSpace(QColorSpace);
    
    void setDecodingState(DecodingState state);
    void setDecodedImage(QImage, QTransform s=QTransform());
    void setErrorMessage(const QString&);
    
    void updatePreviewImage(const QRect& r);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
