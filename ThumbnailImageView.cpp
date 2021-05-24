#include "ThumbnailImageView.hpp"

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
#include "ThumbnailView.hpp"

struct ThumbnailImageView::Impl
{
    ANPV* anpv=nullptr;
    ThumbnailView* parentView=nullptr;
    ThumbnailImageView* q=nullptr;
    SortedImageModel* model=nullptr;
    
    QAction* actionCut=nullptr;
    QAction* actionCopy=nullptr;
    QAction* actionDelete=nullptr;
    
    enum Operation { Move, Copy, Delete };
    QString lastTargetDirectory;
    void onFileOperation(Operation op)
    {
        QDir currentDir = parentView->currentDir();
        QString dirToOpen = lastTargetDirectory.isNull() ? currentDir.absolutePath() : lastTargetDirectory;
        QString dir = QFileDialog::getExistingDirectory(q, "Select Target Directory",
                                                dirToOpen,
                                                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        
        if(dir.isEmpty())
        {
            return;
        }
        if(QDir(dir) == currentDir)
        {
            QMessageBox::information(q, "That doesn't work", "Destination folder cannot be equal with source folder!");
            return;
        }
        
        QList<QString> selectedFileNames;
        q->selectedFiles(selectedFileNames);
        
        switch(op)
        {
            case Move:
                anpv->moveFilesSlot(selectedFileNames, currentDir.absolutePath(), dir);
                break;
            case Copy:
            case Delete:
                 QMessageBox::information(q, "Not yet implemented", "not yet impl");
                break;
        }
        
        lastTargetDirectory = dir;
    }
};

ThumbnailImageView::ThumbnailImageView(ANPV *anpv, ThumbnailView *parent)
 : QListView(parent), d(std::make_unique<Impl>())
{
    this->setViewMode(QListView::IconMode);
    this->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setResizeMode(QListView::Adjust);
    this->setWordWrap(true);
    this->setWrapping(true);
    this->setSpacing(2);
    this->setContextMenuPolicy(Qt::ActionsContextMenu);
    
    d->anpv = anpv;
    d->q = this;
    d->parentView = parent;

    d->actionCut = new QAction(QIcon::fromTheme("edit-cut"), "Move to", this);
    d->actionCut->setShortcuts(QKeySequence::Cut);
    connect(d->actionCut, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Move); });
    
    d->actionCopy = new QAction(QIcon::fromTheme("edit-copy"), "Copy to", this);
    d->actionCopy->setShortcuts(QKeySequence::Copy);
    connect(d->actionCopy, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Copy); });
    
    d->actionDelete = new QAction(QIcon::fromTheme("edit-delete"), "Move To Trash", this);
    d->actionDelete->setShortcuts(QKeySequence::Delete);
    connect(d->actionDelete, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Delete); });
    
    this->addAction(d->actionCut);
    this->addAction(d->actionCopy);
    this->addAction(d->actionDelete);
}

ThumbnailImageView::~ThumbnailImageView() = default;

void ThumbnailImageView::setModel(SortedImageModel* model)
{
    d->model = model;
    this->QListView::setModel(model);
}

void ThumbnailImageView::wheelEvent(QWheelEvent *event)
{
    auto angleDelta = event->angleDelta();

    if (event->modifiers() & Qt::ControlModifier)
    {
        double s = d->model->iconHeight();
        // zoom
        if(angleDelta.y() > 0)
        {
            d->model->setIconHeight(static_cast<int>(std::ceil(s * 1.2)));
            event->accept();
            return;
        }
        else if(angleDelta.y() < 0)
        {
            d->model->setIconHeight(static_cast<int>(std::floor(s / 1.2)));
            event->accept();
            return;
        }
    }

    QListView::wheelEvent(event);
}

void ThumbnailImageView::selectedFiles(QList<QString>& files)
{
    QModelIndexList selectedIdx = this->selectionModel()->selectedRows();
    
    for(int i=0; i<selectedIdx.size(); i++)
    {
        QString name = selectedIdx[i].data().toString();
        files.append(std::move(name));
    }
}
