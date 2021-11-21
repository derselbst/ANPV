#include "ThumbnailListView.hpp"

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
#include <QWidget>
#include <QFileSystemModel>
#include <QString>
#include <QTreeView>
#include <QDockWidget>
#include <QAction>
#include <QMessageBox>
#include <QWheelEvent>
#include <QDesktopServices>
#include <QSortFilterProxyModel>

#include <vector>
#include <algorithm>
#include <cmath>

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "ANPV.hpp"
#include "Image.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "SortedImageModel.hpp"
#include "MoveFileCommand.hpp"

struct ThumbnailListView::Impl
{
    ThumbnailListView* q=nullptr;
    
    QAction *actionOpenSelectionInternally=nullptr;
    QAction *actionOpenSelectionExternally=nullptr;
    QAction* actionOpenFolder=nullptr;
    QAction* actionCut=nullptr;
    QAction* actionCopy=nullptr;
    QAction* actionDelete=nullptr;
    
    enum Operation { Move, Copy, Delete };
    QString lastTargetDirectory;
    void onFileOperation(Operation op)
    {
        QString dir = ANPV::globalInstance()->getExistingDirectory(q, lastTargetDirectory);
        this->startFileOperation(op, std::move(dir));
    }
    
    void startFileOperation(Operation op, QString&& dest)
    {
        QDir currentDir = ANPV::globalInstance()->currentDir();
        if(dest.isEmpty())
        {
            return;
        }
        if(QDir(dest) == currentDir)
        {
            QMessageBox::information(q, "That doesn't work", "Destination folder cannot be equal with source folder!");
            return;
        }
        
        QList<QSharedPointer<Image>> imgs = this->selectedImages();
        QList<QString> files;
        for(QSharedPointer<Image>& i : imgs)
        {
            files.push_back(i->fileInfo().fileName());
        }
        
        switch(op)
        {
            case Move:
                ANPV::globalInstance()->moveFiles(std::move(files), currentDir.absolutePath(), std::move(dest));
                break;
            case Copy:
            case Delete:
                QMessageBox::information(q, "Not yet implemented", "not yet impl");
                break;
        }
        
        lastTargetDirectory = dest;
    }

    void openContainingFolder()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ANPV::globalInstance()->currentDir().absolutePath()));
    }
    
    void openSelectionInternally()
    {
        QList<QSharedPointer<Image>> imgs = this->selectedImages();
        
        if(imgs.size() == 1 && imgs[0]->fileInfo().isDir())
        {
            ANPV::globalInstance()->setCurrentDir(imgs[0]->fileInfo().absoluteFilePath());
        }
        else
        {
            ANPV::globalInstance()->openImages(imgs);
        }
    }
    
    void openSelectionExternally()
    {
    }
    
    QList<QSharedPointer<Image>> selectedImages()
    {
        QList<QSharedPointer<Image>> images;
        QModelIndexList selectedIdx = q->selectionModel()->selectedRows();
        
        auto sourceModel = ANPV::globalInstance()->fileModel();
        auto& proxyModel = dynamic_cast<QSortFilterProxyModel&>(*q->model());
        for(int i=0; i<selectedIdx.size(); i++)
        {
            images.push_back(sourceModel->data(proxyModel.mapToSource(selectedIdx[i])));
        }
        return images;
    }
};

ThumbnailListView::ThumbnailListView(QWidget *parent)
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
    
    d->q = this;

    connect(this, &QListView::activated, this, [&](const QModelIndex &){ d->openSelectionInternally(); });

    d->actionOpenSelectionInternally = new QAction("Open", this);
    connect(d->actionOpenSelectionInternally, &QAction::triggered, this, [&](){ d->openSelectionInternally(); });
    
    d->actionOpenSelectionExternally = new QAction("Open with", this);
    connect(d->actionOpenSelectionExternally, &QAction::triggered, this, [&](){ d->openSelectionExternally(); });
    
    d->actionOpenFolder = new QAction(QIcon::fromTheme("system-file-manager"), "Open containing folder", this);
    connect(d->actionOpenFolder, &QAction::triggered, this, [&](){ d->openContainingFolder(); });
    
    d->actionCut = new QAction(QIcon::fromTheme("edit-cut"), "Move to", this);
    d->actionCut->setShortcuts(QKeySequence::Cut);
    connect(d->actionCut, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Move); });
    
    d->actionCopy = new QAction(QIcon::fromTheme("edit-copy"), "Copy to", this);
    d->actionCopy->setShortcuts(QKeySequence::Copy);
    connect(d->actionCopy, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Copy); });
    
    d->actionDelete = new QAction(QIcon::fromTheme("edit-delete"), "Move To Trash", this);
    d->actionDelete->setShortcuts(QKeySequence::Delete);
    connect(d->actionDelete, &QAction::triggered, this, [&](){ d->onFileOperation(Impl::Operation::Delete); });
    
    
    QAction* sep = new QAction(this);
    sep->setSeparator(true);
    
    this->addAction(d->actionOpenSelectionInternally);
    this->addAction(d->actionOpenSelectionExternally);
    this->addAction(d->actionOpenFolder);
    this->addAction(sep);
    this->addAction(d->actionCut);
    this->addAction(d->actionCopy);
    this->addAction(d->actionDelete);
    this->addAction(sep);
}

ThumbnailListView::~ThumbnailListView() = default;

void ThumbnailListView::wheelEvent(QWheelEvent *event)
{
    auto angleDelta = event->angleDelta();

    if (event->modifiers() & Qt::ControlModifier)
    {
        constexpr int Step = 50;
        double s = ANPV::globalInstance()->iconHeight();
        // zoom
        if(angleDelta.y() > 0)
        {
            ANPV::globalInstance()->setIconHeight(static_cast<int>(std::ceil(s + Step)));
            event->accept();
            return;
        }
        else if(angleDelta.y() < 0)
        {
            ANPV::globalInstance()->setIconHeight(static_cast<int>(std::floor(s - Step)));
            event->accept();
            return;
        }
    }

    QListView::wheelEvent(event);
}

void ThumbnailListView::moveSelectedFiles(QString&& destination)
{
    d->startFileOperation(Impl::Operation::Move, std::move(destination));
}
