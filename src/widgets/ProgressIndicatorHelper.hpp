
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
    QPixmap getProgressIndicator(const QFutureWatcher<DecodingState> &future);

public slots:
    void startRendering();
    void stopRendering();

signals:
    void needsRepaint();
};
