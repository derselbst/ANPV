#include "DocumentView.hpp"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFlags>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QWindow>
#include <QGuiApplication>
#include <QThreadPool>
#include <QDebug>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

#include <vector>
#include <algorithm>

#include "AfPointOverlay.hpp"
#include "ExifOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "ANPV.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "xThreadGuard.hpp"

struct DocumentView::Impl
{
    ANPV* anpv;
    DocumentView* p;
    
    QTimer fovChangedTimer;
    QTransform previousFovTransform;
    
    QGraphicsScene* scene;
    MessageWidget* messageWidget;
    
    // a smoothly scaled version of the full resolution image
    std::unique_ptr<QGraphicsPixmapItem> smoothPixmapOverlay;
    
    std::unique_ptr<QGraphicsPixmapItem> thumbnailPreviewOverlay = std::make_unique<QGraphicsPixmapItem>();
    
    std::unique_ptr<QGraphicsPixmapItem> currentPixmapOverlay = std::make_unique<QGraphicsPixmapItem>();

    std::unique_ptr<AfPointOverlay> afPointOverlay;
    
    std::unique_ptr<ExifOverlay> exifOverlay = std::make_unique<ExifOverlay>(p);
    
    QFutureWatcher<DecodingState> taskFuture;
    
    // the latest image decoder, the same that displays the current image
    // we need to keep a "backup" of this to avoid it being deleted when its deocing task finishes
    // deleting the image decoder would invalidate the Pixmap, but the user may still want to navigate within it
    QSharedPointer<SmartImageDecoder> currentImageDecoder;
    
    // the full resolution image currently displayed in the scene
    QPixmap currentDocumentPixmap;
    
    Impl(DocumentView* parent) : p(parent)
    {}
    
    ~Impl()
    {
        currentImageDecoder = nullptr;
    }
    
    void clearScene()
    {
        removeSmoothPixmap();
        
        // clear the scene without deleting anything
        QList<QGraphicsItem*> L = scene->items();
        while (!L.empty())
        {
            scene->removeItem(L.takeFirst());
        }
        
        currentDocumentPixmap = QPixmap();
        currentPixmapOverlay->setPixmap(currentDocumentPixmap);
        
        if(!taskFuture.isFinished())
        {
            taskFuture.cancel();
            taskFuture.waitForFinished();
            // We must emit finished() manually here, because the next setFuture() call would prevent finished() signal to be emitted for this current future.
            emit taskFuture.finished();
            // Prevent emitting the finished signal twice, in case the next call to setFuture() does not happen, dugh...
            taskFuture.setFuture(QFuture<DecodingState>());
        }
        
        if(currentImageDecoder)
        {
            currentImageDecoder->disconnect(p);
            currentImageDecoder->releaseFullImage();
            currentImageDecoder = nullptr;
        }
        
        afPointOverlay = nullptr;
        
        scene->invalidate();
        
        messageWidget->hide();
        exifOverlay->hide();
    }
    
