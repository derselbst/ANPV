#include "DocumentView.hpp"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QFlags>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QWindow>
#include <QGuiApplication>
#include <QDebug>
#include <QFuture>
#include <QAction>
#include <QFutureWatcher>
#include <QClipboard>
#include <QString>
#include <QDataStream>
#include <QScrollBar>
#include <QActionGroup>
#include <QtConcurrent/QtConcurrent>
#include <QColorDialog>
#include <QMessageBox>
#include <QCheckBox>

#include <vector>
#include <algorithm>

#include "AfPointOverlay.hpp"
#include "ExifOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "FileOperationConfigDialog.hpp"
#include "ANPV.hpp"
#include "Image.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "xThreadGuard.hpp"
#include "WaitCursor.hpp"

struct DocumentView::Impl
{
    DocumentView* p = nullptr;
    
    QTimer fovChangedTimer;
    QTransform previousFovTransform;
    
    QPointer<QGraphicsScene> scene;
    QPointer<MessageWidget> messageWidget;
    QPointer<QCheckBox> isSelectedBox;
    
    // a smoothly scaled version of the full resolution image
    QGraphicsPixmapItem* smoothPixmapOverlay = nullptr;
    
    QGraphicsPixmapItem* thumbnailPreviewOverlay = nullptr;
    
    QGraphicsPixmapItem* currentPixmapOverlay = nullptr;

    QAction* actionShowScrollBars = nullptr;

    AfPointOverlay* afPointOverlay = nullptr;
    
    std::unique_ptr<ExifOverlay> exifOverlay = std::make_unique<ExifOverlay>(p);
    
    QFutureWatcher<DecodingState> taskFuture;
    
    // the latest image decoder, the same that displays the current image
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
        if(currentImageDecoder)
        {
            currentImageDecoder->cancelOrTake(taskFuture.future());
            taskFuture.waitForFinished();
            currentImageDecoder->releaseFullImage();
            currentImageDecoder->image()->disconnect(p);
            currentImageDecoder.reset();
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
        if(newTransform != previousFovTransform && taskFuture.isFinished())
        {
            fovChangedTimer.start();
            previousFovTransform = newTransform;
            removeSmoothPixmap();
        }
    }
    
