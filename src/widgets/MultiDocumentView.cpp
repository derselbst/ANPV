#include "MultiDocumentView.hpp"

#include "DocumentView.hpp"
#include "Image.hpp"

#include <QTabWidget>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSharedPointer>
#include <QKeyEvent>

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
};

MultiDocumentView::MultiDocumentView(QWidget *parent)
 : QMainWindow(parent), d(std::make_unique<Impl>(this))
{
    QScreen *ps = QGuiApplication::primaryScreen();
    QRect screenres = ps->geometry();
    
    this->resize(screenres.width(), screenres.height());
    this->move(screenres.topLeft());
    
    d->tw = new QTabWidget(this);
    d->tw->setTabBarAutoHide(true);
    this->setCentralWidget(d->tw);
    
    this->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(d->tw, &QTabWidget::currentChanged, this, [&](int index) { d->onCurrentTabChanged(index); });
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
                d->tw->setTabIcon(idx, img->thumbnailTransformed(50));
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
        default:
            QMainWindow::keyPressEvent(event);
            break;
    }
}
