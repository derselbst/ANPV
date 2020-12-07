
#pragma once

#include <memory>
#include <QMainWindow>
#include <QImage>
#include <QList>

class ANPV;
class QGraphicsScene;
class QWidget;
class QPixmap;
class QWheelEvent;
class SortedImageModel;
class QEvent;
class SmartImageDecoder;

class ThumbnailView : public QMainWindow
{
Q_OBJECT

public:
    ThumbnailView(SortedImageModel* model, ANPV *parent);
    ~ThumbnailView() override;
    
    void getSelectedFiles(QList<QString>& selectedFiles, QString& sourceDir);

public slots:
    void changeDir(const QString& dir, bool skipScrollTo=false);
    void selectThumbnail(const QModelIndex& idx);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
