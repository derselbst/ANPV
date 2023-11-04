#include "DocumentView.hpp"

#include <QGraphicsScene>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QGraphicsPixmapItem>
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
#include <QMessageBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QMimeData>

#include <vector>
#include <algorithm>
#include <optional>

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
#include "ImageSectionDataContainer.hpp"

struct DocumentView::Impl
{
    DocumentView* q = nullptr;
    
    QTimer fovChangedTimer;
    std::optional<QTransform> previousFovTransform;
    
    QPointer<QGraphicsScene> scene;
    QPointer<MessageWidget> messageWidget;
    QPointer<QCheckBox> isSelectedBox;
    
    // a smoothly scaled version of the full resolution image
    QGraphicsPixmapItem* smoothPixmapOverlay = nullptr;
    
    QGraphicsPixmapItem* thumbnailPreviewOverlay = nullptr;
    
    QGraphicsPixmapItem* currentPixmapOverlay = nullptr;

    QAction* actionShowScrollBars = nullptr;
    QAction* actionShowInfoBox = nullptr;

    AfPointOverlay* afPointOverlay = nullptr;

    QGraphicsRectItem* debugOverlay1 = nullptr;

    std::unique_ptr<ExifOverlay> exifOverlay = std::make_unique<ExifOverlay>(q);
    
    QFutureWatcher<DecodingState> taskFuture;
    
    // the latest image decoder, the same that displays the current image
    QSharedPointer<SmartImageDecoder> currentImageDecoder;
    
    // an owning reference to the image currently displayed - in case it vaishes in the model we'll still have it rather going null
    QSharedPointer<Image> owningRefToImage;
    
    DecodingState latestDecodingState = DecodingState::Ready;
    
    // the full resolution image currently displayed in the scene
    QPixmap currentDocumentPixmap;
    
    // the model for the current directory needed for navigating back and forth
    QSharedPointer<ImageSectionDataContainer> model;

    ViewFlags_t cachedViewFlags = ViewFlags_t(ViewFlag::None);

