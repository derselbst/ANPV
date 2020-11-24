
#pragma once

#include <memory>
#include <QGraphicsView>
#include <QImage>

class ANPV;
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

    void loadImage(QString url);
    
public slots:
    void zoomIn();
    void zoomOut();
    void onImageRefinement(SmartImageDecoder* self, QImage img);
    void onDecodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState);
    void onDecodingProgress(SmartImageDecoder* self, int progress, QString message);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
