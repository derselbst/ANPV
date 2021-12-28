
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "Formatter.hpp"

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
#include <QMessageBox>
#include <QThread>

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

    splash.showMessage("Initialize Decoder Factory");
    
    // set UI Thread to high prio
    QThread::currentThread()->setPriority(QThread::HighestPriority);

    // create and init DecoderFactory in main thread
    (void)DecoderFactory::globalInstance();

    ANPV anpv(&splash);
    
    QDir cur = anpv.currentDir();
    switch(argc)
    {
    case 1:
        anpv.fixupAndSetCurrentDir(anpv.savedCurrentDir());
        anpv.showThumbnailView(&splash);
        break;
    case 2:
    {
        QString arg(argv[1]);
        QFileInfo info(arg);
        if(info.exists())
        {
            if(info.isDir())
            {
                anpv.setCurrentDir(info.absoluteFilePath());
                anpv.showThumbnailView(&splash);
                break;
            }
            else if(info.isFile())
            {
                // fallthrough
            }
        }
        else
        {
            Formatter f;
            f << "Path '" << argv[1] << "' not found";
            QMessageBox::critical(nullptr, "ANPV", f.str().c_str());
            qCritical() << f.str().c_str();
            return -1;
        }
    }
    default:
    {
        QList<QSharedPointer<Image>> files;
        for(int i=1; i<argc;i++)
        {
            files.emplace_back(DecoderFactory::globalInstance()->makeImage(QFileInfo(QString(argv[i]))));
        }
        
        splash.showMessage("Starting the image decoding task...");
        anpv.openImages(files);
        splash.close();
    }
    break;
    }
    
    int r = app.exec();
    return r;
}