    Impl(DocumentView* parent) : q(parent)
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
            currentImageDecoder->image()->disconnect(q);
            currentImageDecoder.reset();
            latestDecodingState = DecodingState::Ready;
            // this makes ensures that the if clause will be entered next time we enter onViewportChanged(),
            // to display the next or previous image
            previousFovTransform = std::nullopt;
        }

        removeSmoothPixmap();

        currentDocumentPixmap = QPixmap();
        currentPixmapOverlay->setPixmap(currentDocumentPixmap);
        currentPixmapOverlay->setScale(1);
        currentPixmapOverlay->hide();
        
        thumbnailPreviewOverlay->setPixmap(QPixmap());
        thumbnailPreviewOverlay->hide();
        
        debugOverlay1->hide();
        
        afPointOverlay->hide();
        
        messageWidget->hide();
        exifOverlay->hide();
        
        scene->invalidate();
        
        owningRefToImage = nullptr;
    }

    void forceTriggerDecoding()
    {
        if (this->latestDecodingState <= DecodingState::Metadata)
        {
            // no preview image available, quickly start the decoding
            if (!this->fovChangedTimer.isActive())
            {
                // delay the decoding by a few milliseconds, as sometimes there may be two resizeEvents being sent, I don't know why
                this->fovChangedTimer.start(50);
            }
        }
        else
        {
            // We already have a preview image, the user zoomed or scrolled around, no need to hurry.
            // Do not wrap this in a if(!timer.isActive()), because if the user keeps scrolling around, this should
            // cause the timer restart from being until the user has stopped all viewport changes.
            this->fovChangedTimer.start(600);
        }
    }
    
    void onViewportChanged()
    {
        QTransform newTransform = q->viewportTransform();
        if(newTransform != this->previousFovTransform)
        {
            forceTriggerDecoding();
            this->previousFovTransform = newTransform;
            removeSmoothPixmap();
        }
    }
    
    void alignImageAccordingToViewMode(const QSharedPointer<Image>& img, ViewMode viewMode)
    {
        auto exif = img->exif();
        
        if(viewMode == ViewMode::Fit)
        {
            q->resetTransform();
            if(exif)
            {
                q->setTransform(exif->transformMatrix(), true);
            }
            q->fitInView(q->sceneRect(), Qt::KeepAspectRatio);
            this->onViewportChanged();
        }
        else if(viewMode == ViewMode::None)
        {
            this->forceTriggerDecoding();
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
        xThreadGuard g(q);
        if (currentDocumentPixmap.isNull())
        {
            return;
        }
        WaitCursor w;

        // get the area of what the user sees
        QRect viewportRect = q->viewport()->rect();

        // and map that rect to scene coordinates
        QRectF viewportRectScene = q->mapToScene(viewportRect).boundingRect();

        // the user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void
        QRectF visPixRect = viewportRectScene.intersected(currentPixmapOverlay->sceneBoundingRect());

        // the "inverted zoom factor"
        // 1.0 means the pixmap is shown at native size
        // >1.0 means the user zoomed out
        // <1.0 means the user zommed in and sees the individual pixels
        auto newScale = std::max(visPixRect.width() / viewportRect.width(), visPixRect.height() / viewportRect.height());

        if (newScale >= 1.0)
        {
            currentPixmapOverlay->setTransformationMode(Qt::SmoothTransformation);
        }
        else
        {
            currentPixmapOverlay->setTransformationMode(Qt::FastTransformation);
        }

        return;

        if (newScale > 2.0)
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
    
    void startImageDecoding()
    {
        if(!this->currentImageDecoder)
        {
            // error while loadImage()
            return;
        }
        
        if(this->latestDecodingState == DecodingState::FullImage)
        {
            // full resolution image already decoded, only create a smooth pixmap
            this->createSmoothPixmap();
            return;
        }

        currentImageDecoder->cancelOrTake(taskFuture.future());
        taskFuture.waitForFinished();

        // get the area of what the user sees
        QRect viewportRect = q->viewport()->rect();

        // and map that rect to scene coordinates
        QRectF viewportRectScene = q->mapToScene(viewportRect).boundingRect();

        QFuture<DecodingState> fut;
        QRect fullResRect = this->currentImageDecoder->image()->fullResolutionRect();
        if (!fullResRect.isEmpty())
        {
            // The user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void.
            QRectF visPixRect = viewportRectScene.intersected(fullResRect);

            // the GraphicsView may have been scaled; we must translate the visible rectangle
            // (which is in scene coordinates) into the view's coordinates
            QRectF visPixRectMappedToView = q->mapFromScene(visPixRect).boundingRect();

            QSize desiredRes = visPixRectMappedToView.toAlignedRect().size();

            fut = this->currentImageDecoder->decodeAsync(DecodingState::PreviewImage, Priority::Important, desiredRes, visPixRect.toAlignedRect());
            qDebug() << "startImageDecoding(): desiredRes: " << desiredRes << " | visPixRect: " << visPixRect.toAlignedRect();
        }
        else
        {
            fut = this->currentImageDecoder->decodeAsync(DecodingState::PreviewImage, Priority::Important, viewportRect.size(), QRect());
        }
        this->taskFuture.setFuture(fut);
    }
    
    void onFOVChanged()
    {
        this->startImageDecoding();
    }
    
    void addThumbnailPreview(QSharedPointer<Image> img)
    {
        QImage thumb = img->thumbnail();
        if(!thumb.isNull())
        {
            auto thumbnailToFullResTrafo = currentImageDecoder->fullResToPageTransform(thumb.size()).inverted();

            thumbnailPreviewOverlay->setPixmap(QPixmap::fromImage(thumb, Qt::NoFormatConversion));
            thumbnailPreviewOverlay->setTransform(thumbnailToFullResTrafo);
            thumbnailPreviewOverlay->show();
        }
    }

    void setDocumentError(const QSharedPointer<Image>& img)
    {
        setDocumentError(img->errorMessage());
    }
    
    void setDocumentError(QString error)
    {
        if (error.isEmpty())
        {
            messageWidget->hide();
            return;
        }
        messageWidget->setText(error);
        messageWidget->setMessageType(MessageWidget::MessageType::Error);
        messageWidget->setIcon(QIcon::fromTheme("dialog-error"));
        messageWidget->show();
        this->centerMessageWidget(q->size());
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
            out << q->transform();
            out << q->horizontalScrollBar()->value();
            out << q->verticalScrollBar()->value();
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
            q->setTransform(t);
            
            in >> v;
            q->horizontalScrollBar()->setValue(v);
            
            in >> v;
            q->verticalScrollBar()->setValue(v);
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
        q->setHorizontalScrollBarPolicy(policy);
        q->setVerticalScrollBarPolicy(policy);
        this->actionShowScrollBars->setChecked(showScrollBar);
        this->cachedViewFlags = v;
    }
    
    void onViewModeChanged(ViewMode v)
    {
        if(this->currentImageDecoder)
        {
            this->alignImageAccordingToViewMode(this->currentImageDecoder->image(), v);
        }
    }
    
    void goTo(int i)
    {
        WaitCursor w;
        if(this->currentImageDecoder && this->model)
        {
            QSharedPointer<Image> newEntry = this->model->goTo(this->cachedViewFlags, this->currentImageDecoder->image().get(), i);
            if (newEntry)
            {
                q->loadImage(newEntry);
            }
        }
    }

    void onToggleSelect()
    {
        if (this->currentImageDecoder)
        {
            auto img = this->currentImageDecoder->image();
            if (img)
            {
                Qt::CheckState chk = img->checked();
                if (chk == Qt::CheckState::PartiallyChecked)
                {
                    return;
                }
                img->setChecked(chk == Qt::CheckState::Checked ? Qt::CheckState::Unchecked : Qt::CheckState::Checked);
            }
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
        QColorDialog colDiag(currentColor, q);
        colDiag.setOptions(QColorDialog::ShowAlphaChannel);
        QObject::connect(&colDiag, &QColorDialog::currentColorChanged, q, [&](const QColor& col){ this->scene->setBackgroundBrush(QBrush(col)); });
        int ret = colDiag.exec();
        if(ret == QDialog::Rejected)
        {
            this->scene->setBackgroundBrush(currentBrush);
        }
    }
    
    void createActions()
    {
        QAction* act;
        
        act = new QAction(QIcon::fromTheme("go-next"), "Next File", q);
        act->setShortcut({Qt::Key_Right});
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&](){ this->goTo(+1); });
        q->addAction(act);

        act = new QAction(QIcon::fromTheme("go-previous"), "Previous File", q);
        act->setShortcut({Qt::Key_Left});
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { this->goTo(-1); });
        q->addAction(act);

        act = new QAction("Toggle check state of current image", q);
        act->setShortcut({Qt::Key_Space});
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { this->onToggleSelect(); });
        q->addAction(act);

        act = new QAction(q);
        act->setSeparator(true);
        q->addAction(act);

        act = new QAction("Mirror", q);
        act->setShortcut({ Qt::Key_F });
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { q->scale(-1, 1); });
        q->addAction(act);

        act = new QAction("Flip", q);
        act->setShortcut({ Qt::CTRL | Qt::Key_F });
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { q->scale(1, -1); });
        q->addAction(act);

        act = new QAction("Rotate Clockwise", q);
        act->setShortcut({ Qt::Key_R });
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { q->rotate(90); });
        q->addAction(act);

        act = new QAction("Rotate Counter-Clockwise", q);
        act->setShortcut({ Qt::Key_L });
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&]() { q->rotate(-90); });
        q->addAction(act);

        act = new QAction(q);
        act->setSeparator(true);
        q->addAction(act);

        QList<QAction*> parentActions = q->parentWidget()->actions();
        for (QAction* a : parentActions)
        {
            q->addAction(a);
        }

        act = new QAction(q);
        act->setSeparator(true);
        q->addAction(act);

        this->actionShowScrollBars = new QAction("Show Scroll Bars", q);
        this->actionShowScrollBars->setCheckable(true);
        connect(this->actionShowScrollBars, &QAction::toggled, q, [&](bool checked) { ANPV::globalInstance()->setViewFlag(ViewFlag::ShowScrollBars, checked); });
        q->addAction(this->actionShowScrollBars);

        this->actionShowInfoBox = new QAction("Show Info Box", q);
        this->actionShowInfoBox->setCheckable(true);
        this->actionShowInfoBox->setChecked(true);
        this->actionShowInfoBox->setShortcut({ Qt::Key_I });
        this->actionShowInfoBox->setShortcutContext(Qt::WidgetShortcut);
        connect(this->actionShowInfoBox, &QAction::toggled, q, [&](bool checked) { this->exifOverlay->setVisible(checked); });
        q->addAction(this->actionShowInfoBox);
        
        act = new QAction("Set Background Color", q);
        connect(act, &QAction::triggered, q, [&](){ this->onSetBackgroundColor(); });
        q->addAction(act);

        act = new QAction(q);
        act->setSeparator(true);
        q->addAction(act);
        
        act = new QAction(QIcon::fromTheme("edit-copy"), "Copy View Transform", q);
        act->setShortcut({ Qt::CTRL | Qt::SHIFT | Qt::Key_C });
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&](){ this->onCopyViewTransform(); });
        q->addAction(act);
        
        act = new QAction(QIcon::fromTheme("edit-paste"), "Paste", q);
        act->setShortcut(QKeySequence::Paste);
        act->setShortcutContext(Qt::WidgetShortcut);
        connect(act, &QAction::triggered, q, [&](){ this->onClipboardPaste(); });
        q->addAction(act);

        act = new QAction(q);
        act->setSeparator(true);
        q->addAction(act);
        
        QActionGroup* fileActions = ANPV::globalInstance()->copyMoveActionGroup();
        q->addActions(fileActions->actions());
        connect(fileActions, &QActionGroup::triggered, q, [&](QAction* act)
        {
            if(!this->currentImageDecoder)
            {
                return;
            }

            WaitCursor w;
            QList<QObject*> objs = act->associatedObjects();
            for(QObject* o : objs)
            {
                if(o == q && q->hasFocus())
                {
                    QSharedPointer<Image> nextImg = this->model->goTo(this->cachedViewFlags, this->currentImageDecoder->image().get(), 1);
                    
                    ANPV::FileOperation op = FileOperationConfigDialog::operationFromAction(act);
                    QString targetDir = act->data().toString();
                    QFileInfo source = this->currentImageDecoder->image()->fileInfo();
                    switch(op)
                    {
                        case ANPV::FileOperation::Move:
                            // cancel any pending decoding to release the file handle and avoid a "File being used by other process" error on Windows
                            this->currentImageDecoder->cancelOrTake(this->taskFuture.future());
                            this->taskFuture.waitForFinished();
                            ANPV::globalInstance()->moveFiles({source.fileName()}, source.absoluteDir().absolutePath(), std::move(targetDir));
                            q->loadImage(nextImg);
                            break;
                        case ANPV::FileOperation::HardLink:
                            ANPV::globalInstance()->hardLinkFiles({ source.fileName() }, source.absoluteDir().absolutePath(), std::move(targetDir));
                            break;
                        case ANPV::FileOperation::Delete:
                            // cancel any pending decoding to release the file handle and avoid a "File being used by other process" error on Windows
                            this->currentImageDecoder->cancelOrTake(this->taskFuture.future());
                            this->taskFuture.waitForFinished();
                            ANPV::globalInstance()->deleteFiles({ source.fileName() }, source.absoluteDir().absolutePath());
                            q->loadImage(nextImg);
                            break;
                        default:
                            QMessageBox::information(q, "Not yet implemented", "not yet impl");
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
    d->currentPixmapOverlay->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    d->currentPixmapOverlay->setTransformationMode(Qt::SmoothTransformation);
    d->scene->addItem(d->currentPixmapOverlay);
    
    d->smoothPixmapOverlay = new QGraphicsPixmapItem;
    d->smoothPixmapOverlay->setZValue(-8);
    d->scene->addItem(d->smoothPixmapOverlay);
    
    d->afPointOverlay = new AfPointOverlay;
    d->afPointOverlay->setZValue(100);
    d->scene->addItem(d->afPointOverlay);

    d->debugOverlay1 = new QGraphicsRectItem;
    d->debugOverlay1->setZValue(100000);
    d->debugOverlay1->setPen(QPen(Qt::green, 6, Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin));
    d->scene->addItem(d->debugOverlay1);
    d->debugOverlay1->hide();
    
    this->setScene(d->scene);
            
    d->messageWidget = new MessageWidget(this);
    d->messageWidget->setCloseButtonVisible(false);
    d->messageWidget->setWordWrap(true);
    d->messageWidget->setFocusPolicy(Qt::FocusPolicy::NoFocus);
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

    d->exifOverlay->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    this->setFocusPolicy(Qt::FocusPolicy::WheelFocus);

    d->createActions();

    d->fovChangedTimer.setSingleShot(true);
    connect(&d->fovChangedTimer, &QTimer::timeout, this, [&](){ d->onFOVChanged();});
    
    d->onViewFlagsChanged(ANPV::globalInstance()->viewFlags());
    connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this,
            [&](ViewFlags_t v, ViewFlags_t){ d->onViewFlagsChanged(v); });
    
    d->onViewModeChanged(ANPV::globalInstance()->viewMode());
    connect(ANPV::globalInstance(), &ANPV::viewModeChanged, this,
            [&](ViewMode neu, ViewMode){ d->onViewModeChanged(neu); });
}

DocumentView::~DocumentView() = default;

void DocumentView::setModel(QSharedPointer<ImageSectionDataContainer> model)
{
    d->model = model;
}

void DocumentView::zoomIn()
{
    this->scale(1.2, 1.2);
    d->onViewportChanged();
}

void DocumentView::zoomOut()
{
    this->scale(1 / 1.2, 1 / 1.2);
    d->onViewportChanged();
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

void DocumentView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    d->onViewportChanged();
}

void DocumentView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);

    auto wndSize = event->size();
    d->centerMessageWidget(wndSize);

    QSize i = d->isSelectedBox->iconSize();
    QPoint bottomLeftCheckPoint(0, wndSize.height() - i.height());
    d->isSelectedBox->move(bottomLeftCheckPoint);
    if(d->currentImageDecoder)
    {
        d->alignImageAccordingToViewMode(d->currentImageDecoder->image(), ANPV::globalInstance()->viewMode());
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
    if (img != this->d->currentImageDecoder->image().data() || !d->currentPixmapOverlay || d->currentPixmapOverlay->pixmap().isNull())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }

    // assert that the update rect is inside the boundingBox of the pixmapOverlay
    QRectF bound = d->currentPixmapOverlay->boundingRect();
    QRect algBound = bound.toAlignedRect();
    Q_ASSERT(algBound.contains(r));
    d->currentPixmapOverlay->update(r);
}

void DocumentView::onImageRefinement(Image* img, QImage image, QTransform scale)
{
    if(img != this->d->currentImageDecoder->image().data())
    {
        // ignore events from a previous decoder that might still be running in the background
        return;
    }

    d->currentDocumentPixmap = QPixmap::fromImage(image, Qt::NoFormatConversion);
    d->currentPixmapOverlay->setPixmap(d->currentDocumentPixmap);
    d->currentPixmapOverlay->setTransform(scale, false);
    d->currentPixmapOverlay->setOffset(d->currentPixmapOverlay->mapFromScene(image.offset()));
    d->currentPixmapOverlay->show();

    d->scene->invalidate();
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
        d->messageWidget->hide();
        break;
    case DecodingState::Metadata:
        this->showImage(dec->image());
        break;
    case DecodingState::FullImage:
        d->thumbnailPreviewOverlay->hide();
        [[fallthrough]];
    case DecodingState::PreviewImage:
        // unset any previous error in case it magically recovered
        d->setDocumentError(QStringLiteral(""));
        break;
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
    
    d->latestDecodingState = static_cast<DecodingState>(newState);
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
    d->currentImageDecoder = dec;
    d->owningRefToImage = dec->image();
    if(d->owningRefToImage.isNull())
    {
        throw std::logic_error("Oops: DocumentView::loadImage() received a NULL image??");
    }
    this->loadImage();
}

void DocumentView::showImage(QSharedPointer<Image> img)
{
    xThreadGuard g(this);
    
    QSize fullImgSize = img->size();
    if(!fullImgSize.isValid())
    {
        // this can happen if the image has been yet started decoding
        auto fut = d->currentImageDecoder->decodeAsync(DecodingState::Metadata, Priority::Important, this->viewport()->rect().size(), QRect());
        fut.waitForFinished();
        auto state = fut.result();
        if(state == DecodingState::Error || state == DecodingState::Fatal)
        {
            QString name = img->fileInfo().fileName();
            d->setDocumentError(QString("Decoder failed to retrieve early metadata for file %1, error was %2").arg(name).arg(img->errorMessage()));
            return;
        }

        fullImgSize = img->size();
        if(!fullImgSize.isValid())
        {
            throw std::logic_error("Oops: Early metadata didn't report full image size, but no error was reported?!");
        }
    }

    if(fullImgSize.isValid())
    {
        this->setSceneRect(QRectF(QPointF(0,0), fullImgSize));
        
        if(d->latestDecodingState < DecodingState::Metadata)
        {
            d->latestDecodingState = DecodingState::Metadata;

            d->alignImageAccordingToViewMode(img, ANPV::globalInstance()->viewMode());
            
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
    bool visible = d->exifOverlay->setMetadata(img);
    d->exifOverlay->setVisible(visible && d->actionShowInfoBox->isChecked());
}

void DocumentView::loadImage()
{
    d->fovChangedTimer.stop();
    this->showImage(d->currentImageDecoder->image());

    QObject::connect(d->currentImageDecoder->image().data(), &Image::decodedImageChanged, this, &DocumentView::onImageRefinement);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::previewImageUpdated, this, &DocumentView::onPreviewImageUpdated);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::decodingStateChanged, this, &DocumentView::onDecodingStateChanged);
    QObject::connect(d->currentImageDecoder->image().data(), &Image::checkStateChanged, this, &DocumentView::onCheckStateChanged);

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
