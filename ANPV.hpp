
#pragma once

#include <memory>
#include <QMainWindow>
#include <QString>

#include "DecodingState.hpp"

class QSplashScreen;

class ANPV : public QMainWindow
{
Q_OBJECT

public:
    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    void notifyProgress(int progress, QString message);
    void notifyDecodingState(DecodingState state);
    
public slots:
    void showImageView();
    void showThumbnailView();
    void loadImage(QString str);
    void setThumbnailDir(QString str);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
