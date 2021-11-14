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
#include "Image.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "xThreadGuard.hpp"

struct DocumentView::Impl
{
    DocumentView* p;
    
    QTimer fovChangedTimer;
    QTransform previousFovTransform;
    
    QPointer<QGraphicsScene> scene;
    QPointer<MessageWidget> messageWidget;
    
    // a smoothly scaled version of the full resolution image
    QGraphicsPixmapItem* smoothPixmapOverlay;
    
    QGraphicsPixmapItem* thumbnailPreviewOverlay;
    
    QGraphicsPixmapItem* currentPixmapOverlay;

    AfPointOverlay* afPointOverlay;
    
    std::unique_ptr<ExifOverlay> exifOverlay = std::make_unique<ExifOverlay>(p);
    
    QFutureWatcher<DecodingState> taskFuture;
    
    // the latest image decoder, the same that displays the current image
    // we need to keep a "backup" of this to avoid it being deleted when its deocing task finishes
    // deleting the image decoder would invalidate the Pixmap, but the user may still want to navigate within it
    QSharedPointer<SmartImageDecoder> currentImageDecoder;
    
    DecodingState latestDecodingState = DecodingState::Ready;
    
    // the full resolution image currently displayed in the scene
    QPixmap currentDocumentPixmap;
    
    // the model for the current directory needed for navigating back and forth
    QSharedPointer<SortedImageModel> model;

    Impl(DocumentView* parent) : p(parent)
    {}
    
    ~Impl()
    {
        this->clearScene();
    }
    
