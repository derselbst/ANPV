
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "UserCancellation.hpp"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QFutureWatcher>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <QPromise>
#include <QDir>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

#include "ANPV.hpp"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(ANPV);
    QApplication app(argc, argv);
    
    QSplashScreen splash(QPixmap(":/images/splash.jpg"));
    splash.show();
    
    // create and init DecoderFactory in main thread
    (void)DecoderFactory::globalInstance();

    ANPV anpv(&splash);
    
    if(argc == 2)
    {
        QString arg(argv[1]);
        QFileInfo info(arg);
        if(info.exists())
        {
            if(info.isDir())
            {
                anpv.setCurrentDir(info.absoluteFilePath());
                anpv.showThumbnailView();
            }
            else if(info.isFile())
            {
                goto openFiles;
            }
        }
        else
        {
            qCritical() << "Path '" << argv[1] << "' not found";
            return -1;
        }
    }
    else
    {
openFiles:
        QList<QFileInfo> files;
        for(i=1; i<argc;i++)
        {
            files.emplace_back(QFileInfo(QString(argv[i])));
        }
        
        // create symlinks into temporary dir
        
        // maybe change path to temp dir, maybe not
        
        // open images
        
        splash.showMessage("Starting the image decoding task...");
        anpv.openImages(files);
    }
    
    int r = app.exec();
    return r;
}
