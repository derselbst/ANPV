
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "types.hpp"
#include "CenteredBoxProxyStyle.hpp"
#include "ImageSectionDataContainer.hpp"
#include "DirectoryWorker.hpp"
#include "TomsSplash.hpp"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QFutureWatcher>
#include <QPixmap>
#include <QGraphicsPixmapItem>
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
#include <QStyleFactory>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

#include "ANPV.hpp"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(ANPV);
    Q_INIT_RESOURCE(oxygen);
    QApplication app(argc, argv);
    app.setOrganizationName("derselbst");
    app.setApplicationName("ANPV");

    TomsSplash splash;

    splash.showMessage(QStringLiteral("Initialize Decoder Factory"));
    
    // set UI Thread to high prio
    QThread::currentThread()->setPriority(QThread::HighestPriority);

    QThreadPool::globalInstance()->setThreadPriority(QThread::LowPriority);
    QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount() / 2);

    // create and init DecoderFactory in main thread
    (void)DecoderFactory::globalInstance();

    splash.showMessage(QStringLiteral("Setting application-wide style"));
    
    app.setStyle(new CenteredBoxProxyStyle(QStyleFactory::create("Fusion")));
    if (QIcon::themeName().isEmpty())
    {
        QIcon::setThemeName("oxygen");
    }

    try
    {
        ANPV anpv(&splash);

        QDir cur = anpv.currentDir();
        switch (argc)
        {
        case 1:
            anpv.fixupAndSetCurrentDir(anpv.savedCurrentDir());
            anpv.showThumbnailView(&splash);
            break;
        case 2:
        {
            QString arg = QString::fromLocal8Bit(argv[1]);
            QFileInfo info(arg);
            if (info.exists() && info.isDir())
            {
                anpv.setCurrentDir(info.absoluteFilePath());
                anpv.showThumbnailView(&splash);
                break;
            }
            // else
            [[fallthrough]];
        }
        default:
        {
            QSharedPointer<ImageSectionDataContainer> currentDirModel;
            QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> imagesWithFileModel;
            QFileInfo prevFileInfo;
            for (int i = 1; i < argc; i++)
            {
                QFileInfo fileInfo(QString::fromLocal8Bit(argv[i]));

                if (!fileInfo.exists())
                {
                    Formatter f;
                    f << "Path '" << argv[i] << "' not found";
                    QMessageBox::critical(nullptr, "ANPV", f.str().c_str());
                    qCritical() << f.str().c_str();
                    return -1;
                }

                if (currentDirModel == nullptr || fileInfo.canonicalPath() != prevFileInfo.canonicalPath())
                {
                    splash.showMessage(QString("Discover directory contents %1").arg(fileInfo.canonicalPath()));

                    currentDirModel.reset(new ImageSectionDataContainer(nullptr));
                    currentDirModel->sortSections(SortField::None, Qt::AscendingOrder);
                    currentDirModel->sortImageItems(SortField::FileName, Qt::AscendingOrder);
                    DirectoryWorker w(currentDirModel.data());
                    auto task = w.changeDirAsync(fileInfo.canonicalPath());
                    app.processEvents(QEventLoop::AllEvents);
                    task.waitForFinished();
                }

                auto emplacedImage = AbstractListItem::imageCast(currentDirModel->getItemByLinearIndex(currentDirModel->getLinearIndexOfItem(fileInfo)));
                if (emplacedImage.isNull())
                {
                    throw std::logic_error("This shouldn't happen: emplacedImage is null!");
                }

                imagesWithFileModel.push_back({ emplacedImage, currentDirModel });
                prevFileInfo = fileInfo;
            }

            splash.showMessage("Starting the image decoding task...");
            anpv.openImages(imagesWithFileModel);
            splash.close();
        }
        break;
        }

        int r = app.exec();
        return r;
    }
    catch(const std::exception& e)
    {
        Formatter f;
        f << "An unexpected error caused ANPV to terminate.\n"
            "Error Type: " << typeid(e).name() << "\n"
            "Error Message: \n" << e.what();
        QMessageBox::critical(nullptr, "Unexpected error", f.str().c_str());
    }
    return -1;
}
