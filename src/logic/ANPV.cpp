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
    
    QUndoStack* undoStack;
    std::map<ProgressGroup, QPointer<CancellableProgressWidget>> progressWidgetGroupMap;
    QVBoxLayout* progressWidgetLayout;
    QWidget* progressWidgetContainer;
    QStackedLayout* stackedLayout;
    QWidget *stackedLayoutWidget;
    DocumentView* imageViewer;
    ThumbnailView* thumbnailViewer;
    
    QScopedPointer<MainWindow, QScopedPointerDeleteLater> mainWindow;
    SortedImageModel* fileModel;
    
    QDir currentDir;
    ViewMode viewMode = ViewMode::Unknown;
    Qt::SortOrder sortOrder = static_cast<Qt::SortOrder>(-1);
    SortedImageModel::Column primarySortColumn = SortedImageModel::Column::Unknown;
    
    QAction *actionUndo;
    QAction *actionRedo;
    QAction *actionFileOperationConfigDialog;
    QAction *actionExit;
    
    Impl(ANPV* parent) : q(parent)
    {
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

    if(::global.isNull())
    {
        ::global = QPointer<ANPV>(this);
    }
    
    splash->showMessage("Reading latest settings");
    
    splash->showMessage("Creating logic");
//     d->fileModel = new SortedImageModel(this);
    
    splash->showMessage("Creating UI Widgets");
    d->mainWindow.reset(new MainWindow(splash));
//     d->mainWindow->setModel(d->fileModel);
    
    
    d->mainWindow->show();
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

ANPV::~ANPV() = default;

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
