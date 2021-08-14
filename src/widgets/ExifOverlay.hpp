
#pragma once

#include <memory>
#include "MessageWidget.hpp"

class Image;

class ExifOverlay : public MessageWidget
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay() override;

    void setMetadata(Image*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
