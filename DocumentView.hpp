
#pragma once

#include <memory>
#include <QGraphicsView>

class QGraphicsScene;
class QWidget;
class QPixmap;

class DocumentView : public QGraphicsView
{
Q_OBJECT

public:
    DocumentView(QGraphicsScene *scene, QWidget *parent = nullptr);
    ~DocumentView() override;

public slots:
    void zoomIn();
    void zoomOut();
    
signals:
    void fovChangedBegin();
    void fovChangedEnd();
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