    void alignImageAccordingToViewMode(const QSharedPointer<Image>& img)
    {
        auto exif = img->exif();
        
        auto viewMode = ANPV::globalInstance()->viewMode();
        if(viewMode == ViewMode::Fit)
        {
            p->resetTransform();
            if(exif)
            {
                p->setTransform(exif->transformMatrix(), true);
            }
            p->fitInView(p->sceneRect(), Qt::KeepAspectRatio);
        }
        else if(viewMode == ViewMode::None)
        {
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
        WaitCursor w;

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

    void setDocumentError(const QSharedPointer<Image>& img)
    {
        setDocumentError(img->errorMessage());
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
    
    static inline const QString MimeTransform{"anpv/transform"};
    void onCopyViewTransform()
    {
        QByteArray b;
        {
            QDataStream out(&b, QIODeviceBase::WriteOnly);
            out.setVersion(QDataStream::Qt_6_2);
            out << p->transform();
            out << p->horizontalScrollBar()->value();
            out << p->verticalScrollBar()->value();
        }
        
        QMimeData* mime = new QMimeData;
        mime->setData(MimeTransform, b);
        
        QClipboard* clip = QGuiApplication::clipboard();
        clip->setMimeData(mime);
    }
    
    void onClipboardPaste()
    {
        QClipboard* clip = QGuiApplication::clipboard();
        const QMimeData* mime = clip->mimeData();
        
        QByteArray data = mime->data(MimeTransform);
        if(!data.isEmpty())
        {
            QDataStream in(data);
            in.setVersion(QDataStream::Qt_6_2);
            
            QTransform t;
            int v;
            in >> t;
            p->setTransform(t);
            
            in >> v;
            p->horizontalScrollBar()->setValue(v);
            
            in >> v;
            p->verticalScrollBar()->setValue(v);
        }
    }
    
    void onViewFlagsChanged(ViewFlags_t v)
    {
        bool vis = (v & static_cast<ViewFlags_t>(ViewFlag::ShowAfPoints)) != 0;
        if(this->afPointOverlay)
        {
            this->afPointOverlay->setVisible(vis);
        }
        
        bool showScrollBar = (v & static_cast<ViewFlags_t>(ViewFlag::ShowScrollBars)) != 0;
        auto policy = showScrollBar ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff;
        // Qt::ScrollBarAsNeeded causes many resizeEvents to be delivered
        p->setHorizontalScrollBarPolicy(policy);
        p->setVerticalScrollBarPolicy(policy);
        this->actionShowScrollBars->setChecked(showScrollBar);
    }
    
    void onViewModeChanged(ViewMode)
    {
        if(this->currentImageDecoder)
        {
            this->alignImageAccordingToViewMode(this->currentImageDecoder->image());
        }
    }
    
    void goTo(int i)
    {
        WaitCursor w;
        if(this->currentImageDecoder && this->model)
        {
            Entry_t newEntry = this->model->goTo(this->currentImageDecoder->image(), i);
            p->loadImage(newEntry);
        }
    }
    
    void onSetBackgroundColor()
    {
        QBrush currentBrush = this->scene->backgroundBrush();
        QColor currentColor = currentBrush.color();
        if(currentBrush.style() == Qt::NoBrush)
        {
            currentColor = Qt::white;
        }
        QColorDialog colDiag(currentColor, p);
        colDiag.setOptions(QColorDialog::ShowAlphaChannel);
        QObject::connect(&colDiag, &QColorDialog::currentColorChanged, p, [&](const QColor& col){ this->scene->setBackgroundBrush(QBrush(col)); });
        int ret = colDiag.exec();
        if(ret == QDialog::Rejected)
        {
            this->scene->setBackgroundBrush(currentBrush);
        }
    }
    
    void createActions()
    {
        QAction* act;
        
        act = new QAction(QIcon::fromTheme("go-next"), "Go Next", p);
        act->setShortcuts({{Qt::Key_Space}, {Qt::Key_Right}});
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, p, [&](){ this->goTo(+1); });
        p->addAction(act);
        
        act = new QAction(QIcon::fromTheme("go-previous"), "Go Previous", p);
        act->setShortcuts({{Qt::Key_Backspace}, {Qt::Key_Left}});
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, p, [&](){ this->goTo(-1); });
        p->addAction(act);

        act = new QAction(p);
        act->setSeparator(true);
        p->addAction(act);
        
        this->actionShowScrollBars = new QAction("Show Scroll Bars", p);
        this->actionShowScrollBars->setCheckable(true);
        connect(this->actionShowScrollBars, &QAction::toggled, p, [&](bool checked){ ANPV::globalInstance()->setViewFlag(ViewFlag::ShowScrollBars, checked); });
        p->addAction(this->actionShowScrollBars);
        
        act = new QAction("Set Background Color", p);
        connect(act, &QAction::triggered, p, [&](){ this->onSetBackgroundColor(); });
        p->addAction(act);

        act = new QAction(p);
        act->setSeparator(true);
        p->addAction(act);
        
        act = new QAction(QIcon::fromTheme("edit-copy"), "Copy View Transform", p);
        connect(act, &QAction::triggered, p, [&](){ this->onCopyViewTransform(); });
        p->addAction(act);
        
        act = new QAction(QIcon::fromTheme("edit-paste"), "Paste", p);
        connect(act, &QAction::triggered, p, [&](){ this->onClipboardPaste(); });
        p->addAction(act);
        
        act = new QAction(p);
        act->setSeparator(true);
        p->addAction(act);
        
        QActionGroup* fileActions = ANPV::globalInstance()->copyMoveActionGroup();
        p->addActions(fileActions->actions());
        connect(ANPV::globalInstance()->copyMoveActionGroup(), &QActionGroup::triggered, p, [&](QAction* act)
        {
            if(!this->currentImageDecoder)
            {
                return;
            }

            QList<QObject*> objs = act->associatedObjects();
            for(QObject* o : objs)
            {
                if(o == p && p->hasFocus())
                {
                    Entry_t nextImg = this->model->goTo(this->currentImageDecoder->image(), 1);

                    ANPV::FileOperation op = FileOperationConfigDialog::operationFromAction(act);
                    QString targetDir = act->data().toString();
                    QFileInfo source = this->currentImageDecoder->image()->fileInfo();
                    switch(op)
                    {
                        case ANPV::FileOperation::Move:
                            ANPV::globalInstance()->moveFiles({source.fileName()}, source.absoluteDir().absolutePath(), std::move(targetDir));
                            p->loadImage(nextImg);
                            break;
                        case ANPV::FileOperation::HardLink:
                            ANPV::globalInstance()->hardLinkFiles({source.fileName()}, source.absoluteDir().absolutePath(), std::move(targetDir));
                            break;
                        default:
                            QMessageBox::information(p, "Not yet implemented", "not yet impl");
                            break;
                    }
                    break;
                }
            }
        });
    }
};

DocumentView::DocumentView(QWidget *parent)
 : QGraphicsView(parent), d(std::make_unique<Impl>(this))
{
    this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    this->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    this->setWindowState(Qt::WindowMaximized);
    this->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    this->setContextMenuPolicy(Qt::ActionsContextMenu);
    this->setDragMode(QGraphicsView::ScrollHandDrag);
    
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
    
    d->isSelectedBox = new QCheckBox(this);
    d->isSelectedBox->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    connect(d->isSelectedBox.get(), &QCheckBox::stateChanged, this, [&](int state)
        {
            auto& dec = d->currentImageDecoder;
            if (dec)
            {
                dec->image()->setChecked(static_cast<Qt::CheckState>(state));
            }
        });

    d->createActions();

    d->fovChangedTimer.setInterval(1000);
    d->fovChangedTimer.setSingleShot(true);
    connect(&d->fovChangedTimer, &QTimer::timeout, this, [&](){ emit d->createSmoothPixmap();});
    
    d->onViewFlagsChanged(ANPV::globalInstance()->viewFlags());
    connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this,
            [&](ViewFlags_t v, ViewFlags_t){ d->onViewFlagsChanged(v); });
    
