
#pragma once

#include <QRunnable>
#include <QObject>
#include <memory>

class SmartImageDecoder;

class ImageDecodeTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ImageDecodeTask(std::shared_ptr<SmartImageDecoder> d);
    ~ImageDecodeTask();
    void run() override;
    void cancel() noexcept;

signals:
    void finished(ImageDecodeTask* self);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};


