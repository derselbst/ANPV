
#pragma once

#include <memory>
#include <QMainWindow>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>

#include "DecodingState.hpp"
#include "ANPV.hpp"

class Image;
class QSplashScreen;
class SmartImageDecoder;
class CancellableProgressWidget;

template<typename T>
class QFuture;

class MainWindow : public QMainWindow
{
Q_OBJECT

public:
    MainWindow(QSplashScreen *splash);
    ~MainWindow() override;

    void readSettings();
    void setBackgroundTask(const QFuture<DecodingState>& fut);
    
public slots:
    void hideProgressWidget(CancellableProgressWidget* w);
    void setCurrentIndex(QSharedPointer<Image>);

protected:
    bool event(QEvent* evt) override;
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
