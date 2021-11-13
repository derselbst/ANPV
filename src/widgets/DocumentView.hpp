
#pragma once

#include <memory>
#include <QGraphicsView>
#include <QImage>
#include <QString>
#include <QFileInfo>

class ANPV;
class Image;
class QGraphicsScene;
class QWidget;
class QPixmap;
class QWheelEvent;
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
    void loadImage(QSharedPointer<Image> image);
    void loadImage(QString url);
    void loadImage(const QSharedPointer<SmartImageDecoder>& dec);
    void loadImage(QSharedPointer<SmartImageDecoder>&& dec);
    
public slots:
    void zoomIn();
    void zoomOut();
    void onImageRefinement(SmartImageDecoder* self, QImage img);
    void onDecodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);

signals:
    void imageChanged(QSharedPointer<Image>);

protected:
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void loadImage();
    void showImage(QSharedPointer<Image> img);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
