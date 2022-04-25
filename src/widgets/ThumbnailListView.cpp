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
#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include <vector>
#include <algorithm>
#include <cmath>

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "FileOperationConfigDialog.hpp"
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
    QAction* actionMoveTo=nullptr;
    QAction* actionCopyTo=nullptr;
    QAction* actionCopyToFilePath=nullptr;
    QAction* actionMove=nullptr;
    QAction* actionCopy=nullptr;
    QAction* actionDelete=nullptr;
    
    QString lastTargetDirectory;
    void onFileOperation(ANPV::FileOperation op)
    {
        QString dir = ANPV::globalInstance()->getExistingDirectory(q, lastTargetDirectory);
        this->startFileOperation(op, std::move(dir));
    }
    
    void startFileOperation(ANPV::FileOperation op, QString&& dest)
    {
        if(dest.isEmpty())
        {
            return;
        }
        QDir currentDir = ANPV::globalInstance()->currentDir();
        if(QDir(dest) == currentDir)
        {
            QMessageBox::information(q, "That doesn't work", "Destination folder cannot be equal with source folder!");
            return;
        }
        
        QList<Entry_t> imgs = q->selectedImages();
        QList<QString> files;
        for(Entry_t& e : imgs)
        {
            files.push_back(SortedImageModel::image(e)->fileInfo().fileName());
        }
        
        switch(op)
        {
            case ANPV::FileOperation::Move:
                ANPV::globalInstance()->moveFiles(std::move(files), currentDir.absolutePath(), std::move(dest));
                break;
            case ANPV::FileOperation::HardLink:
                ANPV::globalInstance()->hardLinkFiles(std::move(files), currentDir.absolutePath(), std::move(dest));
                break;
            default:
                QMessageBox::information(q, "Not yet implemented", "not yet impl");
                break;
        }
    }
    
    void onCopyToClipboard(ANPV::FileOperation op)
    {
        QList<Entry_t> imgs = q->selectedImages();
        QList<QUrl> files;
        for(Entry_t& e : imgs)
        {
            files.push_back(QUrl::fromLocalFile(SortedImageModel::image(e)->fileInfo().absoluteFilePath()));
        }
        
        QMimeData *mimeData = new QMimeData;
        ANPV::setUrls(mimeData, files);
        ANPV::setClipboardDataCut(mimeData, op == ANPV::FileOperation::Move);
        QGuiApplication::clipboard()->setMimeData(mimeData);
    }

    void openContainingFolder()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ANPV::globalInstance()->currentDir()));
    }
    
    void openSelectionInternally()
    {
        QList<Entry_t> imgs = q->selectedImages();
        if(imgs.size() == 0)
        {
            return;
        }
        
        QFileInfo firstInf = SortedImageModel::image(imgs[0])->fileInfo();
        if(imgs.size() == 1 && firstInf.isDir())
        {
            ANPV::globalInstance()->setCurrentDir(firstInf.absoluteFilePath());
        }
        else
        {
            ANPV::globalInstance()->openImages(imgs);
        }
    }
    
    void openSelectionExternally()
    {
    }
    
    void onCopyFilePath()
    {
        QModelIndex cur = q->currentIndex();
        if(!cur.isValid())
        {
            return;
        }
        
        auto imgs = q->selectedImages();
        QString filePaths;
        for(Entry_t& e : imgs)
        {
            filePaths += QString(" '%1' ").arg(SortedImageModel::image(e)->fileInfo().absoluteFilePath());
        }
        
        QClipboard* cb = QApplication::clipboard();
        Q_ASSERT(cb != nullptr);
        cb->setText(filePaths);
    }
    
    void scrollToCurrentIdx()
    {
        QModelIndex cur = q->selectionModel()->currentIndex();
        if(!cur.isValid() || !q->selectionModel()->isSelected(cur))
        {
            return;
        }
        
        q->scrollTo(cur, QAbstractItemView::PositionAtCenter);
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
    
    d->actionCopyToFilePath = new QAction(QIcon::fromTheme("edit-copy"), "Copy filepath to clipboard", this);
    connect(d->actionCopyToFilePath, &QAction::triggered, this, [&](){ d->onCopyFilePath(); });
    
    d->actionMoveTo = new QAction(QIcon::fromTheme("edit-cut"), "Move to", this);
    connect(d->actionMoveTo, &QAction::triggered, this, [&](){ d->onFileOperation(ANPV::FileOperation::Move); });
    
    d->actionCopyTo = new QAction(QIcon::fromTheme("edit-copy"), "HardLink to", this);
    connect(d->actionCopyTo, &QAction::triggered, this, [&](){ d->onFileOperation(ANPV::FileOperation::HardLink); });
    
    d->actionMove = new QAction(QIcon::fromTheme("edit-cut"), "Cut", this);
    d->actionMove->setShortcuts(QKeySequence::Cut);
    connect(d->actionMove, &QAction::triggered, this, [&](){ d->onCopyToClipboard(ANPV::FileOperation::Move); });
    
    d->actionCopy = new QAction(QIcon::fromTheme("edit-copy"), "HardLink", this);
    d->actionCopy->setShortcuts(QKeySequence::Copy);
    connect(d->actionCopy, &QAction::triggered, this, [&](){ d->onCopyToClipboard(ANPV::FileOperation::HardLink); });
    
    d->actionDelete = new QAction(QIcon::fromTheme("edit-delete"), "Move To Trash", this);
    d->actionDelete->setShortcuts(QKeySequence::Delete);
    connect(d->actionDelete, &QAction::triggered, this, [&](){ d->onFileOperation(ANPV::FileOperation::Delete); });
    
    this->addAction(d->actionOpenSelectionInternally);
    this->addAction(d->actionOpenSelectionExternally);
    this->addAction(d->actionOpenFolder);
    
    QAction* sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    
    this->addAction(d->actionCopyToFilePath);
    
    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    
    this->addAction(d->actionMoveTo);
    this->addAction(d->actionCopyTo);
    
    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionMove);
    this->addAction(d->actionCopy);
    
    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionDelete);
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

