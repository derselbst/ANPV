
#include "DocumentView.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"


#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>
#include <QThreadPool>
#include <QFileInfo>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

QGraphicsPixmapItem* prev=nullptr;
QGraphicsScene* s;
DocumentView* v;
QPixmap *p;

void testBegin()
{
    if(prev)
        s->removeItem(prev);
    delete prev;
    prev = nullptr;
}

void test()
{
    if(p == nullptr)
        return;
    
    // get the area of what the user sees
     QRect viewportRect = v->viewport()->rect();
     
     // and map that rect to scene coordinates
     QRectF viewportRectScene = v->mapToScene(viewportRect).boundingRect();
     
     // the user might have zoomed out too far, crop the rect, as we are not interseted in the surrounding void
     QRectF visPixRect = viewportRectScene.intersected(v->sceneRect());
     
     // the "inverted zoom factor"
     // 1.0 means the pixmap is shown at native size
     // >1.0 means the user zoomed out
     // <1.0 mean the user zommed in and sees the individual pixels
     auto newScale = std::max(visPixRect.width() / viewportRect.width(), visPixRect.height() / viewportRect.height());
     
     qWarning() << newScale << "\n";
     
     if (newScale > 1.0)
     {
         QPixmap imgToScale;
         
         if(viewportRectScene.contains(v->sceneRect()))
         {
             // the user sees the entire image
             imgToScale = *p;
         }
         else
         {
             // the user sees a part of the image
             // crop the image to the visible part, so we don't need to scale the entire one
             imgToScale = p->copy(visPixRect.toAlignedRect());
         }
        QPixmap scaled = imgToScale.scaled(viewportRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        prev = new QGraphicsPixmapItem(std::move(scaled));
        prev->setPos(visPixRect.topLeft());
        prev->setScale( newScale );
        s->addItem(prev);
     }
     else
     {
         qDebug() << "Skipping smooth pixmap scaling: Too far zoomed in";
     }
}

void onDecodingStateChanged(SmartImageDecoder* self, DecodingState newState, DecodingState oldState)
{
    switch(newState)
    {
        case DecodingState::Metadata:
            break;
        case DecodingState::PreviewImage:
            if(oldState == DecodingState::Metadata)
            {
                s->addPixmap(QPixmap::fromImage(self->image()));
                break;
            }
            else
            {
                s->invalidate(s->sceneRect());
            }
            break;
        default:
            break;
    }
}

void onDecodingProgress(SmartImageDecoder* self, int progress, QString message)
{
    qWarning() << message << progress << " %";
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap("/home/tom/EigeneProgramme/ANPV/splash.jpg"));
    splash.show();
    
    splash.showMessage("Initializing objects...");
    
    
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    auto screenSize = primaryScreen->availableVirtualSize();
    
    QGraphicsScene scene; s = &scene;
    DocumentView view(&scene); v= &view;
    QObject::connect(&view, &DocumentView::fovChangedBegin, &::testBegin);
    QObject::connect(&view, &DocumentView::fovChangedEnd, &::test);
    
    view.show();
    scene.addRect(QRectF(0, 0, 100, 100));
    
    
    splash.showMessage("Starting the image decoding task...");
    
    std::unique_ptr<SmartImageDecoder> sid = DecoderFactory::load(QString(argv[1]));
    QObject::connect(sid.get(), &SmartImageDecoder::decodingStateChanged, &::onDecodingStateChanged);
    QObject::connect(sid.get(), &SmartImageDecoder::decodingProgress, &::onDecodingProgress);
    
    ImageDecodeTask t(sid.get());
    QThreadPool::globalInstance()->start(&t);
    
    
    
    
    
    splash.finish(&view);
    return a.exec();
}
