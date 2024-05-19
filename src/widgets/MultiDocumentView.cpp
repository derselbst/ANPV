#include "MultiDocumentView.hpp"

#include "DocumentView.hpp"
#include "Image.hpp"
#include "SortedImageModel.hpp"
#include "WaitCursor.hpp"
#include "ANPV.hpp"

#include <QTabWidget>
#include <QSharedPointer>
#include <QKeyEvent>
#include <QSettings>
#include <QAction>
#include <QActionGroup>
#include <QPointer>

struct MultiDocumentView::Impl
{
    MultiDocumentView *q;
    QPointer<QTabWidget> tw = nullptr;

    Impl(MultiDocumentView *q) : q(q)
    {}

    void onCurrentTabChanged(int idx)
    {
        q->setWindowTitle(tw->tabText(idx));
        q->setWindowIcon(tw->tabIcon(idx));
    }

    void onTabCloseRequested(int idx)
    {
        QWidget *w = this->tw->widget(idx);
        this->tw->removeTab(idx);
        w->deleteLater();

        if(this->tw->count() == 0)
        {
            q->close();
        }
    }

    void writeSettings()
    {
        auto &settings = ANPV::globalInstance()->settings();
        settings.beginGroup("MultiDocumentView");
        settings.setValue("geometry", q->saveGeometry());
        settings.endGroup();
        
        auto dv = dynamic_cast<DocumentView*>(this->tw->currentWidget());
        if (dv) // will be NULL closing last remaining tab with CTRL+W
        {
            settings.beginGroup("DocumentView");
            dv->writeSettings(settings);
            settings.endGroup();
        }
    }

    void readSettings(QMainWindow *parent)
    {
        auto &settings = ANPV::globalInstance()->settings();
        settings.beginGroup("MultiDocumentView");
        // open the window on the primary screen
        // by moving and resize it explicitly
        QByteArray parentGeo;

        if(parent)
        {
            parentGeo = parent->saveGeometry();
        }

        QByteArray settingsGeo = settings.value("geometry", parentGeo).toByteArray();
        q->restoreGeometry(settingsGeo);
        settings.endGroup();
    }

    void openThumbnailView()
    {
        WaitCursor w;

        bool previouslyVisible = ANPV::globalInstance()->showThumbnailView();

        auto* dv = qobject_cast<DocumentView*>(this->tw->currentWidget());
        if (dv)
        {
            QFileInfo inf = dv->currentFile();
            ANPV::globalInstance()->setCurrentDir(inf.dir().absolutePath(), !previouslyVisible);
        }

        q->close();
    }
};

MultiDocumentView::MultiDocumentView(QMainWindow *parent)
    : QMainWindow(nullptr /* i.e. always treat it as new, independent Window */), d(std::make_unique<Impl>(this))
{
    d->tw = new QTabWidget(this);
    d->tw->setTabBarAutoHide(true);
    d->tw->setTabsClosable(true);
    d->tw->setMovable(true);
    this->setCentralWidget(d->tw);

    this->setAttribute(Qt::WA_DeleteOnClose);

    d->readSettings(parent);

    connect(d->tw, &QTabWidget::currentChanged, this, [&](int index)
    {
        d->onCurrentTabChanged(index);
    });
    connect(d->tw, &QTabWidget::tabCloseRequested, this, [&](int index)
    {
        d->onTabCloseRequested(index);
    });

    QAction *closeAction = new QAction("Open ThumbnailView", this);
    closeAction->setShortcuts({ {Qt::Key_Escape} });
    closeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(closeAction, &QAction::triggered, this, [&]()
    {
        d->openThumbnailView();
    });
    this->addAction(closeAction);

    QAction *sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);

    this->addActions(ANPV::globalInstance()->viewModeActionGroup()->actions());

    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);

    this->addActions(ANPV::globalInstance()->viewFlagActionGroup()->actions());
}

MultiDocumentView::~MultiDocumentView() = default;

void MultiDocumentView::addImages(const QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> &imageWithModel)
{
    if(imageWithModel.empty())
    {
        return;
    }

    for(auto& [e, model] : imageWithModel)
    {
        DocumentView *dv = new DocumentView(this);

        connect(dv, &DocumentView::imageAboutToBeChanged, this,
            [=](QSharedPointer<Image> img)
            {
                // disconnect Image::thumbnailChanged signal
                QObject::disconnect(img.data(), nullptr, this, nullptr);
            });

        connect(dv, &DocumentView::imageChanged, this,
                [ = ](QSharedPointer<Image> img)
        {
            int idx = d->tw->indexOf(dv);

            if (idx >= 0)
            {
                QString text = img->fileInfo().fileName();
                d->tw->setTabText(idx, text);

                QPixmap icon = img->thumbnailTransformed(d->tw->iconSize().height());
                QImage thumb = img->thumbnail();
                if (!thumb.isNull())
                {
                    d->tw->setTabIcon(idx, icon);

                    // update title and icon of window, if this Image is the one currently active
                    if (d->tw->currentIndex() == idx)
                    {
                        this->setWindowTitle(text);
                        this->setWindowIcon(icon);
                    }
                }
                else
                {
                    // The image might not yet have a thumbnail
                    connect(img.get(), &Image::thumbnailChanged, this,
                        [=](Image* sender, QImage thumb)
                        {
                            if (!thumb.isNull())
                            {
                                QPixmap pix = sender->thumbnailTransformed(d->tw->iconSize().height());
                                d->tw->setTabIcon(idx, pix);
                                // update title and icon of window, if this Image is the one currently active
                                if (d->tw->currentIndex() == idx)
                                {
                                    this->setWindowIcon(pix);
                                }
                            }
                        });

                    d->tw->setTabIcon(idx, icon);

                    // update title and icon of window, if this Image is the one currently active
                    if (d->tw->currentIndex() == idx)
                    {
                        this->setWindowTitle(text);
                        // this will set the "no thumbnail" thumbnail until a real thumbnail has been generated
                        this->setWindowIcon(icon);
                    }
                }
            }
        });

        d->tw->addTab(dv, "");
        dv->setModel(model);
        dv->loadImage(e);
        dv->setAttribute(Qt::WA_DeleteOnClose);

        auto& settings = ANPV::globalInstance()->settings();
        settings.beginGroup("DocumentView");
        dv->readSettings(settings);
        settings.endGroup();
    }

    d->tw->currentWidget()->setFocus(Qt::PopupFocusReason);
}

void MultiDocumentView::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
    case Qt::Key_Escape:
        event->accept();
        this->close();
        break;

    case Qt::Key_W:
        if(event->modifiers() & Qt::ControlModifier)
        {
            event->accept();
            d->onTabCloseRequested(d->tw->currentIndex());
            break;
        }

        [[fallthrough]];

    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

void MultiDocumentView::closeEvent(QCloseEvent *event)
{
    d->writeSettings();
    QMainWindow::closeEvent(event);
}
