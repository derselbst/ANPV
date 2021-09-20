
#pragma once

#include <memory>
#include <QMainWindow>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>

#include "DecodingState.hpp"
#include "ANPV.hpp"

class QUndoCommand;
class MoveFileCommand;
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
    
    void closeEvent(QCloseEvent *event) override;

    void addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut);
    
public slots:
    void hideProgressWidget(CancellableProgressWidget* w);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
