
#pragma once

#include "DecodingState.hpp"
#include <memory>
#include <QWidget>
#include <QFuture>

class ANPV;

class ProgressIndicatorHelper : public QObject
{
    Q_OBJECT
    struct Impl;
    std::unique_ptr<Impl> d;

public:
    ProgressIndicatorHelper(QObject *parent = nullptr);
    ~ProgressIndicatorHelper();
    void drawProgressIndicator(QPainter* localPainter, const QRect& bounds, const QFutureWatcher<DecodingState> &future);

public slots:
    void startRendering();
    void stopRendering();

signals:
    void needsRepaint();
};
