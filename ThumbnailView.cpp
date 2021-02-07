#include "ThumbnailView.hpp"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFlags>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QWindow>
#include <QGuiApplication>
#include <QThreadPool>
#include <QDebug>
#include <QListView>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QString>
#include <QTreeView>
#include <QDockWidget>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QWheelEvent>
#include <QFutureWatcher>

#include <vector>
#include <algorithm>
#include <cmath>

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "ANPV.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "SortedImageModel.hpp"
#include "MoveFileCommand.hpp"
#include "ThumbnailImageView.hpp"

struct ThumbnailView::Impl
{
    ThumbnailView* p;
    ANPV* anpv;
    bool isInitialized{false};
    
    QFileSystemModel* dirModel;
    
    SortedImageModel* fileModel;
    QFutureWatcher<DecodingState> changeDirFuture;
    QDir currentDir;
    
    ThumbnailImageView* thumbnailList;
    QTreeView* fileSystemTree;
    QDockWidget* fileSystemTreeDockContainer;
    
    QFileInfo selectedIndexBackup;
    
    QAction* actionCut;
    QAction* actionCopy;
    QAction* actionDelete;
    
    Impl(ThumbnailView* parent) : p(parent)
    {}

    void resizeTreeColumn(const QModelIndex &index)
    {
        fileSystemTree->resizeColumnToContents(0);
        fileSystemTree->scrollTo(index);
    }
    
    void scrollLater(const QString& s)
    {
        if(QDir(s) == currentDir)
        {
            QModelIndex mo = dirModel->index(s);
            fileSystemTree->scrollTo(mo);
        }
    }
    
    void onThumbnailActivated(const QModelIndex& idx)
    {
        QSharedPointer<SmartImageDecoder> dec = fileModel->decoder(idx);
        if(dec)
        {
            anpv->showImageView();
            anpv->loadImage(dec);
        }
        else
        {
            QFileInfo info = fileModel->fileInfo(idx);
            if(info.isDir())
            {
                p->changeDir(info.absoluteFilePath());
            }
            else if(info.isFile())
            {
                anpv->showImageView();
                anpv->loadImage(info);
            }
        }
    }
    
    void onTreeActivated(const QModelIndex& idx)
    {
        QFileInfo info = dirModel->fileInfo(idx);
        p->changeDir(info.absoluteFilePath(), true);
    }

    void onModelAboutToBeReset()
    {
        QModelIndex cur = thumbnailList->currentIndex();
        if(cur.isValid())
        {
            selectedIndexBackup = fileModel->fileInfo(cur);
        }
    }
    
    void onModelReset()
    {
        if(!selectedIndexBackup.filePath().isEmpty())
        {
            QModelIndex newCurrentIndex = fileModel->index(selectedIndexBackup);
            if(newCurrentIndex.isValid())
            {
                thumbnailList->setCurrentIndex(newCurrentIndex);
                thumbnailList->scrollTo(newCurrentIndex, QAbstractItemView::PositionAtCenter);
            }
            selectedIndexBackup = QFileInfo();
        }
    }
    
