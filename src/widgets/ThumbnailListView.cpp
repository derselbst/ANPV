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
#include <QFileDialog>
#include <QMessageBox>
#include <QWheelEvent>
#include <QDesktopServices>

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

struct ThumbnailListView::Impl
{
    ThumbnailListView* q=nullptr;
    SortedImageModel* model=nullptr;
    
    QAction *actionOpenSelectionInternally;
    QAction *actionOpenSelectionExternally;
    QAction* actionOpenFolder=nullptr;
    QAction* actionCut=nullptr;
    QAction* actionCopy=nullptr;
    QAction* actionDelete=nullptr;
    
    enum Operation { Move, Copy, Delete };
    QString lastTargetDirectory;
    void onFileOperation(Operation op)
    {
        QDir currentDir = ANPV::globalInstance()->currentDir();
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
                ANPV::globalInstance()->moveFilesSlot(selectedFileNames, currentDir.absolutePath(), dir);
                break;
            case Copy:
            case Delete:
                 QMessageBox::information(q, "Not yet implemented", "not yet impl");
                break;
        }
        
        lastTargetDirectory = dir;
    }

    void openContainingFolder()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ANPV::globalInstance()->currentDir().absolutePath()));
    }
    
    void openSelectionInternally()
    {
        ANPV::globalInstance()->openImages(this->selectedImages());
    }
    
    void openSelectionExternally()
    {
    }
    
    QList<QSharedPointer<Image>> selectedImages()
    {
        QList<QSharedPointer<Image>> images;
        QModelIndexList selectedIdx = q->selectionModel()->selectedRows();
        
        for(int i=0; i<selectedIdx.size(); i++)
        {
            images.push_back(this->model->data(selectedIdx[i]));
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
}

ThumbnailListView::~ThumbnailListView() = default;

void ThumbnailListView::setModel(SortedImageModel* model)
{
    d->model = model;
    this->QListView::setModel(model);
}

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

void ThumbnailListView::selectedFiles(QList<QString>& files)
{
    QModelIndexList selectedIdx = this->selectionModel()->selectedRows();
    
    for(int i=0; i<selectedIdx.size(); i++)
    {
        QString name = selectedIdx[i].data().toString();
        files.append(std::move(name));
    }
}

void ThumbnailListView::setCurrentIndex(const QSharedPointer<Image>& img)
{
    QModelIndex wantedIdx = d->model->index(img);
    if(!wantedIdx.isValid())
    {
        return;
    }
    
    this->selectionModel()->setCurrentIndex(wantedIdx, QItemSelectionModel::NoUpdate);
    this->scrollTo(wantedIdx, QAbstractItemView::PositionAtCenter);
}
