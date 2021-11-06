#include "ANPV.hpp"

#include <QMainWindow>
#include <QProgressBar>
#include <QStackedLayout>
#include <QWidget>
#include <QSplashScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QFileIconProvider>
#include <QScreen>
#include <QtDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <QDir>
#include <QFileSystemModel>
#include <QListView>
#include <QActionGroup>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QUndoStack>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QTabWidget>
#include <QSvgRenderer>

#include "DocumentView.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"
#include "MultiDocumentView.hpp"
#include "MainWindow.hpp"

class MyDisabledFileIconProvider : public QAbstractFileIconProvider
{
public:
    MyDisabledFileIconProvider() = default;
    ~MyDisabledFileIconProvider() override = default;
    
    // The default implementation of this function is so horribly slow. It spams the UI thread with a bunch of useless events.
    // Disable this, to make it use the icon(QAbstractFileIconProvider::IconType type) overload.
    QIcon icon(const QFileInfo &info) const override
    {
        return QIcon();
    }
};

static QPointer<ANPV> global;

struct ANPV::Impl
{
    ANPV* q;
    
    // normal objects without parent
    QScopedPointer<QAbstractFileIconProvider> iconProvider;
    QScopedPointer<MyDisabledFileIconProvider> noIconProvider;
    QPixmap noIconPixmap;
    
    // QObjects without parent
    QScopedPointer<MainWindow, QScopedPointerDeleteLater> mainWindow;
    
    // QObjects with parent
    QFileSystemModel* dirModel = nullptr;
    QSharedPointer<SortedImageModel> fileModel = nullptr;
    
    QDir currentDir;
    ViewMode viewMode = ViewMode::Unknown;
    ViewFlags_t viewFlags = static_cast<ViewFlags_t>(ViewFlag::None);
    Qt::SortOrder sortOrder = static_cast<Qt::SortOrder>(-1);
    SortedImageModel::Column primarySortColumn = SortedImageModel::Column::Unknown;
    int iconHeight = -1;
    
    Impl(ANPV* parent) : q(parent)
    {
    }
    
    void initLogic()
    {
        if(::global.isNull())
        {
            ::global = QPointer<ANPV>(q);
        }
        
        this->iconProvider.reset(new QFileIconProvider());
        this->iconProvider->setOptions(QAbstractFileIconProvider::DontUseCustomDirectoryIcons);
        this->noIconProvider.reset(new MyDisabledFileIconProvider());
        
        this->dirModel = new QFileSystemModel(q);
        this->dirModel->setIconProvider(this->noIconProvider.get());
        this->dirModel->setRootPath("");
        this->dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
        
        this->fileModel.reset(new SortedImageModel(q));
    }
    
    void connectLogic()
    {
        connect(q, &ANPV::iconHeightChanged, q, [&](int, int){ this->drawNoIconPixmap(); });
        connect(q, &ANPV::currentDirChanged, q,
                [&](QDir,QDir)
                {
                    auto fut = this->fileModel->changeDirAsync(this->currentDir);
                    q->addBackgroundTask(ProgressGroup::Directory, fut);
                });
    }
    
    void writeSettings()
    {
        QSettings settings;

        settings.setValue("currentDir", q->currentDir().absolutePath());
        settings.setValue("viewMode", static_cast<int>(q->viewMode()));
        settings.setValue("viewFlags", q->viewFlags());
        settings.setValue("sortOrder", static_cast<int>(q->sortOrder()));
        settings.setValue("primarySortColumn", static_cast<int>(q->primarySortColumn()));
        settings.setValue("iconHeight", q->iconHeight());
    }

    void readSettings()
    {
        QSettings settings;

        q->setCurrentDir(settings.value("currentDir", qgetenv("HOME")).toString());
        q->setViewMode(static_cast<ViewMode>(settings.value("viewMode", static_cast<int>(ViewMode::Fit)).toInt()));
        q->setViewFlags(settings.value("viewFlags", static_cast<ViewFlags_t>(ViewFlag::None)).toUInt());
        q->setSortOrder(static_cast<Qt::SortOrder>(settings.value("sortOrder", Qt::AscendingOrder).toInt()));
        q->setPrimarySortColumn(static_cast<SortedImageModel::Column>(settings.value("primarySortColumn", static_cast<int>(SortedImageModel::Column::FileName)).toInt()));
        q->setIconHeight(settings.value("iconHeight", 150).toInt());
    }
    
