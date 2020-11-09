#include "DocumentController.hpp"

#include "DocumentView.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "AfPointOverlay.hpp"

#include <QGraphicsScene>
#include <QDebug>
#include <QThreadPool>
#include <QGraphicsPixmapItem>
#include <QGuiApplication>
#include <QApplication>
#include <QWindow>
#include <QScreen>
#include <QDesktopWidget>
#include <QProgressBar>
#include <QStatusBar>
#include <QMainWindow>


struct DocumentController::Impl
{
    std::unique_ptr<QGraphicsScene> scene = std::make_unique<QGraphicsScene>();
    std::unique_ptr<DocumentView> view;
    
    QStatusBar* statusBar = nullptr;
    QProgressBar* progressBar = nullptr;
    
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
    
    std::unique_ptr<QGraphicsPixmapItem> thumbnailPreviewOverlay = std::make_unique<QGraphicsPixmapItem>();
    
    std::unique_ptr<QGraphicsPixmapItem> currentPixmapOverlay = std::make_unique<QGraphicsPixmapItem>();

    std::unique_ptr<QGraphicsSimpleTextItem> textOverlay = std::make_unique<QGraphicsSimpleTextItem>();
    
    std::unique_ptr<AfPointOverlay> afPointOverlay;

    Impl()
    {
        view = std::make_unique<DocumentView>(scene.get());
        view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    }
    
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
    
    void addThumbnailPreview(QImage thumb, QSize fullImageSize)
    {
        if(!thumb.isNull())
        {
            auto newScale = std::max(fullImageSize.width() * 1.0 / thumb.width(), fullImageSize.height() * 1.0 / thumb.height());

            thumbnailPreviewOverlay->setPixmap(QPixmap::fromImage(thumb));
            thumbnailPreviewOverlay->setScale(newScale);
            
            view->fitInView(thumbnailPreviewOverlay.get(), Qt::KeepAspectRatio);
            scene->addItem(thumbnailPreviewOverlay.get());
        }
    }

    void removeSmoothPixmap()
    {
        if (smoothPixmapOverlay)
        {
            scene->removeItem(smoothPixmapOverlay.get());
            smoothPixmapOverlay = nullptr;
        }
    }

    void addAfPoints(std::unique_ptr<AfPointOverlay>&& afpoint)
    {
        if(afpoint)
        {
            afPointOverlay = std::move(afpoint);
            afPointOverlay->setZValue(1);
            scene->addItem(afPointOverlay.get());
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
        textOverlay->setText(error);
        textOverlay->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        scene->clear();
        scene->addItem(textOverlay.get());
    }
};

DocumentController::DocumentController(QMainWindow* wnd, QObject *parent)
 : QObject(parent), d(std::make_unique<Impl>())
{
    QScreen *ps = QGuiApplication::primaryScreen();
    QRect screenres = ps->geometry();

    connect(d->view.get(), &DocumentView::fovChangedBegin, this, &DocumentController::onBeginFovChanged);
    connect(d->view.get(), &DocumentView::fovChangedEnd, this, &DocumentController::onEndFovChanged);
        
    std::unique_ptr<QProgressBar> progressBar = std::make_unique<QProgressBar>();
        progressBar->setMinimum(0);
        progressBar->setMaximum(100);
    
    d->progressBar = progressBar.release();
    wnd->statusBar()->addPermanentWidget(d->progressBar);
    wnd->setCentralWidget(d->view.get());
    
    // open the widget on the primary screen
    // by moving and resize it explicitly
    wnd->move(screenres.topLeft());
    wnd->resize(screenres.width(), screenres.height());
    wnd->setWindowState(Qt::WindowMaximized);
    wnd->setWindowTitle("ANPV");
    d->statusBar = wnd->statusBar();
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
        d->addThumbnailPreview(self->thumbnail(), self->size());
        break;
    case DecodingState::PreviewImage:
        if (oldState == DecodingState::Metadata)
        {
            d->scene->addItem(d->currentPixmapOverlay.get());
            d->addAfPoints(self->exif()->autoFocusPoints());
        }
        break;
    case DecodingState::FullImage:
        this->onImageRefinement(self->image());
        d->createSmoothPixmap();
        break;
    case DecodingState::Error:
        d->setDocumentError(self);
        break;
    default:
        break;
    }
}


void DocumentController::onImageRefinement(QImage img)
{
    d->removeSmoothPixmap();
    d->currentDocumentPixmap = QPixmap::fromImage(img, Qt::NoFormatConversion);
    d->currentPixmapOverlay->setPixmap(d->currentDocumentPixmap);
    d->scene->invalidate(d->scene->sceneRect());
}

void DocumentController::onDecodingProgress(SmartImageDecoder*, int progress, QString message)
{
    d->statusBar->showMessage(message, 0);
    d->progressBar->setValue(progress);
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
    
    d->progressBar->reset();
    d->statusBar->showMessage(QString("Opening ") + d->currentImageDecoder->fileInfo().fileName());
    
    QThreadPool::globalInstance()->start(task);
}
