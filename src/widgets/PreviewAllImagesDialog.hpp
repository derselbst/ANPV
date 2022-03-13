
#pragma once

#include "DecodingState.hpp"
#include <memory>
#include <QWidget>
#include <QDialog>
#include <QFuture>

class ANPV;

class PreviewAllImagesDialog : public QDialog
{
Q_OBJECT
    struct Impl;
    std::unique_ptr<Impl> d;
    
public:
    PreviewAllImagesDialog(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~PreviewAllImagesDialog();
    void setImageHeight(int height);
    int imageHeight();
};
