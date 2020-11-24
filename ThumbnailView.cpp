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

#include <vector>
#include <algorithm>

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"
#include "ANPV.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"

struct ThumbnailView::Impl
{
    ThumbnailView* p;
    ANPV* anpv;
    
    QFileSystemModel* dirModel;
    
    QFileSystemModel* fileModel;
    QDir currentDir;
    
    QListView* thumbnailList;
    QTreeView* fileSystemTree;
    QDockWidget* fileSystemTreeDockContainer;
    
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
        QFileInfo info = fileModel->fileInfo(idx);
        
        if(info.isDir())
        {
            p->changeDir(info.absoluteFilePath());
        }
        else if(info.isFile())
        {
            anpv->showImageView();
            anpv->loadImage(info.absoluteFilePath());
        }
    }
    
    void onTreeClicked(const QModelIndex& idx)
    {
        QFileInfo info = dirModel->fileInfo(idx);
        p->changeDir(info.absoluteFilePath());
    }
};

ThumbnailView::ThumbnailView(QFileSystemModel* model, ANPV *anpv)
 : QMainWindow(anpv), d(std::make_unique<Impl>(this))
{
    d->anpv = anpv;
    d->dirModel = model;
    
    connect(d->dirModel, &QFileSystemModel::directoryLoaded, this, [&](const QString& s){d->scrollLater(s);});
    
    d->fileModel = new QFileSystemModel(this);
    
    d->thumbnailList = new QListView(this);
    d->thumbnailList->setModel(d->fileModel);
    d->thumbnailList->setViewMode(QListView::IconMode);
    d->thumbnailList->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->thumbnailList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    connect(d->thumbnailList, &QListView::activated, this, [&](const QModelIndex &idx){d->onThumbnailActivated(idx);});
    
    this->setCentralWidget(d->thumbnailList);
    
    d->fileSystemTree = new QTreeView(this);
    d->fileSystemTree->setHeaderHidden(true);
    d->fileSystemTree->setModel(model);
    d->fileSystemTree->showColumn(0);
    d->fileSystemTree->hideColumn(1);
    d->fileSystemTree->hideColumn(2);
    d->fileSystemTree->hideColumn(3);
    d->fileSystemTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->fileSystemTree->setSelectionMode(QAbstractItemView::SingleSelection);
    d->fileSystemTree->setRootIndex(model->index(model->rootPath()));
    
    connect(d->fileSystemTree, &QTreeView::clicked, this,  [&](const QModelIndex &idx){d->onTreeClicked(idx);});
    connect(d->fileSystemTree, &QTreeView::expanded, this, [&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    connect(d->fileSystemTree, &QTreeView::collapsed, this,[&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    
    d->fileSystemTreeDockContainer = new QDockWidget(this);
    d->fileSystemTreeDockContainer->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    d->fileSystemTreeDockContainer->setWidget(d->fileSystemTree);
    
    this->addDockWidget(Qt::LeftDockWidgetArea, d->fileSystemTreeDockContainer, Qt::Vertical);
}

ThumbnailView::~ThumbnailView() = default;

void ThumbnailView::changeDir(const QString& dir)
{
    d->currentDir = dir;
    QModelIndex mo = d->dirModel->index(dir);
    d->fileSystemTree->setExpanded(mo, true);
    d->thumbnailList->setRootIndex(d->fileModel->setRootPath(dir));
}