    void drawNoIconPixmap()
    {
        QSvgRenderer renderer(QString(":/images/FileNotFound.svg"));

        QSize imgSize = renderer.defaultSize().scaled(this->iconHeight, this->iconHeight, Qt::KeepAspectRatio);
        QImage image(imgSize, QImage::Format_ARGB32);
        image.fill(0);

        QPainter painter(&image);
        renderer.render(&painter);

        this->noIconPixmap = QPixmap::fromImage(image);
    }
};

ANPV* ANPV::globalInstance()
{
    return global.get();
}


ANPV::ANPV() : d(std::make_unique<Impl>(this))
{
    d->initLogic();
    d->readSettings();
    d->drawNoIconPixmap();
}

ANPV::ANPV(QSplashScreen *splash)
 : d(std::make_unique<Impl>(this))
{
    QCoreApplication::setOrganizationName("derselbst");
    QCoreApplication::setOrganizationDomain("");
    QCoreApplication::setApplicationName("ANPV");

    splash->showMessage("Creating logic");
    d->initLogic();

    splash->showMessage("Connecting logic");
    d->connectLogic();
    
    splash->showMessage("Creating UI Widgets");
    d->mainWindow.reset(new MainWindow(splash));
    d->mainWindow->show();
    
    splash->showMessage("Reading latest settings");
    d->readSettings();
    d->mainWindow->readSettings();
    
    splash->showMessage("ANPV initialized, waiting for Qt-Framework getting it's events processed...");
    splash->finish(d->mainWindow.get());
}

ANPV::~ANPV()
{
    d->writeSettings();
}

QAbstractFileIconProvider* ANPV::iconProvider()
{
    return d->iconProvider.get();
}

QFileSystemModel* ANPV::dirModel()
{
    return d->dirModel;
}

QSharedPointer<SortedImageModel> ANPV::fileModel()
{
    return d->fileModel;
}

QDir ANPV::currentDir()
{
    xThreadGuard(this);
    return d->currentDir;
}

void ANPV::setCurrentDir(QString str)
{
    xThreadGuard(this);
    
    QDir old = d->currentDir;
    if(old != str)
    {
        d->currentDir = str;
        emit this->currentDirChanged(str, old);
    }
}

ViewMode ANPV::viewMode()
{
    xThreadGuard(this);
    return d->viewMode;
}

void ANPV::setViewMode(ViewMode v)
{
    xThreadGuard(this);
    ViewMode old = d->viewMode;
    if(old != v)
    {
        d->viewMode = v;
        emit this->viewModeChanged(v, old);
    }
}

ViewFlags_t ANPV::viewFlags()
{
    xThreadGuard(this);
    return d->viewFlags;
}

void ANPV::setViewFlags(ViewFlags_t newFlags)
{
    xThreadGuard(this);
    ViewFlags_t old = d->viewFlags;
    if(old != newFlags)
    {
        d->viewFlags = newFlags;
        emit this->viewFlagsChanged(newFlags, old);
    }
}

void ANPV::setViewFlag(ViewFlag v, bool on)
{
    xThreadGuard(this);
    ViewFlags_t newFlags = d->viewFlags;
    
    if(on)
    {
        newFlags |= static_cast<ViewFlags_t>(v);
    }
    else
    {
        newFlags &= ~static_cast<ViewFlags_t>(v);
    }
    this->setViewFlags(newFlags);
}

Qt::SortOrder ANPV::sortOrder()
{
    xThreadGuard(this);
    return d->sortOrder;
}

void ANPV::setSortOrder(Qt::SortOrder order)
{
    xThreadGuard(this);
    Qt::SortOrder old = d->sortOrder;
    if(order != old)
    {
        d->sortOrder = order;
        emit this->sortOrderChanged(order, old);
    }
}

SortedImageModel::Column ANPV::primarySortColumn()
{
    xThreadGuard(this);
    return d->primarySortColumn;
}

