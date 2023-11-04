
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"

#include <QObject>
#include <QSize>
#include <QFile>
#include <functional>


class SmartJxlDecoder : public SmartImageDecoder
{
public:
    SmartJxlDecoder(QSharedPointer<Image> image);
    ~SmartJxlDecoder() override;

    SmartJxlDecoder(const SmartJxlDecoder&) = delete;
    SmartJxlDecoder& operator=(const SmartJxlDecoder&) = delete;

protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void decodeInternal(QImage& image);
};
