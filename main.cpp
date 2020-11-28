
#include "DocumentView.hpp"
#include "ANPV.hpp"


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
#include <QDir>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    QSplashScreen splash(QPixmap("/home/tom/EigeneProgramme/ANPV/splash.jpg"));
    splash.show();
    
    ANPV m(&splash);
    /*
    splash.showMessage("Initializing objects...");
    QMainWindow m;
    DocumentController dc(&m);
    */
    m.show();
    splash.finish(&m);
    
    if(argc == 2)
    {
        QString arg(argv[1]);
        QFileInfo info(arg);
        if(info.exists())
        {
            if(info.isDir())
            {
                m.showThumbnailView();
                m.setThumbnailDir(arg);
            }
            else if(info.isFile())
            {
                m.showImageView();
                m.loadImage(info);
                splash.showMessage("Starting the image decoding task...");
            }
        }
        else
        {
            qCritical() << "Path '" << argv[1] << "' not found";
        }
    }
    else
    {
        m.showThumbnailView();
        m.setThumbnailDir(QDir::currentPath());
    }
    
    return a.exec();
}
