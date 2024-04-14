
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"


class MangoDecoder : public SmartImageDecoder
{
public:
    MangoDecoder(QSharedPointer<Image> image);
    ~MangoDecoder() override;

    MangoDecoder(const MangoDecoder &) = delete;
    MangoDecoder &operator=(const MangoDecoder &) = delete;

protected:
    void decodeHeader(const unsigned char *buffer, qint64 nbytes) override;
    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void decodeInternal(int imagePageToDecode, QImage &image, QRect roi, QTransform, QSize desiredResolution, bool quiet);
};