
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

class DocumentView : public QGraphicsView
{
Q_OBJECT

public:
    DocumentView(ANPV *parent);
    ~DocumentView() override;

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
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void loadImage();

signals:
    void requestNext(QString cur);
    void requestPrev(QString cur);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