    d->onViewModeChanged(ANPV::globalInstance()->viewMode());
    connect(ANPV::globalInstance(), &ANPV::viewModeChanged, this,
            [&](ViewMode neu, ViewMode){ d->onViewModeChanged(neu); });
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
    QGraphicsView::showEvent(event);
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    auto wndSize = event->size();
    d->centerMessageWidget(wndSize);

    QPoint bottomLeftCheckPoint(0, wndSize.height() - d->isSelectedBox->size().height());
    d->isSelectedBox->move(bottomLeftCheckPoint);
    if(d->currentImageDecoder)
    {
        d->alignImageAccordingToViewMode(d->currentImageDecoder->image());
    }

    QGraphicsView::resizeEvent(event);
}

void DocumentView::keyPressEvent(QKeyEvent *event)
{
    WaitCursor w;
    switch(event->key())
    {
        case Qt::Key_Escape:
            // intentionally ignore the event, so that it can be processed by the parent view (MultiDocumentView)
            event->ignore();
            if(ANPV::globalInstance()->currentDir().isEmpty() && d->currentImageDecoder)
            {
                ANPV::globalInstance()->setCurrentDir(d->currentImageDecoder->image()->fileInfo().dir().absolutePath());
            }
            ANPV::globalInstance()->showThumbnailView();
            this->close();
            break;
        default:
            QGraphicsView::keyPressEvent(event);
            break;
    }
}

void DocumentView::mouseMoveEvent(QMouseEvent *event)
{
    auto width = this->width();
    auto height = this->height();
    auto pos = event->pos();
    
    bool yExceeded = pos.y() < 0 || pos.y() >= height;
    bool xExceeded = pos.x() < 0 || pos.x() >= width;
    if (yExceeded || xExceeded)
    {
        event->accept();
        // Mouse cursor has left the widget. Wrap the mouse.
        auto globalPos = this->mapToGlobal(event->pos());
        if (yExceeded)
        {
            // Cursor left on the y axis. Move cursor to the opposite side.
            globalPos.setY(globalPos.y() + (pos.y() < 0 ? height-1 : -height+2));
        }
        else
        {
            // Cursor left on the x axis. Move cursor to the opposite side.
            globalPos.setX(globalPos.x() + (pos.x() < 0 ? width-1 : -width+2));
        }
        // For the scroll hand dragging to work with mouse wrapping
        // we have to emulate a mouse release, move the cursor and
        // then emulate a mouse press. Not doing this causes the
        // scroll hand drag to stop after the cursor has moved.
        QMouseEvent r_event(QEvent::MouseButtonRelease,
                            this->mapFromGlobal(QCursor::pos()),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        this->mouseReleaseEvent(&r_event);
        
        QCursor::setPos(globalPos);
        
        QMouseEvent p_event(QEvent::MouseButtonPress,
                                this->mapFromGlobal(QCursor::pos()),
                                Qt::LeftButton,
                                Qt::LeftButton,
                                Qt::NoModifier);
        this->mousePressEvent(&p_event);
    }
    else
    {
        QGraphicsView::mouseMoveEvent(event);
    }
}

void DocumentView::onPreviewImageUpdated(Image* img, QRect r)
{
    if (img != this->d->currentImageDecoder->image().data() || !d->currentPixmapOverlay)
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }
    d->currentPixmapOverlay->update(r);
}

