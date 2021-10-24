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

void MultiDocumentView::addImages(const QList<QSharedPointer<Image>>& image, QSharedPointer<SortedImageModel> model)
{
    for(auto& i : image)
    {
        DocumentView* dv = new DocumentView(d->tw);

        connect(dv, &DocumentView::imageChanged, this,
        [=](QSharedPointer<Image> img)
        {
            int idx = d->tw->indexOf(dv);
            if(idx >= 0)
            {
                d->tw->setTabIcon(idx, img->thumbnail());
                d->tw->setTabText(idx, img->fileInfo().fileName());
                d->onCurrentTabChanged(idx);
            }
        });

        d->tw->addTab(dv, "");
        dv->setModel(model);
        dv->loadImage(i);
    }
    d->tw->currentWidget()->setFocus(Qt::PopupFocusReason);
}

void MultiDocumentView::keyPressEvent(QKeyEvent *event)
{
    // explitly pass on the keyevent to the currently shown DocumentView, because QTabWidget ignores all unknown events
//     d->tw->currentWidget()->QObject::event(event);
}
