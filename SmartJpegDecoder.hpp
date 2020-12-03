
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"

#include <QObject>
#include <QSize>
#include <QFile>
#include <functional>


class SmartJpegDecoder : public SmartImageDecoder
{
Q_OBJECT

public:
    SmartJpegDecoder(const QFileInfo&);
    ~SmartJpegDecoder() override;

    SmartJpegDecoder(const SmartJpegDecoder&) = delete;
    SmartJpegDecoder& operator=(const SmartJpegDecoder&) = delete;
    
    void releaseFullImage() override;
    
protected:
    QSize size() override;
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    void decodingLoop(DecodingState state) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
