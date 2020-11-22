
#pragma once

#include <memory>
#include <QMainWindow>
#include <QImage>

class ANPV;
class QGraphicsScene;
class QWidget;
class QPixmap;
class QWheelEvent;
class QFileSystemModel;
class QEvent;
class SmartImageDecoder;

class ThumbnailView : public QMainWindow
{
Q_OBJECT

public:
    ThumbnailView(QFileSystemModel* model, ANPV *parent);
    ~ThumbnailView() override;
    
public slots:
    void changeDir(const QString& dir);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
