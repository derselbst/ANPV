
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
    SmartJpegDecoder(QString&&);
    ~SmartJpegDecoder() override;

protected:
    QSize size() override;
    void decodeHeader() override;
    void decodingLoop(DecodingState state) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
