
#pragma once

#include <memory>
#include <QLabel>

class ExifWrapper;

class ExifOverlay : QLabel
{
public:
    ExifOverlay(QWidget* parent = nullptr);
    ~ExifOverlay();

    void setMetadata(ExifWrapper*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
