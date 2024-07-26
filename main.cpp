
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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
char* fluid_get_windows_error(void)
{
    static TCHAR err[1024];

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        err,
        sizeof(err) / sizeof(err[0]),
        NULL);

#ifdef _UNICODE
    static char ascii_err[sizeof(err)];

    WideCharToMultiByte(CP_UTF8, 0, err, -1, ascii_err, sizeof(ascii_err) / sizeof(ascii_err[0]), 0, 0);
    return ascii_err;
#else
    return err;
#endif
}

QString getLongPathName(const char* shortPath8)
{
    auto shortPath16 = QString::fromLocal8Bit(shortPath8);

    QString longPath;
    auto len = GetLongPathNameW((LPCTSTR)shortPath16.utf16(), nullptr, 0);
    if (len == 0)
    {
        throw std::runtime_error(fluid_get_windows_error());
    }

    longPath.resize(len-1);

    len = GetLongPathNameW((LPCTSTR)shortPath16.utf16(), (LPWSTR)longPath.data(), len);
    if (len == 0)
    {
        throw std::runtime_error(fluid_get_windows_error());
    }
    return longPath;
}

#else
inline QString getLongPathName(const char* path)
{
    return QString::fromLocal8Bit(path);
}
#endif

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
            QString arg = getLongPathName(argv[1]);
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
                QFileInfo fileInfo(getLongPathName(argv[i]));

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