    void clearScene()
    {
        if(!taskFuture.isFinished())
        {
            bool taken = QThreadPool::globalInstance()->tryTake(currentImageDecoder.get());
            if(!taken)
            {
                taskFuture.cancel();
                taskFuture.waitForFinished();
            }
            taskFuture.setFuture(QFuture<DecodingState>());
        }
        
        if(currentImageDecoder)
        {
            currentImageDecoder->disconnect(p);
            currentImageDecoder->reset();
            currentImageDecoder = nullptr;
            latestDecodingState = DecodingState::Ready;
        }
        
        removeSmoothPixmap();

        currentDocumentPixmap = QPixmap();
        currentPixmapOverlay->setPixmap(currentDocumentPixmap);
        currentPixmapOverlay->setScale(1);
        currentPixmapOverlay->hide();
        
        thumbnailPreviewOverlay->setPixmap(QPixmap());
        thumbnailPreviewOverlay->hide();
        
        afPointOverlay->hide();
        
        messageWidget->hide();
        exifOverlay->hide();
        
        scene->invalidate();
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
    
    void alignImageAccordingToViewMode(const QSharedPointer<Image>& img)
    {
        auto viewMode = ANPV::globalInstance()->viewMode();
        if(viewMode == ViewMode::Fit)
        {
            p->resetTransform();
            p->setTransform(img->defaultTransform(), true);
            p->fitInView(p->sceneRect(), Qt::KeepAspectRatio);
        }
    }
    
    void removeSmoothPixmap()
    {
        if (smoothPixmapOverlay)
        {
            smoothPixmapOverlay->setPixmap(QPixmap());
            smoothPixmapOverlay->hide();
            currentPixmapOverlay->show();
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
            else // the user sees a part of the image
            {
                // the pixmap overlay may have been scaled; we must translate the visible Pixmap Rectangle
                // (which is in scene coordinates) into the overlay's coordinates
                QRectF visPixRectMappedToItem = currentPixmapOverlay->mapFromScene(visPixRect).boundingRect();

                // now, crop the image to the visible part, so we don't need to scale the entire one
                imgToScale = currentDocumentPixmap.copy(visPixRectMappedToItem.toAlignedRect());
            }
            // Optimization for huge gigapixel images: before applying the smooth transformation, first scale it down to double
            // window resolution size with fast nearest neighbour transform.
            QPixmap fastDownScaled = imgToScale.scaled(viewportRect.size() * 2, Qt::KeepAspectRatio, Qt::FastTransformation);
            QPixmap scaled = fastDownScaled.scaled(viewportRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

            smoothPixmapOverlay->setPos(visPixRect.topLeft());
            smoothPixmapOverlay->setScale(newScale);
            smoothPixmapOverlay->setPixmap(scaled);
            smoothPixmapOverlay->show();
            currentPixmapOverlay->hide();
        }
        else
        {
            qDebug() << "Skipping smooth pixmap scaling: Too far zoomed in";
        }
        
        QGuiApplication::restoreOverrideCursor();
    }
    
    void addThumbnailPreview(QSharedPointer<Image> img)
    {
        QImage thumb = img->thumbnail();
        if(!thumb.isNull())
        {
            QSize fullImageSize = img->size();
            auto newScale = std::max(fullImageSize.width() * 1.0 / thumb.width(), fullImageSize.height() * 1.0 / thumb.height());

            thumbnailPreviewOverlay->setPixmap(QPixmap::fromImage(thumb, Qt::NoFormatConversion));
            thumbnailPreviewOverlay->setScale(newScale);
            thumbnailPreviewOverlay->show();
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

DocumentView::DocumentView(QWidget *parent)
 : QGraphicsView(parent), d(std::make_unique<Impl>(this))
{
    this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    this->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    this->setWindowState(Qt::WindowMaximized);
    this->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    
    d->scene = new QGraphicsScene(this);
    
    d->thumbnailPreviewOverlay = new QGraphicsPixmapItem;
    d->thumbnailPreviewOverlay->setZValue(-10);
    d->scene->addItem(d->thumbnailPreviewOverlay);
    
    d->currentPixmapOverlay = new QGraphicsPixmapItem;
    d->currentPixmapOverlay->setZValue(-9);
    d->scene->addItem(d->currentPixmapOverlay);
    
    d->smoothPixmapOverlay = new QGraphicsPixmapItem;
    d->smoothPixmapOverlay->setZValue(-8);
    d->scene->addItem(d->smoothPixmapOverlay);
    
    d->afPointOverlay = new AfPointOverlay;
    d->afPointOverlay->setZValue(100);
    d->scene->addItem(d->afPointOverlay);
    
    this->setScene(d->scene);
            
    d->messageWidget = new MessageWidget(this);
    d->messageWidget->setCloseButtonVisible(false);
    d->messageWidget->setWordWrap(true);
    d->messageWidget->hide();
    
    d->fovChangedTimer.setInterval(1000);
    d->fovChangedTimer.setSingleShot(true);
    connect(&d->fovChangedTimer, &QTimer::timeout, this, [&](){ emit d->createSmoothPixmap();});
    
    connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this,
            [&](ViewFlags_t v, ViewFlags_t)
            {
                bool vis = (v & static_cast<ViewFlags_t>(ViewFlag::ShowAfPoints)) != 0;
                if(d->afPointOverlay)
                {
                    d->afPointOverlay->setVisible(vis);
                }
            });
}

DocumentView::~DocumentView() = default;

void DocumentView::setModel(QSharedPointer<SortedImageModel> model)
{
    d->model = model;
}

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
    auto modifiers = event->modifiers();

    if (modifiers & Qt::ControlModifier && modifiers & Qt::ShiftModifier)
    {
        event->accept();
        double sign = angleDelta.y() < 0 ? -1 : 1;
        this->rotate(sign * (90.0 / 8));
    }
    else if (modifiers & Qt::ControlModifier)
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

void DocumentView::showEvent(QShowEvent* event)
{
    if(d->currentImageDecoder)
    {
        d->alignImageAccordingToViewMode(d->currentImageDecoder->image());
    }
    QGraphicsView::showEvent(event);
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
            // intentionally ignore the event, so that it can be processed by the parent view (MultiDocumentView)
            event->ignore();
            ANPV::globalInstance()->showThumbnailView(d->currentImageDecoder->image());
            this->close();
            break;
        case Qt::Key_Space:
            event->accept();
            if(d->currentImageDecoder && d->model)
            {
                QSharedPointer<Image> newImg = d->model->goTo(d->currentImageDecoder->image(), 1);
                if(newImg)
                {
                    this->loadImage(newImg);
                }
            }
            break;
        case Qt::Key_Backspace:
            event->accept();
            if(d->currentImageDecoder)
            {
                QSharedPointer<Image> newImg = d->model->goTo(d->currentImageDecoder->image(), -1);
                if(newImg)
                {
                    this->loadImage(newImg);
                }
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
    {
        this->showImage(dec->image());
        break;
    }
    case DecodingState::PreviewImage:
        if (oldState == DecodingState::Metadata)
        {
            d->currentPixmapOverlay->show();
        }
        break;
    case DecodingState::FullImage:
    {
        this->onImageRefinement(this->d->currentImageDecoder.data(), dec->decodedImage());

        QSize fullImageSize = dec->image()->size();
        auto newScale = std::max(fullImageSize.width() * 1.0 / d->currentDocumentPixmap.width(), fullImageSize.height() * 1.0 / d->currentDocumentPixmap.height());
        d->currentPixmapOverlay->setScale(newScale);
        
        d->createSmoothPixmap();
        d->thumbnailPreviewOverlay->hide();
        break;
    }
    case DecodingState::Error:
        d->currentDocumentPixmap = QPixmap();
        d->setDocumentError(dec);
        [[fallthrough]];
    case DecodingState::Cancelled:
        break;
    default:
        break;
    }
    
    if(d->latestDecodingState < newState)
    {
        d->latestDecodingState = static_cast<DecodingState>(newState);
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
    
    this->loadImage(DecoderFactory::globalInstance()->makeImage(info));
}

void DocumentView::loadImage(QSharedPointer<Image> image)
{
    QSharedPointer<SmartImageDecoder> dec = DecoderFactory::globalInstance()->getDecoder(image);
    if(!dec)
    {
        QString name = image->fileInfo().fileName();
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

void DocumentView::showImage(QSharedPointer<Image> img)
{
    xThreadGuard g(this);
    
    QSize fullImgSize = img->size();
    if(fullImgSize.isValid())
    {
        this->setSceneRect(QRectF(QPointF(0,0), fullImgSize));
        
        QSharedPointer<ExifWrapper> exif = img->exif();
        if(exif && d->latestDecodingState < DecodingState::Metadata)
        {
            d->latestDecodingState = DecodingState::Metadata;

            d->alignImageAccordingToViewMode(img);
            
            auto viewFlags = ANPV::globalInstance()->viewFlags();
            std::vector<AfPoint> afPoints;
            QSize size;
            QRect inFocusBoundingRect;
            QRect selectedFocusBoundingRect;
            if(exif->autoFocusPoints(afPoints,size))
            {
                d->afPointOverlay->setVisible((ANPV::globalInstance()->viewFlags() & static_cast<ViewFlags_t>(ViewFlag::ShowAfPoints)) != 0);
                d->afPointOverlay->setAfPoints(afPoints, size);
                for(size_t i=0; i < afPoints.size(); i++)
                {
                    auto& af = afPoints[i];
                    auto type = std::get<0>(af);
                    auto rect = std::get<1>(af);
                    if(type == AfType::HasFocus)
                    {
                        inFocusBoundingRect = inFocusBoundingRect.united(rect);
                    }
                    else if(type == AfType::Selected)
                    {
                        selectedFocusBoundingRect = selectedFocusBoundingRect.united(rect);
                    }
                }
                
                if(viewFlags & static_cast<ViewFlags_t>(ViewFlag::CenterAf))
                {
                    if(inFocusBoundingRect.isValid())
                    {
                        this->centerOn(inFocusBoundingRect.center());
                    }
                    else if(selectedFocusBoundingRect.isValid())
                    {
                        this->centerOn(selectedFocusBoundingRect.center());
                    }
                }
            }
        }
    }
    
    d->addThumbnailPreview(img);
    d->exifOverlay->setMetadata(img);
}

void DocumentView::loadImage()
{
    this->showImage(d->currentImageDecoder->image());
    
    QObject::connect(d->currentImageDecoder.data(), &SmartImageDecoder::imageRefined, this, &DocumentView::onImageRefinement);
    QObject::connect(d->currentImageDecoder.data(), &SmartImageDecoder::decodingStateChanged, this, &DocumentView::onDecodingStateChanged);

    auto fut = d->currentImageDecoder->decodeAsync(DecodingState::FullImage, Priority::Important, this->geometry().size());
    d->taskFuture.setFuture(fut);
    ANPV::globalInstance()->addBackgroundTask(ProgressGroup::Image, fut);

    emit this->imageChanged(d->currentImageDecoder->image());
}

QFileInfo DocumentView::currentFile()
{
    if(d->currentImageDecoder)
    {
        return d->currentImageDecoder->image()->fileInfo();
    }
    return QFileInfo();
}
