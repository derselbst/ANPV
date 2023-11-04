
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"


class SmartPngDecoder : public SmartImageDecoder
{
public:
    SmartPngDecoder(QSharedPointer<Image> image);
    ~SmartPngDecoder() override;

    SmartPngDecoder(const SmartPngDecoder&) = delete;
    SmartPngDecoder& operator=(const SmartPngDecoder&) = delete;
    
protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
