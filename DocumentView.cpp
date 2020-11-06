#include "DocumentView.hpp"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFlags>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QWindow>

#include <future>


struct DocumentView::Impl
{    
    DocumentView* p;
    QTimer fovChangedTimer;
    QTransform previousFovTransform;
    
    Impl(DocumentView* parent) : p(parent)
    {
        fovChangedTimer.setInterval(1000);
        fovChangedTimer.setSingleShot(true);
    }
    
    void onViewportChanged(QTransform newTransform)
    {
        if(newTransform != previousFovTransform)
        {
            fovChangedTimer.start();
            previousFovTransform = newTransform;
            emit p->fovChangedBegin();
        }
    }
};

DocumentView::DocumentView(QGraphicsScene *scene, QWidget *parent)
 : QGraphicsView(scene, parent), d(std::make_unique<Impl>(this))
{
    this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    this->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    this->setWindowState(Qt::WindowMaximized);
    
    connect(&d->fovChangedTimer, &QTimer::timeout, this, [&](){ emit this->fovChangedEnd();});
}

DocumentView::~DocumentView() = default;

void DocumentView::zoomIn()
{
    this->scale(1.2, 1.2);
}

void DocumentView::zoomOut()
{
    this->scale(1 / 1.2, 1 / 1.2);
}

void DocumentView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Plus:
        this->zoomIn();
        break;
    case Qt::Key_Minus:
        this->zoomOut();
        break;
    case Qt::Key_A:
        this->setRenderHint(QPainter::Antialiasing);
        break;
    case Qt::Key_S:
        this->setRenderHint(QPainter::SmoothPixmapTransform);
        break;
    case Qt::Key_Q:
        this->setRenderHint(QPainter::Antialiasing, false);
        break;
    case Qt::Key_W:
        this->setRenderHint(QPainter::SmoothPixmapTransform, false);
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        return;
    }
    
    event->accept();
}

void DocumentView::wheelEvent(QWheelEvent *event)
{
    auto angleDelta = event->angleDelta();

    if (event->modifiers() & Qt::ControlModifier)
    {
        // zoom
        if(angleDelta.y() > 0)
        {
            this->zoomIn();
            event->accept();
            return;
        }
        else if(angleDelta.y() < 0)
        {
            this->zoomOut();
            event->accept();
            return;
        }
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
//         angleDelta = QPoint(angleDelta.y(), angleDelta.x());
//         QWheelEvent(event->position(), event->globalPosition(), event->pixelDelta(), angleDelta, event->buttons(), event->modifiers(), event->phase(), event->source());
    }

    QGraphicsView::wheelEvent(event);
}

bool DocumentView::viewportEvent(QEvent* event)
{
    d->onViewportChanged(this->viewportTransform());
    return QGraphicsView::viewportEvent(event);
}