void DocumentView::onImageRefinement(Image* img, QImage image)
{
    if(img != this->d->currentImageDecoder->image().data())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }
    
    d->removeSmoothPixmap();
    d->currentDocumentPixmap = QPixmap::fromImage(image, Qt::NoFormatConversion);
    d->currentPixmapOverlay->setPixmap(d->currentDocumentPixmap);
}

void DocumentView::onDecodingStateChanged(Image* img, quint32 newState, quint32 oldState)
{
    auto& dec = d->currentImageDecoder;
    if(dec && img != dec->image().data())
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
        this->onImageRefinement(dec->image().data(), dec->image()->decodedImage());

        QSize fullImageSize = dec->image()->size();
        auto newScale = std::max(fullImageSize.width() * 1.0 / d->currentDocumentPixmap.width(), fullImageSize.height() * 1.0 / d->currentDocumentPixmap.height());
        d->currentPixmapOverlay->setScale(newScale);
        
        d->createSmoothPixmap();
        d->thumbnailPreviewOverlay->hide();
        break;
    }
    case DecodingState::Fatal:
    case DecodingState::Error:
        d->currentDocumentPixmap = QPixmap();
        d->setDocumentError(dec->image());
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

void DocumentView::onCheckStateChanged(Image* img, int state, int old)
{
    auto& dec = d->currentImageDecoder;
    if (dec && img != dec->image().data())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }
    d->isSelectedBox->setCheckState(static_cast<Qt::CheckState>(state));
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

void DocumentView::loadImage(const Entry_t& e)
{
    auto& dec = SortedImageModel::decoder(e);
    auto& img = SortedImageModel::image(e);
    if(dec)
    {
        this->loadImage(dec);
    }
    else if(img)
    {
        this->loadImage(img);
    }
}

void DocumentView::loadImage(QSharedPointer<Image> image)
{
    auto dec = QSharedPointer<SmartImageDecoder>(DecoderFactory::globalInstance()->getDecoder(image).release());
    if(!dec)
    {
        QString name = image->fileInfo().fileName();
        d->setDocumentError(QString("Could not find a decoder for file %1").arg(name));
        return;
    }
    
    this->loadImage(std::move(dec));
}

void DocumentView::loadImage(const QSharedPointer<SmartImageDecoder>& dec)
{
    d->clearScene();
    d->currentImageDecoder = std::move(dec);
    this->loadImage();
}

void DocumentView::showImage(QSharedPointer<Image> img)
{
    xThreadGuard g(this);
    
    QSize fullImgSize = img->size();
    if(fullImgSize.isValid())
    {
        this->setSceneRect(QRectF(QPointF(0,0), fullImgSize));
        
        if(d->latestDecodingState < DecodingState::Metadata)
        {
            d->latestDecodingState = DecodingState::Metadata;

            d->alignImageAccordingToViewMode(img);
            
            auto viewFlags = ANPV::globalInstance()->viewFlags();
            auto afp = img->cachedAutoFocusPoints();
            if(afp)
            {
                std::vector<AfPoint>& afPoints = std::get<0>(*afp);
                QSize& size = std::get<1>(*afp);
                d->afPointOverlay->setVisible((ANPV::globalInstance()->viewFlags() & static_cast<ViewFlags_t>(ViewFlag::ShowAfPoints)) != 0);
                d->afPointOverlay->setAfPoints(afPoints, size);
                
                if(viewFlags & static_cast<ViewFlags_t>(ViewFlag::CenterAf))
                {
                    QRect inFocusBoundingRect;
                    QRect selectedFocusBoundingRect;
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
    d->fovChangedTimer.stop();

    QObject::connect(d->currentImageDecoder->image().data(), &Image::decodedImageChanged, this, &DocumentView::onImageRefinement);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::previewImageUpdated, this, &DocumentView::onPreviewImageUpdated);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::decodingStateChanged, this, &DocumentView::onDecodingStateChanged);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::checkStateChanged, this, &DocumentView::onCheckStateChanged);

    auto fut = d->currentImageDecoder->decodeAsync(DecodingState::FullImage, Priority::Important, this->screen()->geometry().size());
    d->taskFuture.setFuture(fut);

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