void ANPV::setPrimarySortColumn(SortedImageModel::Column col)
{
    xThreadGuard(this);
    SortedImageModel::Column old = d->primarySortColumn;
    if(old != col)
    {
        d->primarySortColumn = col;
        emit this->primarySortColumnChanged(col, old);
    }
}

int ANPV::iconHeight()
{
    xThreadGuard(this);
    return d->iconHeight;
}

void ANPV::setIconHeight(int h)
{
    xThreadGuard(this);
    int old = d->iconHeight;
    h = std::min(h, ANPV::MaxIconHeight);
    if(old != h)
    {
        d->iconHeight = h;
        emit iconHeightChanged(h, old);
    }
}

void ANPV::showThumbnailView(QSharedPointer<Image> img)
{
    d->mainWindow->setWindowState( (d->mainWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    d->mainWindow->raise();
    d->mainWindow->activateWindow();
    d->mainWindow->setCurrentIndex(img);
}

void ANPV::openImages(const QList<QSharedPointer<Image>>& image)
{
    MultiDocumentView* mdv = new MultiDocumentView(d->mainWindow.get());
    mdv->show();
    mdv->addImages(image, d->fileModel);
}

void ANPV::addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut)
{
    if(d->mainWindow)
    {
        d->mainWindow->addBackgroundTask(group, fut);
    }
}

void ANPV::hideProgressWidget(CancellableProgressWidget* w)
{
    if(d->mainWindow)
    {
        d->mainWindow->hideProgressWidget(w);
    }
}

QPixmap ANPV::noIconPixmap()
{
    return d->noIconPixmap;
}

void ANPV::moveFilesSlot(const QString& targetDir)
{
//     if(targetDir.isEmpty())
//     {
//         return;
//     }
//     
//     if(d->stackedLayout->currentWidget() == d->thumbnailViewer)
//     {
//         QList<QString> selectedFileNames;
//         QDir curDir = d->thumbnailViewer->currentDir();
//         d->thumbnailViewer->selectedFiles(selectedFileNames);
//         
//         this->moveFilesSlot(selectedFileNames, curDir.absolutePath(), targetDir);
//     }
//     else if(d->stackedLayout->currentWidget() == d->imageViewer)
//     {
//         QFileInfo info = d->imageViewer->currentFile();
//         if(!info.filePath().isEmpty())
//         {
//             QList<QString> files;
//             files.append(info.fileName());
//             this->moveFilesSlot(files, info.absoluteDir().absolutePath(), targetDir);
//         }
//     }
}

void ANPV::moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir)
{
//     MoveFileCommand* cmd = new MoveFileCommand(files, sourceDir, targetDir);
//     
//     connect(cmd, &MoveFileCommand::moveFailed, this, [&](QList<QPair<QString, QString>> failedFilesWithReason)
//     {
//         QMessageBox box(QMessageBox::Critical,
//                     "Move operation failed",
//                     "Some files could not be moved to the destination folder. See details below.",
//                     QMessageBox::Ok,
//                     d->mainWindow.get());
//         
//         QString details;
//         for(int i=0; i<failedFilesWithReason.size(); i++)
//         {
//             QPair<QString, QString>& p = failedFilesWithReason[i];
//             details += p.first;
//             
//             if(!p.second.isEmpty())
//             {
//                 details += QString(": ") + p.second;
//                 details += "\n";
//             }
//         }
//         box.setDetailedText(details);
//         box.setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
//         box.exec();
//     });
//     
//     d->undoStack->push(cmd);
}

void ANPV::about()
{
    // build up the huge constexpr about anmp string
    static constexpr char text[] = "<p>\n"
                                   "<b>ANPV - Another Nameless Picture Viewer</b><br />\n"
                                   "<br />\n"
                                   "Version: " ANPV_VERSION "<br />\n"
                                   "<br />\n"
    "Website: <a href=\"https://github.com/derselbst/ANPV\">https://github.com/derselbst/ANPV</a><br />\n"
    "<br />\n"
    "<small>"
    "&copy;Tom Moebert (derselbst)<br />\n"
    "<br />\n"
    "This program is free software; you can redistribute it and/or modify it"
    "<br />\n"
    "under the terms of the GNU Affero Public License version 3."
    "</small>"
    "</p>\n";

    QMessageBox::about(d->mainWindow.get(), "About ANPV", text);
}
