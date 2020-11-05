
#pragma once

#include <QRunnable>
#include <memory>

class SmartImageDecoder;

class ImageDecodeTask : public QRunnable
{
public:
    ImageDecodeTask(SmartImageDecoder* d);
    ~ImageDecodeTask();
    void run() override;
    void cancel() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};


