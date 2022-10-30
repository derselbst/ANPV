
#pragma once

#include "types.hpp"
#include <memory>
#include <QGraphicsView>
#include <QImage>
#include <QString>
#include <QFileInfo>
#include <QTransform>

class ANPV;
class Image;
class QGraphicsScene;
class QWidget;
class QPixmap;
class QWheelEvent;
class QMouseEvent;
class QShowEvent;
class QEvent;
class SmartImageDecoder;
class SortedImageModel;

class DocumentView : public QGraphicsView
{
Q_OBJECT

public:
    DocumentView(QWidget *parent);
    ~DocumentView() override;

    void setModel(QSharedPointer<SortedImageModel>);
    QFileInfo currentFile();
    void loadImage(const Entry_t& e);
    void loadImage(QSharedPointer<Image> image);
    void loadImage(QString url);
    void loadImage(const QSharedPointer<SmartImageDecoder>& dec);
    
public slots:
    void zoomIn();
    void zoomOut();
    void onPreviewImageUpdated(Image* img, QRect r);
    void onImageRefinement(Image* self, QImage img, QTransform);
    void onDecodingStateChanged(Image* self, quint32 newState, quint32 oldState);
    void onCheckStateChanged(Image* img, int state, int old);

signals:
    void imageChanged(QSharedPointer<Image>);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

    void loadImage();
    void showImage(QSharedPointer<Image> img);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
