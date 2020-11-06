
#include "DocumentController.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"
#include "DocumentView.hpp"


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

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap("/home/tom/EigeneProgramme/ANPV/splash.jpg"));
    splash.show();
    
    splash.showMessage("Initializing objects...");
    
    
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    auto screenSize = primaryScreen->availableVirtualSize();

    DocumentController dc;

    splash.showMessage("Starting the image decoding task...");
    std::unique_ptr<SmartImageDecoder> sid = DecoderFactory::load(QString(argv[1]));
    QObject::connect(sid.get(), &SmartImageDecoder::decodingStateChanged, &dc, &DocumentController::onDecodingStateChanged);
    QObject::connect(sid.get(), &SmartImageDecoder::decodingProgress, &dc, &DocumentController::onDecodingProgress);
    
    ImageDecodeTask t(sid.get());
    QThreadPool::globalInstance()->start(&t);
    
        
    splash.finish(dc.documentView());
    return a.exec();
}
