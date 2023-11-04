
#pragma once

#include <memory>
#include <QSharedPointer>
#include "MessageWidget.hpp"

class Image;
class QEnterEvent;
class QEvent;

class ExifOverlay : public MessageWidget
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay() override;
    
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

    bool setMetadata(QSharedPointer<Image>);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
