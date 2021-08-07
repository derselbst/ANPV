
#pragma once

#include <memory>
#include "MessageWidget.hpp"

class SmartImageDecoder;

class ExifOverlay : public MessageWidget
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay() override;

    void setMetadata(SmartImageDecoder*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
