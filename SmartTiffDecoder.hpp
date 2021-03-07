
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
    SmartTiffDecoder(const QFileInfo&, QByteArray arr=QByteArray());
    ~SmartTiffDecoder() override;

    SmartTiffDecoder(const SmartTiffDecoder&) = delete;
    SmartTiffDecoder& operator=(const SmartTiffDecoder&) = delete;

protected:
    void decodeHeader(const unsigned char* buffer, qint64 nbytes) override;
    QImage decodingLoop(DecodingState state) override;
    void close() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void decodeInternal(int imagePageToDecode, QImage& image, bool);
};
