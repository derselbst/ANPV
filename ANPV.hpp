
#pragma once

#include <memory>
#include <QMainWindow>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>

#include "DecodingState.hpp"

class QUndoCommand;
class MoveFileCommand;
class QSplashScreen;
class SmartImageDecoder;
class CancellableProgressWidget;

template<typename T>
class QFuture;

enum ProgressGroup
{
    Directory,
    Image,
};

enum ViewMode
{
    None,
    Fit,
    CenterAf,
};

class ANPV : public QMainWindow
{
Q_OBJECT

public:
    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    void addBackgroundTask(int group, const QFuture<DecodingState>& fut);
    void hideProgressWidget(CancellableProgressWidget* w);
    
    void moveFilesSlot(const QString& targetDir);
    void moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir);
    
    bool shouldHideProgressWidget();
    ViewMode viewMode();
    
public slots:
    void showImageView();
    void showThumbnailView();
    void loadImage(QFileInfo str);
    void loadImage(QSharedPointer<SmartImageDecoder> dec);
    void setThumbnailDir(QString str);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
