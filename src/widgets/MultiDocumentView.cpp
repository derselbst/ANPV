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
    MultiDocumentView* q;
    QPointer<QTabWidget> tw = nullptr;
    
    Impl(MultiDocumentView* q) : q(q)
    {}
    
    void onCurrentTabChanged(int idx)
    {
        q->setWindowTitle(tw->tabText(idx));
        q->setWindowIcon(tw->tabIcon(idx));
    }
    
    void onTabCloseRequested(int idx)
    {
        QWidget* w = this->tw->widget(idx);
        this->tw->removeTab(idx);
        w->deleteLater();
        
        if(this->tw->count() == 0)
        {
            q->close();
        }
    }
        
    void writeSettings()
    {
        WaitCursor w;
        QSettings settings;
        settings.beginGroup("MultiDocumentView");
        settings.setValue("geometry", q->saveGeometry());
        settings.endGroup();
    }

    void readSettings(QMainWindow *parent)
    {
        WaitCursor w;
        QSettings settings;
        settings.beginGroup("MultiDocumentView");
        // open the window on the primary screen
        // by moving and resize it explicitly
        QByteArray parentGeo;
        if (parent)
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
        if (ANPV::globalInstance()->currentDir().isEmpty())
        {
            auto* dv = qobject_cast<DocumentView*>(this->tw->currentWidget());
            if (dv)
            {
                QFileInfo inf = dv->currentFile();
                ANPV::globalInstance()->setCurrentDir(inf.dir().absolutePath());
            }
        }

        ANPV::globalInstance()->showThumbnailView();
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
    
    connect(d->tw, &QTabWidget::currentChanged, this, [&](int index) { d->onCurrentTabChanged(index); });
    connect(d->tw, &QTabWidget::tabCloseRequested, this, [&](int index) { d->onTabCloseRequested(index); });

    QAction* closeAction = new QAction("Open ThumbnailView", this);
    closeAction->setShortcuts({ {Qt::Key_Escape} });
    closeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(closeAction, &QAction::triggered, this, [&]() { d->openThumbnailView(); });
    this->addAction(closeAction);

    QAction* sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);

    this->addActions(ANPV::globalInstance()->viewModeActionGroup()->actions());

    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);

    this->addActions(ANPV::globalInstance()->viewFlagActionGroup()->actions());
}

MultiDocumentView::~MultiDocumentView() = default;

void MultiDocumentView::addImages(const QList<QSharedPointer<Image>>& image, QSharedPointer<SortedImageModel> model)
{
    if(image.empty())
    {
        return;
    }
    
    for(auto& e : image)
    {
        DocumentView* dv = new DocumentView(this);

        connect(dv, &DocumentView::imageChanged, this,
        [=](QSharedPointer<Image> img)
        {
            int idx = d->tw->indexOf(dv);
            if(idx >= 0)
            {
                d->tw->setTabIcon(idx, img->thumbnailTransformed(d->tw->iconSize().height()));
                d->tw->setTabText(idx, img->fileInfo().fileName());
                // update title and icon of window, if this Image is the one currently active
                if(d->tw->currentIndex() == idx)
                {
                    d->onCurrentTabChanged(idx);
                }
            }
        });

        d->tw->addTab(dv, "");
        dv->setModel(model);
        dv->loadImage(e);
        dv->setAttribute(Qt::WA_DeleteOnClose);
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
