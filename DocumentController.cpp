#include "DocumentController.hpp"

#include "DocumentView.hpp"
#include "SmartImageDecoder.hpp"

#include <QGraphicsScene>
#include <QDebug>
#include <QGraphicsPixmapItem>
#include <QGuiApplication>
#include <QApplication>
#include <QWindow>
#include <QScreen>
#include <QDesktopWidget>



struct DocumentController::Impl
{
    std::unique_ptr<QGraphicsScene> scene;
    std::unique_ptr<DocumentView> view;

    QPixmap currentDocumentPixmap;
    std::unique_ptr<QGraphicsPixmapItem> smoothPixmapOverlay;



    Impl() : scene(std::make_unique<QGraphicsScene>()), view(std::make_unique<DocumentView>(scene.get()))
    {}

    void removeSmoothPixmap()
    {
        if (smoothPixmapOverlay)
        {
            scene->removeItem(smoothPixmapOverlay.get());
            smoothPixmapOverlay.reset(nullptr);
        }
    }

    void createSmoothPixmap()
    {
        if (currentDocumentPixmap.isNull())
        {
            return;
        }

        // get the area of what the user sees
        QRect viewportRect = view->viewport()->rect();

        // and map that rect to scene coordinates
        QRectF viewportRectScene = view->mapToScene(viewportRect).boundingRect();

        // the user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void
        QRectF visPixRect = viewportRectScene.intersected(view->sceneRect());

        // the "inverted zoom factor"
        // 1.0 means the pixmap is shown at native size
        // >1.0 means the user zoomed out
        // <1.0 mean the user zommed in and sees the individual pixels
        auto newScale = std::max(visPixRect.width() / viewportRect.width(), visPixRect.height() / viewportRect.height());

        qWarning() << newScale << "\n";

        if (newScale > 1.0)
        {
            QPixmap imgToScale;

            if (viewportRectScene.contains(view->sceneRect()))
            {
                // the user sees the entire image
                imgToScale = currentDocumentPixmap;
            }
            else
            {
                // the user sees a part of the image
                // crop the image to the visible part, so we don't need to scale the entire one
                imgToScale = currentDocumentPixmap.copy(visPixRect.toAlignedRect());
            }
            QPixmap scaled = imgToScale.scaled(viewportRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

            smoothPixmapOverlay = std::make_unique<QGraphicsPixmapItem>(std::move(scaled));
            smoothPixmapOverlay->setPos(visPixRect.topLeft());
            smoothPixmapOverlay->setScale(newScale);
            scene->addItem(smoothPixmapOverlay.get());
        }
        else
        {
            qDebug() << "Skipping smooth pixmap scaling: Too far zoomed in";
        }
    }
};

DocumentController::DocumentController(QObject *parent)
 : QObject(parent), d(std::make_unique<Impl>())
{
    d->scene->addRect(QRectF(0, 0, 100, 100));

    connect(d->view.get(), &DocumentView::fovChangedBegin, this, &DocumentController::onBeginFovChanged);
    connect(d->view.get(), &DocumentView::fovChangedEnd, this, &DocumentController::onEndFovChanged);
    
    QScreen *ps = QGuiApplication::primaryScreen();
    QRect screenres = ps->geometry();

    // open the widget on the primary screen
    // setScreen() is not sufficient on Windows
    d->view->windowHandle()->setScreen(ps);

    // that's why we need to move and resize it explicitly
    d->view->move(screenres.topLeft());
    d->view->resize(screenres.width(), screenres.height());

    d->view->show();
}

DocumentController::~DocumentController() = default;


DocumentView* DocumentController::documentView()
{
    return d->view.get();
}

void DocumentController::onBeginFovChanged()
{
    d->removeSmoothPixmap();
}

void DocumentController::onEndFovChanged()
{
    d->createSmoothPixmap();
}

void DocumentController::onDecodingStateChanged(SmartImageDecoder* self, quint32 newState, quint32 oldState)
{
    switch (newState)
    {
    case DecodingState::Metadata:
        break;
    case DecodingState::PreviewImage:
        if (oldState == DecodingState::Metadata)
        {
            d->currentDocumentPixmap = QPixmap::fromImage(self->image());
            auto* pixitem = d->scene->addPixmap(d->currentDocumentPixmap);
            d->view->fitInView(pixitem, Qt::KeepAspectRatio);
            break;
        }
        else
        {
            d->removeSmoothPixmap();
            d->scene->invalidate(d->scene->sceneRect());
        }
        break;
    case DecodingState::FullImage:
        d->removeSmoothPixmap();
        d->scene->invalidate(d->scene->sceneRect());
        d->createSmoothPixmap();
        break;
    default:
        break;
    }
}

void DocumentController::onDecodingProgress(SmartImageDecoder* self, int progress, QString message)
{
    qWarning() << message.toStdString().c_str() << progress << " %";
}
