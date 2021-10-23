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

void MultiDocumentView::addImages(QList<QSharedPointer<Image>> image)
{
    for(auto& i : image)
    {
        DocumentView* dv = new DocumentView(d->tw);
        int tabIdx = d->tw->addTab(dv, i->fileInfo().fileName());
        d->tw->setTabIcon(tabIdx, i->thumbnail());
        dv->loadImage(i);
    }
}

void MultiDocumentView::keyPressEvent(QKeyEvent *event)
{
    // explitly pass on the keyevent to the currently shown DocumentView, because QTabWidget ignores all unknown events
//     d->tw->currentWidget()->QObject::event(event);
}
