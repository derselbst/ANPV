
#pragma once

#include <memory>
#include "MessageWidget.hpp"

class ExifWrapper;

class ExifOverlay : public MessageWidget
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay() override;

    void setMetadata(ExifWrapper*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
