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

#include "DocumentView.hpp"
#include "Image.hpp"
#include "ThumbnailView.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"
#include "MainWindow.hpp"


struct ANPV::Impl
{
    ANPV* q;
    
    // QObjects without parent
    QScopedPointer<MainWindow, QScopedPointerDeleteLater> mainWindow;
    
    // QObjects with parent
    QFileSystemModel* dirModel = nullptr;
    
    QDir currentDir;
    ViewMode viewMode = ViewMode::Unknown;
    ViewFlags_t viewFlags = static_cast<ViewFlags_t>(ViewFlag::None);
    Qt::SortOrder sortOrder = static_cast<Qt::SortOrder>(-1);
    SortedImageModel::Column primarySortColumn = SortedImageModel::Column::Unknown;
    int iconHeight = -1;
    
    Impl(ANPV* parent) : q(parent)
    {
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
    
    void onImageNavigate(const QString& url, int stepsForward)
    {
//         QModelIndex idx;
//         QSharedPointer<SmartImageDecoder> dec = fileModel->goTo(url, stepsForward, idx);
//         if(dec && idx.isValid())
//         {
//             q->loadImage(dec);
//             thumbnailViewer->selectThumbnail(idx);
//         }
//         else
//         {
//             q->showThumbnailView();
//         }
    }
};

static QPointer<ANPV> global;
ANPV* ANPV::globalInstance()
{
    return global.get();
}

ANPV::ANPV(QSplashScreen *splash)
 : d(std::make_unique<Impl>(this))
{
    QCoreApplication::setOrganizationName("derselbst");
    QCoreApplication::setOrganizationDomain("");
    QCoreApplication::setApplicationName("ANPV");

    splash->showMessage("Creating logic");
//     d->fileModel = new SortedImageModel(this);
    if(::global.isNull())
    {
        ::global = QPointer<ANPV>(this);
    }
    
    d->dirModel = new QFileSystemModel(this);
    d->dirModel->setRootPath("");
    d->dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    
    splash->showMessage("Connecting logic");
    
    
    splash->showMessage("Creating UI Widgets");
    d->mainWindow.reset(new MainWindow(splash));
//     d->mainWindow->setModel(d->fileModel);
    
    
    d->mainWindow->show();
    
    
    
    splash->showMessage("Reading latest settings");
    d->readSettings();
    splash->finish(d->mainWindow.get());
    
    
//     d->progressWidgetLayout = new QVBoxLayout(this);
//     d->progressWidgetContainer = new QWidget(this);
//     d->progressWidgetContainer->setLayout(d->progressWidgetLayout);
//     this->statusBar()->showMessage(tr("Ready"));
//     this->statusBar()->addPermanentWidget(d->progressWidgetContainer, 1);
//     
//     d->thumbnailViewer = new ThumbnailView(d->fileModel, this);
//     
//     d->imageViewer = new DocumentView(this);
//     connect(d->imageViewer, &DocumentView::requestNext, this,
//             [&](QString current) { d->onImageNavigate(current, 1); });
//     connect(d->imageViewer, &DocumentView::requestPrev, this,
//             [&](QString current) { d->onImageNavigate(current, -1); });
// 
//     d->stackedLayout = new QStackedLayout(this);
//     d->stackedLayout->addWidget(d->thumbnailViewer);
//     d->stackedLayout->addWidget(d->imageViewer);
//     
//     d->stackedLayoutWidget = new QWidget(this);
//     d->stackedLayoutWidget->setLayout(d->stackedLayout);
//     this->setCentralWidget(d->stackedLayoutWidget);
//     
    
}

ANPV::~ANPV()
{
    d->writeSettings();
}

QFileSystemModel* ANPV::dirModel()
{
    return d->dirModel;
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
    d->currentDir = str;
    if(old != d->currentDir)
    {
        emit this->currentDirChanged(d->currentDir, old);
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
//     d->fileModel->sort(order);
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
    //     d->fileModel->sort(col);
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
    if(old != h)
    {
        d->iconHeight = h;
        emit iconHeightChanged(h, old);
    }
}

/*
void ANPV::showImageView()
{
    d->stackedLayout->setCurrentWidget(d->imageViewer);
}

void ANPV::showThumbnailView()
{
    d->thumbnailViewer->scrollToCurrentImage();
    d->stackedLayout->setCurrentWidget(d->thumbnailViewer);
}

void ANPV::loadImage(QFileInfo inf)
{
    this->setWindowTitle(inf.fileName());
    d->imageViewer->loadImage(inf.absoluteFilePath());
    this->setCurrentDir(inf.absoluteDir().absolutePath());
}

void ANPV::loadImage(QSharedPointer<SmartImageDecoder> dec)
{
    this->setWindowTitle(dec->image()->fileInfo().fileName());
    d->imageViewer->loadImage(dec);
}*/


void ANPV::addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut)
{
    d->mainWindow->addBackgroundTask(group, fut);
}

void ANPV::hideProgressWidget(CancellableProgressWidget* w)
{
    d->mainWindow->hideProgressWidget(w);
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
