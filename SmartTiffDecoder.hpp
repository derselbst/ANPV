
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
    SmartTiffDecoder(QString&&);
    ~SmartTiffDecoder() override;

protected:
    QSize size() override;
    void decodeHeader() override;
    void decodingLoop(DecodingState state) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