    void onViewportChanged(QTransform newTransform)
    {
        if(newTransform != previousFovTransform)
        {
            fovChangedTimer.start();
            previousFovTransform = newTransform;
            removeSmoothPixmap();
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
    
    void createSmoothPixmap()
    {
        xThreadGuard g(p);
        if (currentDocumentPixmap.isNull())
        {
            return;
        }
        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        // get the area of what the user sees
        QRect viewportRect = p->viewport()->rect();

        // and map that rect to scene coordinates
        QRectF viewportRectScene = p->mapToScene(viewportRect).boundingRect();

        // the user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void
        QRectF visPixRect = viewportRectScene.intersected(currentPixmapOverlay->sceneBoundingRect());

        // the "inverted zoom factor"
        // 1.0 means the pixmap is shown at native size
        // >1.0 means the user zoomed out
        // <1.0 mean the user zommed in and sees the individual pixels
        auto newScale = std::max(visPixRect.width() / viewportRect.width(), visPixRect.height() / viewportRect.height());

        qWarning() << newScale << "\n";

        if (newScale > 1.0)
        {
            QPixmap imgToScale;

            if (viewportRectScene.contains(currentPixmapOverlay->sceneBoundingRect()))
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
            // Optimization for huge gigapixel images: before applying the smooth transformation, first scale it down to double
            // window resolution size with fast nearest neighbour transform.
            QPixmap fastDownScaled = imgToScale.scaled(viewportRect.size() * 2, Qt::KeepAspectRatio, Qt::FastTransformation);
            QPixmap scaled = fastDownScaled.scaled(viewportRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

            smoothPixmapOverlay = std::make_unique<QGraphicsPixmapItem>(std::move(scaled));
            smoothPixmapOverlay->setPos(visPixRect.topLeft());
            smoothPixmapOverlay->setScale(newScale);
            scene->addItem(smoothPixmapOverlay.get());
        }
        else
        {
            qDebug() << "Skipping smooth pixmap scaling: Too far zoomed in";
        }
        
        QGuiApplication::restoreOverrideCursor();
    }
    
    void addThumbnailPreview(QPixmap thumb, QSize fullImageSize)
    {
        if(!thumb.isNull())
        {
            auto newScale = std::max(fullImageSize.width() * 1.0 / thumb.width(), fullImageSize.height() * 1.0 / thumb.height());

            thumbnailPreviewOverlay->setPixmap(thumb);
            thumbnailPreviewOverlay->setScale(newScale);
            
            scene->addItem(thumbnailPreviewOverlay.get());
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
    
    void setDocumentError(SmartImageDecoder* sid)
    {
        setDocumentError(sid->errorMessage());
    }
    
    void setDocumentError(QString error)
    {
        messageWidget->setText(error);
        messageWidget->setMessageType(MessageWidget::MessageType::Error);
        messageWidget->setIcon(QIcon::fromTheme("dialog-error"));
        messageWidget->show();
        this->centerMessageWidget(p->size());
    }
    
    void centerMessageWidget(QSize wndSize)
    {
        auto boxSize = messageWidget->size();
        
        auto posX = wndSize.width()/2 - boxSize.width()/2;
        auto posY = wndSize.height()/2 - boxSize.height()/2;
        messageWidget->move(posX, posY);
    }
};

DocumentView::DocumentView(ANPV *parent)
 : QGraphicsView(parent), d(std::make_unique<Impl>(this))
{
    d->anpv = parent;
    
    this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    this->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    this->setWindowState(Qt::WindowMaximized);
    this->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    
    d->scene = new QGraphicsScene(this);
    this->setScene(d->scene);
    
    d->messageWidget = new MessageWidget(this);
    d->messageWidget->setCloseButtonVisible(false);
    d->messageWidget->setWordWrap(true);
    d->messageWidget->hide();
    
    d->fovChangedTimer.setInterval(1000);
    d->fovChangedTimer.setSingleShot(true);
    connect(&d->fovChangedTimer, &QTimer::timeout, this, [&](){ emit d->createSmoothPixmap();});
    
    connect(&d->taskFuture, &QFutureWatcher<DecodingState>::finished, this,
    [&]()
    {
        QGuiApplication::restoreOverrideCursor();
    });
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

void DocumentView::resizeEvent(QResizeEvent *event)
{
    auto wndSize = event->size();
    d->centerMessageWidget(wndSize);
    
    QGraphicsView::resizeEvent(event);
}

void DocumentView::keyPressEvent(QKeyEvent *event)
{
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    switch(event->key())
    {
        case Qt::Key_Escape:
            d->clearScene();
            d->anpv->showThumbnailView();
            break;
        case Qt::Key_Space:
            if(d->currentImageDecoder)
            {
                emit requestNext(d->currentImageDecoder->fileInfo().absoluteFilePath());
            }
            break;
        case Qt::Key_Backspace:
            if(d->currentImageDecoder)
            {
                emit requestPrev(d->currentImageDecoder->fileInfo().absoluteFilePath());
            }
            break;
        default:
            QGraphicsView::keyPressEvent(event);
            break;
    }
    QGuiApplication::restoreOverrideCursor();
}

void DocumentView::onImageRefinement(SmartImageDecoder* dec, QImage img)
{
    if(dec != this->d->currentImageDecoder.data())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }
    
    d->removeSmoothPixmap();
    d->currentDocumentPixmap = QPixmap::fromImage(img, Qt::NoFormatConversion);
    d->currentPixmapOverlay->setPixmap(d->currentDocumentPixmap);
    d->scene->invalidate();
}

void DocumentView::onDecodingStateChanged(SmartImageDecoder* dec, quint32 newState, quint32 oldState)
{
    if(dec != this->d->currentImageDecoder.data())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }
    
    switch (newState)
    {
    case DecodingState::Ready:
        break;
    case DecodingState::Metadata:
        this->setSceneRect(QRectF(QPointF(0,0), dec->size()));
        if(d->anpv->viewMode() == ViewMode::Fit)
        {
            this->resetTransform();
            this->fitInView(QRectF(QPointF(0,0), dec->size()), Qt::KeepAspectRatio);
        }
        this->setTransform(dec->exif()->transformMatrix(), true);
        d->addThumbnailPreview(dec->thumbnail(), dec->size());
        d->exifOverlay->setMetadata(dec->exif());
        break;
    case DecodingState::PreviewImage:
        if (oldState == DecodingState::Metadata)
        {
            d->scene->addItem(d->currentPixmapOverlay.get());
            d->addAfPoints(dec->exif()->autoFocusPoints());
        }
        break;
    case DecodingState::FullImage:
        this->onImageRefinement(this->d->currentImageDecoder.data(), dec->image());
        d->createSmoothPixmap();
        break;
    case DecodingState::Error:
        d->currentDocumentPixmap = QPixmap();
        d->setDocumentError(dec);
        [[fallthrough]];
    case DecodingState::Cancelled:
        break;
    default:
        break;
    }
}

void DocumentView::loadImage(QString url)
{
    d->clearScene();
    
    QFileInfo info(url);
    
    if(!info.exists())
    {
        d->setDocumentError(QString("No such file %1").arg(info.absoluteFilePath()));
        return;
    }
    
    if(!info.isReadable())
    {
        QString name = info.fileName();
        d->setDocumentError(QString("No permission to read file %1").arg(name));
        return;
    }
    
    QSharedPointer<SmartImageDecoder> dec = DecoderFactory::globalInstance()->getDecoder(info);
    if(!dec)
    {
        QString name = info.fileName();
        d->setDocumentError(QString("Could not find a decoder for file %1").arg(name));
        return;
    }
    
    this->loadImage(std::move(dec));
}

void DocumentView::loadImage(QSharedPointer<SmartImageDecoder>&& dec)
{
    d->clearScene();
    d->currentImageDecoder = std::move(dec);
    this->loadImage();
}

void DocumentView::loadImage(const QSharedPointer<SmartImageDecoder>& dec)
{
    d->clearScene();
    d->currentImageDecoder = dec;
    this->loadImage();
}

void DocumentView::loadImage()
{
    QObject::connect(d->currentImageDecoder.data(), &SmartImageDecoder::imageRefined, this, &DocumentView::onImageRefinement);
    QObject::connect(d->currentImageDecoder.data(), &SmartImageDecoder::decodingStateChanged, this, &DocumentView::onDecodingStateChanged);
   
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    auto fut = d->currentImageDecoder->decodeAsync(DecodingState::FullImage);
    d->taskFuture.setFuture(fut);
    d->anpv->addBackgroundTask(ProgressGroup::Image, fut);
}

QFileInfo DocumentView::currentFile()
{
    if(d->currentImageDecoder)
    {
        return d->currentImageDecoder->fileInfo();
    }
    return QFileInfo();
}
