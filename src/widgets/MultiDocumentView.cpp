#include "MultiDocumentView.hpp"

#include "DocumentView.hpp"
#include "Image.hpp"

#include <QTabWidget>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSharedPointer>

struct MultiDocumentView::Impl
{
    MultiDocumentView* q;
    QTabWidget* tw;
};

MultiDocumentView::MultiDocumentView(QWidget *parent)
 : QMainWindow(parent), d(std::make_unique<Impl>(this))
{
    QScreen *ps = QGuiApplication::primaryScreen();
    QRect screenres = ps->geometry();
    
    this->resize(screenres.width(), screenres.height());
    this->move(screenres.topLeft());
    
    d->tw = new QTabWidget(this);
    this->setCentralWidget(d->tw);
    
    this->setAttribute(Qt::WA_DeleteOnClose);
}

MultiDocumentView::~MultiDocumentView() = default;

void MultiDocumentView::addImages(QList<QSharedPointer<Image>> image)
{
    for(auto& i : image)
    {
        DocumentView* dv = new DocumentView(d->tw);
        d->tw->addTab(dv, i->fileInfo().fileName());
        dv->loadImage(i);
    }
}
