
#pragma once

#include "DecodingState.hpp"
#include <memory>
#include <QWidget>
#include <QFuture>

class ANPV;

class CancellableProgressWidget : public QWidget
{
Q_OBJECT
    struct Impl;
    std::unique_ptr<Impl> d;
    
public:
    CancellableProgressWidget(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~CancellableProgressWidget();
    bool isFinished();
    void setFuture(const QFuture<DecodingState>& future);
    
signals:
    void expired(CancellableProgressWidget*);
};
