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
    QDir currentDir;
    
    ThumbnailImageView* thumbnailList;
    QTreeView* fileSystemTree;
    QDockWidget* fileSystemTreeDockContainer;
    
    QFileInfo selectedIndexBackup;
    
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

    d->thumbnailList = new ThumbnailImageView(anpv, this);
    d->thumbnailList->setModel(d->fileModel);
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
        auto fut = d->fileModel->changeDirAsync(dir);
        d->anpv->addBackgroundTask(ProgressGroup::Directory, fut);
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

void ThumbnailView::selectedFiles(QList<QString>& files)
{
    d->thumbnailList->selectedFiles(files);
}

QDir ThumbnailView::currentDir()
{
    return d->currentDir;
}
