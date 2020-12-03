
#pragma once

#include "DecodingState.hpp"
#include "SmartImageDecoder.hpp"

#include <QObject>
#include <QSize>
#include <QFile>
#include <functional>


class SmartTiffDecoder : public SmartImageDecoder
{
Q_OBJECT

public:
    SmartTiffDecoder(const QFileInfo&);
    ~SmartTiffDecoder() override;

    SmartTiffDecoder(const SmartTiffDecoder&) = delete;
    SmartTiffDecoder& operator=(const SmartTiffDecoder&) = delete;
    
    void releaseFullImage() override;

protected:
    QSize size() override;
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    void decodingLoop(DecodingState state) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
