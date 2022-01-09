
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"

#include <QObject>
#include <QSize>
#include <QFile>
#include <functional>


class SmartJpegDecoder : public SmartImageDecoder
{
public:
    SmartJpegDecoder(QSharedPointer<Image> image);
    ~SmartJpegDecoder() override;

    SmartJpegDecoder(const SmartJpegDecoder&) = delete;
    SmartJpegDecoder& operator=(const SmartJpegDecoder&) = delete;
    
protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    QImage decodingLoop(QSize desiredResolution, QRect roiRect, QPoint& topLeft) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
