
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"

#include <QObject>
#include <QSize>
#include <QFile>
#include <functional>


class CziDecoder : public SmartImageDecoder
{
public:
    CziDecoder(QSharedPointer<Image> image);
    ~CziDecoder() override;

    CziDecoder(const CziDecoder&) = delete;
    CziDecoder& operator=(const CziDecoder&) = delete;

protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    QImage decodingLoop(QSize desiredResolution, QRect roiRect) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void decodeInternal(int imagePageToDecode, QImage& image, QRect roi, QTransform, QSize desiredResolution, bool quiet);
};
