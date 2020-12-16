
#pragma once

#include <QObject>
#include <QFileInfo>
#include <QSharedPointer>
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

    ~DecoderFactory() override;
    QSharedPointer<SmartImageDecoder> getDecoder(const QFileInfo& url);
    void configureDecoder(SmartImageDecoder* dec, DocumentView* dc);
    QSharedPointer<ImageDecodeTask> createDecodeTask(QSharedPointer<SmartImageDecoder> dec, DecodingState targetState);
    bool cancelDecodeTask(QSharedPointer<ImageDecodeTask>& task);

signals:
    void noMoreTasksLeft();
    
private:
    DecoderFactory();
    
    struct Impl;
    std::unique_ptr<Impl> d;
};