    enum Operation { Move, Copy, Delete };
    QString lastTargetDirectory;
    void onFileOperation(Operation op)
    {
        QString dirToOpen = lastTargetDirectory.isNull() ? currentDir.absolutePath() : lastTargetDirectory;
        QString dir = QFileDialog::getExistingDirectory(p, "Select Target Directory",
                                                dirToOpen,
                                                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        
        if(dir.isEmpty())
        {
            return;
        }
        if(QDir(dir) == currentDir)
        {
            QMessageBox::information(p, "That doesn't work", "Destination folder cannot be equal with source folder!");
            return;
        }
        
        QList<QString> selectedFileNames;
        QString curDir;
        p->getSelectedFiles(selectedFileNames, curDir);
        
        switch(op)
        {
            case Move:
                anpv->moveFilesSlot(selectedFileNames, currentDir.absolutePath(), dir);
                break;
            case Copy:
            case Delete:
                 QMessageBox::information(p, "Not yet implemented", "not yet impl");
                break;
        }
        
        lastTargetDirectory = dir;
    }
};

ThumbnailView::ThumbnailView(SortedImageModel* model, ANPV *anpv)
 : QMainWindow(anpv), d(std::make_unique<Impl>(this))
{
    d->anpv = anpv;
    d->dirModel = new QFileSystemModel(this);
    d->dirModel->setRootPath("");
    d->dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    
    connect(d->dirModel, &QFileSystemModel::directoryLoaded, this, [&](const QString& s){d->scrollLater(s);});
    
    d->fileModel = model;
    connect(&d->changeDirFuture, &QFutureWatcher<DecodingState>::finished, this,
    [&]()
    {
        QGuiApplication::restoreOverrideCursor();
    });

    d->actionCut = new QAction(QIcon::fromTheme("edit-cut"), "Move to", this);
    d->actionCut->setShortcuts(QKeySequence::Cut);
    connect(d->actionCut, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Move); });
    
    d->actionCopy = new QAction(QIcon::fromTheme("edit-copy"), "Copy to", this);
    d->actionCopy->setShortcuts(QKeySequence::Copy);
    connect(d->actionCopy, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Copy); });
    
    d->actionDelete = new QAction(QIcon::fromTheme("edit-delete"), "Move To Trash", this);
    d->actionDelete->setShortcuts(QKeySequence::Delete);
    connect(d->actionDelete, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Delete); });
    
    d->thumbnailList = new ThumbnailImageView(this);
    d->thumbnailList->setModel(d->fileModel);
    d->thumbnailList->addAction(d->actionCut);
    d->thumbnailList->addAction(d->actionCopy);
    d->thumbnailList->addAction(d->actionDelete);
    connect(d->thumbnailList, &QListView::activated, this, [&](const QModelIndex &idx){d->onThumbnailActivated(idx);});
    
    // connect to model reset signals after setModel()!
    connect(d->fileModel, &SortedImageModel::modelAboutToBeReset, this, [&](){ d->onModelAboutToBeReset(); });
    connect(d->fileModel, &SortedImageModel::modelReset, this, [&](){ d->onModelReset(); });
    
    this->setCentralWidget(d->thumbnailList);
    
    d->fileSystemTree = new QTreeView(this);
    d->fileSystemTree->setHeaderHidden(true);
    d->fileSystemTree->setModel(d->dirModel);
    d->fileSystemTree->showColumn(0);
    d->fileSystemTree->hideColumn(1);
    d->fileSystemTree->hideColumn(2);
    d->fileSystemTree->hideColumn(3);
    d->fileSystemTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->fileSystemTree->setSelectionMode(QAbstractItemView::SingleSelection);
    d->fileSystemTree->setRootIndex(d->dirModel->index(d->dirModel->rootPath()));
    
    connect(d->fileSystemTree, &QTreeView::activated, this,  [&](const QModelIndex &idx){d->onTreeActivated(idx);});
    connect(d->fileSystemTree, &QTreeView::expanded, this, [&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    connect(d->fileSystemTree, &QTreeView::collapsed, this,[&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    
    d->fileSystemTreeDockContainer = new QDockWidget(this);
    d->fileSystemTreeDockContainer->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    d->fileSystemTreeDockContainer->setWidget(d->fileSystemTree);
    
    this->addDockWidget(Qt::LeftDockWidgetArea, d->fileSystemTreeDockContainer, Qt::Vertical);
}

ThumbnailView::~ThumbnailView() = default;

void ThumbnailView::changeDir(const QString& dir, bool skipScrollTo)
{
    if(!d->isInitialized || d->currentDir != dir)
    {
        d->isInitialized = true;
        d->currentDir = dir;
        QModelIndex mo = d->dirModel->index(dir);
        d->fileSystemTree->setCurrentIndex(mo);
        if(!skipScrollTo)
        {
            // vertically scroll to center
            d->fileSystemTree->scrollTo(mo, QAbstractItemView::PositionAtCenter);
            // and make sure we do not scroll to center horizontally
            d->fileSystemTree->scrollTo(mo, QAbstractItemView::EnsureVisible);
        }
        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        auto fut = d->fileModel->changeDirAsync(dir);
        d->changeDirFuture.setFuture(fut);
        d->anpv->addBackgroundTask(fut);
    }
}

void ThumbnailView::selectThumbnail(const QModelIndex& idx)
{
    if(idx.isValid())
    {
        d->thumbnailList->setCurrentIndex(idx);
    }
}

void ThumbnailView::scrollToCurrentImage()
{
    QModelIndex cur = d->thumbnailList->currentIndex();
    if(cur.isValid())
    {
        d->thumbnailList->scrollTo(cur, QAbstractItemView::PositionAtCenter);
    }
}

void ThumbnailView::getSelectedFiles(QList<QString>& selectedFiles, QString& sourceDir)
{
    QModelIndexList selectedIdx = d->thumbnailList->selectionModel()->selectedRows();
    
    for(int i=0; i<selectedIdx.size(); i++)
    {
        QString name = selectedIdx[i].data().toString();
        selectedFiles.append(std::move(name));
    }
    
    sourceDir = d->currentDir.absolutePath();
}

