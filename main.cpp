
#include "DocumentController.hpp"
#include "DocumentView.hpp"


#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap("/home/tom/EigeneProgramme/ANPV/splash.jpg"));
    splash.show();
    
    splash.showMessage("Initializing objects...");
    QMainWindow m;
    DocumentController dc(&m);
    
    m.show();
    splash.finish(&m);
    
    splash.showMessage("Starting the image decoding task...");
    dc.loadImage(QString(argv[1]));
    
    return a.exec();
}
