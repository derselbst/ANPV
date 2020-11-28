
#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;
class DocumentView;
class ImageDecodeTask;

class DecoderFactory : public QObject
{
Q_OBJECT

public:
    static DecoderFactory* globalInstance();

    std::unique_ptr<SmartImageDecoder> getDecoder(QString url);
    void configureDecoder(SmartImageDecoder* dec, DocumentView* dc);
    std::shared_ptr<ImageDecodeTask> createDecodeTask(std::shared_ptr<SmartImageDecoder> dec, DecodingState targetState);
    void cancelDecodeTask(std::shared_ptr<ImageDecodeTask> task);

private:
    DecoderFactory();
    ~DecoderFactory();
    
    struct Impl;
    std::unique_ptr<Impl> d;
};
