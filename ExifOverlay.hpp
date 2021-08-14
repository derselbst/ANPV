
#pragma once

#include <memory>
#include "MessageWidget.hpp"

class ExifWrapper;
class QEnterEvent;
class QEvent;

class ExifOverlay : public MessageWidget
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay() override;
    
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

    void setMetadata(ExifWrapper*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
