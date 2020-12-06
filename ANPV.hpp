
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

class ANPV : public QMainWindow
{
Q_OBJECT

public:
    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    void notifyProgress(int progress, QString message);
    void notifyProgress(int progress);
    void notifyDecodingState(DecodingState state);
    
    void moveFilesSlot(const QString& targetDir);
    void moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir);
    
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
