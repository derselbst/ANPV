#include "MultiDocumentView.hpp"

#include "DocumentView.hpp"
#include "Image.hpp"

#include <QTabWidget>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSharedPointer>
#include <QKeyEvent>
#include <QSettings>

struct MultiDocumentView::Impl
{
    MultiDocumentView* q;
    QTabWidget* tw;
    
    Impl(MultiDocumentView* q) : q(q)
    {}
    
    void onCurrentTabChanged(int idx)
    {
        q->setWindowTitle(tw->tabText(idx));
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
        QSettings settings;
        settings.beginGroup("MultiDocumentView");
        settings.setValue("geometry", q->saveGeometry());
        settings.setValue("windowState", q->saveState());
        settings.endGroup();
    }

    void readSettings(QMainWindow *parent)
    {
        QSettings settings;
        settings.beginGroup("MultiDocumentView");
        // open the window on the primary screen
        // by moving and resize it explicitly
        q->restoreGeometry(settings.value("geometry", parent->saveGeometry()).toByteArray());
        q->restoreState(settings.value("windowState", parent->saveState()).toByteArray());
        settings.endGroup();
    }
};

MultiDocumentView::MultiDocumentView(QMainWindow *parent)
 : QMainWindow(parent), d(std::make_unique<Impl>(this))
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
}

MultiDocumentView::~MultiDocumentView() = default;

void MultiDocumentView::addImages(const QList<QSharedPointer<Image>>& image, QSharedPointer<SortedImageModel> model)
{
    if(image.empty())
    {
        return;
    }
    
    for(auto& i : image)
    {
        DocumentView* dv = new DocumentView(d->tw);

        connect(dv, &DocumentView::imageChanged, this,
        [=](QSharedPointer<Image> img)
        {
            int idx = d->tw->indexOf(dv);
            if(idx >= 0)
            {
                d->tw->setTabIcon(idx, img->thumbnailTransformed(d->tw->iconSize().height()));
                d->tw->setTabText(idx, img->fileInfo().fileName());
                d->onCurrentTabChanged(idx);
            }
        });

        d->tw->addTab(dv, "");
        dv->setModel(model);
        dv->loadImage(i);
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