// QListView's ExtendedSelection mode is broken in IconMode, see
// https://bugreports.qt.io/browse/QTBUG-94098
void ThumbnailListView::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags flags)
{
    if (state() == DragSelectingState)
    {
        // visual selection mode (rubberband selection)
        QListView::setSelection(rect,flags);
    }
    else
    {
        // logical selection mode (key and mouse click selection)
        QModelIndex firstIndex = this->indexAt(rect.topLeft());
        QModelIndex lastIndex = this->indexAt(rect.bottomRight());
        if(lastIndex < firstIndex)
        {
            std::swap(firstIndex, lastIndex);
        }
        
        QItemSelection selection;
        if (firstIndex.isValid() && lastIndex.isValid())
        {
            selection << QItemSelectionRange(firstIndex, lastIndex);
        }
        
        this->selectionModel()->select(selection, flags);
    }
}

void ThumbnailListView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel* old = this->model();
    if(old)
    {
        old->disconnect(this);
    }
    connect(model, &QAbstractItemModel::layoutChanged, this, [&](const QList<QPersistentModelIndex>, QAbstractItemModel::LayoutChangeHint){ d->scrollToCurrentIdx(); }, Qt::QueuedConnection);
    QListView::setModel(model);
}

QList<Entry_t> ThumbnailListView::selectedImages()
{
    QModelIndexList selectedIdx = this->selectionModel()->selectedRows();
    return this->selectedImages(selectedIdx);
}

QList<Entry_t> ThumbnailListView::selectedImages(const QModelIndexList& selectedIdx)
{
    QList<Entry_t> entries;
    auto sourceModel = ANPV::globalInstance()->fileModel();
    auto& proxyModel = dynamic_cast<QSortFilterProxyModel&>(*this->model());
    for(int i=0; i<selectedIdx.size(); i++)
    {
        entries.push_back(sourceModel->entry(proxyModel.mapToSource(selectedIdx[i])));
    }
    return entries;
}

void ThumbnailListView::fileOperationOnSelectedFiles(QAction* action)
{
    ANPV::FileOperation op = FileOperationConfigDialog::operationFromAction(action);
    QString targetDir = action->data().toString();
    d->startFileOperation(op, std::move(targetDir));
}
