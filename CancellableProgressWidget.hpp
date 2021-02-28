
#pragma once

#include "DecodingState.hpp"
#include <memory>
#include <QWidget>
#include <QFuture>

class ANPV;

class CancellableProgressWidget : public QWidget
{
    struct Impl;
    std::unique_ptr<Impl> d;
    
public:
    CancellableProgressWidget(const QFuture<DecodingState>& future, ANPV* anpv, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~CancellableProgressWidget();
    bool isFinished();
};
