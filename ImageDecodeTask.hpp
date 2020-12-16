
#pragma once

#include <QRunnable>
#include <QObject>
#include <QSharedPointer>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;

class ImageDecodeTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ImageDecodeTask(QSharedPointer<SmartImageDecoder> d, DecodingState targetState);
    ~ImageDecodeTask() override;
    void run() override;
    void cancel() noexcept;
    void shutdown() noexcept;

signals:    
    // emitted before the thread exits the decoding task
    void finished(ImageDecodeTask* self);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};


