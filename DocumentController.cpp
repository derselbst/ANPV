#include "DocumentController.hpp"

#include "DocumentView.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"
#include "DecoderFactory.hpp"

#include <QGraphicsScene>
#include <QDebug>
#include <QThreadPool>
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
    
    // a container were we store all tasks that need to be processed
    std::vector<std::unique_ptr<ImageDecodeTask>> taskContainer;
    
    // a shortcut to the most recent task queued
    ImageDecodeTask* currentDecodeTask=nullptr;

    // the latest image decoder, the same that displays the current image
    // we need to keep a "backup" of this to avoid it being deleted when its deocing task finishes
    // deleting the image decoder would invalidate the Pixmap, but the user may still want to navigate within it
    std::shared_ptr<SmartImageDecoder> currentImageDecoder;
    
    // the full resolution image currently displayed in the scene
    QPixmap currentDocumentPixmap;
    
    // a smoothly scaled version of the full resolution image
    std::unique_ptr<QGraphicsPixmapItem> smoothPixmapOverlay;
    
    std::unique_ptr<QGraphicsPixmapItem> thumbnailPreviewOverlay;

    std::unique_ptr<QGraphicsSimpleTextItem> textOverlay;

    Impl() : scene(std::make_unique<QGraphicsScene>()), view(std::make_unique<DocumentView>(scene.get()))
    {}
    
    ~Impl()
    {
        for(size_t i=0; i < taskContainer.size(); i++)
        {
            taskContainer[i]->cancel();
        }
        
        if(!QThreadPool::globalInstance()->waitForDone(5000))
        {
            qWarning() << "Waited over 5 seconds for the thread pool to finish, giving up.";
        }
        
        taskContainer.clear();
    }

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
    
    void setDocumentError(SmartImageDecoder* sid)
    {
        QString error = sid->errorMessage();
        textOverlay = std::make_unique<QGraphicsSimpleTextItem>(error);
        textOverlay->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        scene->addItem(textOverlay.get());
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
    case DecodingState::Error:
        d->setDocumentError(self);
        break;
    default:
        break;
    }
}

void DocumentController::onDecodingProgress(SmartImageDecoder*, int progress, QString message)
{
    qWarning() << message.toStdString().c_str() << progress << " %";
}

void DocumentController::onDecodingTaskFinished(ImageDecodeTask* t)
{
    auto result = std::find_if(d->taskContainer.begin(),
                               d->taskContainer.end(),
                               [&](std::unique_ptr<ImageDecodeTask>& other)
                               { return other.get() == t;}
                              );
    if (result != d->taskContainer.end())
    {
        d->taskContainer.erase(result);
    }
    else
    {
        qWarning() << "ImageDecodeTask '" << t << "' not found in container.";
    }
    
    if(d->currentDecodeTask == t)
    {
        d->currentDecodeTask = nullptr;
    }
}

void DocumentController::loadImage(QString url)
{
    d->scene->clear();
    
    if(d->currentDecodeTask)
    {
        d->currentDecodeTask->cancel();
        d->currentDecodeTask = nullptr;
    }
    d->currentDocumentPixmap = QPixmap();
    
    d->currentImageDecoder = DecoderFactory::load(url, this);
    
    d->taskContainer.emplace_back(std::make_unique<ImageDecodeTask>(d->currentImageDecoder));
    auto* task = d->taskContainer.back().get();
    d->currentDecodeTask = task;
    connect(task, &ImageDecodeTask::finished, this, &DocumentController::onDecodingTaskFinished, Qt::QueuedConnection);
    
    QThreadPool::globalInstance()->start(task);
}
