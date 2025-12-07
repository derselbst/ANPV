#include "ThumbnailListView.hpp"

#include <QPointer>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QListView>
#include <QWidget>
#include <QString>
#include <QAction>
#include <QMessageBox>
#include <QDesktopServices>
#include <QSortFilterProxyModel>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMetaEnum>
#include <QScrollBar>
#include <QtGlobal>
#include <QElapsedTimer>

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
#include "WaitCursor.hpp"
#include "ListItemDelegate.hpp"
#include "TraceTimer.hpp"
#include "ProgressIndicatorHelper.hpp"

struct ThumbnailListView::Impl
{
    ThumbnailListView *q = nullptr;

    QAction *actionOpenSelectionInternally = nullptr;
    QAction *actionOpenSelectionExternally = nullptr;
    QAction *actionOpenFolder = nullptr;
    QAction *actionToggle = nullptr;
    QAction *actionCheck = nullptr;
    QAction *actionUncheck = nullptr;
    QAction *actionMoveTo = nullptr;
    QAction *actionCopyTo = nullptr;
    QAction *actionCopyToFilePath = nullptr;
    QAction *actionMove = nullptr;
    QAction *actionCopy = nullptr;
    QAction *actionDelete = nullptr;

    QPointer<ListItemDelegate> itemDelegate;

    QString lastTargetDirectory;
    void onFileOperation(ANPV::FileOperation op)
    {
        QString dir;

        if(op != ANPV::FileOperation::Delete)
        {
            dir = ANPV::globalInstance()->getExistingDirectory(q, lastTargetDirectory);

            if(dir.isEmpty())
            {
                // user canceled dialog
                return;
            }
        }

        this->startFileOperation(op, std::move(dir));
    }

    void startFileOperation(ANPV::FileOperation op, QString &&dest)
    {
        QDir currentDir = ANPV::globalInstance()->currentDir();

        if(op != ANPV::FileOperation::Delete && QDir(dest) == currentDir)
        {
            QMessageBox::information(q, "That doesn't work", "Destination folder cannot be equal with source folder!");
            return;
        }

        auto imgs = q->checkedImages();

        if(imgs.isEmpty())
        {
            const char *operation = QMetaEnum::fromType<ANPV::FileOperation>().key(op);
            QMessageBox::information(q, QString("Unable to %1").arg(operation), "Pls. select one or more files by checking the box located top-left of the file icon.", QMessageBox::Ok);
            return;
        }

        WaitCursor w;
        QList<QString> files;
        files.reserve(imgs.size());

        for(auto &e : imgs)
        {
            files.push_back(e->fileInfo().fileName());
        }

        switch(op)
        {
        case ANPV::FileOperation::Move:
            ANPV::globalInstance()->moveFiles(std::move(files), currentDir.absolutePath(), std::move(dest));
            break;

        case ANPV::FileOperation::HardLink:
            ANPV::globalInstance()->hardLinkFiles(std::move(files), currentDir.absolutePath(), std::move(dest));
            break;

        case ANPV::FileOperation::Delete:
            ANPV::globalInstance()->deleteFiles(std::move(files), currentDir.absolutePath());
            break;

        default:
            QMessageBox::information(q, "Not yet implemented", "not yet impl");
            break;
        }

        // FIXME: only uncheck those images, which have been processed successfully
        for(auto &e : imgs)
        {
            e->setChecked(Qt::Unchecked);
        }
    }

    void onCopyToClipboard(ANPV::FileOperation op)
    {
        if(!(op == ANPV::FileOperation::Move || op == ANPV::FileOperation::Copy))
        {
            throw std::logic_error("Unsupported mode when copying to clipboard");
        }

        QList<QSharedPointer<Image>> imgs = q->selectedImages();
        QList<QUrl> files;

        for(QSharedPointer<Image> &e : imgs)
        {
            files.push_back(QUrl::fromLocalFile(e->fileInfo().absoluteFilePath()));
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
        QList<QSharedPointer<Image>> imgs = q->selectedImages();

        if(imgs.size() == 0)
        {
            return;
        }

        QFileInfo firstInf = imgs[0]->fileInfo();

        if(imgs.size() == 1 && firstInf.isDir())
        {
            ANPV::globalInstance()->setCurrentDir(firstInf.absoluteFilePath());
        }
        else
        {
            QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> imgsWithModel;

            for(const auto &i : imgs)
            {
                if (i->fileInfo().isFile())
                {
                    imgsWithModel.push_back({ i, ANPV::globalInstance()->fileModel()->dataContainer() });
                }
            }

            ANPV::globalInstance()->openImages(imgsWithModel);
        }
    }

    void openSelectionExternally()
    {
        QList<QSharedPointer<Image>> imgs = q->selectedImages();

        if (imgs.size() == 0)
        {
            return;
        }

        if (imgs.size() == 1)
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(imgs[0]->fileInfo().absoluteFilePath()));
        }
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

        for(QSharedPointer<Image> &e : imgs)
        {
            filePaths += QString(" '%1' ").arg(e->fileInfo().absoluteFilePath());
        }

        QClipboard *cb = QApplication::clipboard();
        Q_ASSERT(cb != nullptr);
        cb->setText(filePaths);
    }

    static Qt::CheckState toggleCheckState(Qt::CheckState state, const Qt::ItemFlags &flags)
    {
        if(flags & Qt::ItemIsUserTristate)
        {
            state = ((Qt::CheckState)((state + 1) % 3));
        }
        else
        {
            state = (state == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
        }

        return state;
    }

    static Qt::CheckState checkCheckState(Qt::CheckState state, const Qt::ItemFlags &flags)
    {
        return Qt::Checked;
    }

    static Qt::CheckState uncheckCheckState(Qt::CheckState state, const Qt::ItemFlags &flags)
    {
        return Qt::Unchecked;
    }

    void checkSelectedImages(Qt::CheckState(*getNewState)(Qt::CheckState state, const Qt::ItemFlags &flags))
    {
        WaitCursor w;
        QItemSelectionModel *selMod = q->selectionModel();
        QModelIndexList selInd = selMod->selectedRows();
        bool ok = false;

        if(selMod && !selInd.isEmpty())
        {
            Qt::CheckState firstCheckState = static_cast<Qt::CheckState>(selInd[0].data(Qt::CheckStateRole).toInt());

            for(auto &i : selInd)
            {
                // borrowed from QStyledItemDelegate::editorEvent()
                QVariant value = i.data(Qt::CheckStateRole);
                Qt::ItemFlags flags = i.flags();

                if(!(flags & Qt::ItemIsUserCheckable) || !(flags & Qt::ItemIsEnabled) || !value.isValid())
                {
                    continue;
                }

                Qt::CheckState state = getNewState(firstCheckState, flags);
                ok |= q->model()->setData(i, state, Qt::CheckStateRole);
            }
        }
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
    this->setSpacing(5);
    this->setContextMenuPolicy(Qt::ActionsContextMenu);
    // always show scroll bars to prevent flickering, cause by an event loop that turns it on and off
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    d->q = this;
    d->itemDelegate = new ListItemDelegate(this);
    this->setItemDelegate(d->itemDelegate);

    connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, this,
            [&](ViewFlags_t v, ViewFlags_t)
    {
        // Item flags of the model have changed, but there is no itemFlagsChanged event.
        // Reschedule a paint event for the view to make disabled items become gray.
        // And since the QListView is inside a scrollarea, we cannot simply call this->repaint()!
        this->viewport()->repaint();
    });

    connect(this, &QListView::activated, this, [&](const QModelIndex &)
    {
        d->openSelectionInternally();
    });

    d->actionOpenSelectionInternally = new QAction("Open", this);
    connect(d->actionOpenSelectionInternally, &QAction::triggered, this, [&]()
    {
        d->openSelectionInternally();
    });

    d->actionOpenSelectionExternally = new QAction("Open with default app", this);
    connect(d->actionOpenSelectionExternally, &QAction::triggered, this, [&]()
    {
        d->openSelectionExternally();
    });

    d->actionOpenFolder = new QAction(QIcon::fromTheme("system-file-manager"), "Open containing folder", this);
    connect(d->actionOpenFolder, &QAction::triggered, this, [&]()
    {
        d->openContainingFolder();
    });

    d->actionToggle = new QAction("Toggle selected files", this);
    d->actionToggle->setShortcut(Qt::Key_Space);
    d->actionToggle->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(d->actionToggle, &QAction::triggered, this, [&]()
    {
        d->checkSelectedImages(d->toggleCheckState);
    });

    d->actionCheck = new QAction("Check selected files", this);
    d->actionCheck->setShortcut(Qt::Key_Insert);
    d->actionCheck->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(d->actionCheck, &QAction::triggered, this, [&]()
    {
        d->checkSelectedImages(d->checkCheckState);
    });

    d->actionUncheck = new QAction("Uncheck selected files", this);
    d->actionUncheck->setShortcut(Qt::SHIFT | Qt::Key_Insert);
    d->actionUncheck->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(d->actionUncheck, &QAction::triggered, this, [&]()
    {
        d->checkSelectedImages(d->uncheckCheckState);
    });

    d->actionCopyToFilePath = new QAction(QIcon::fromTheme("edit-copy"), "Copy absolute path of selected files to clipboard", this);
    connect(d->actionCopyToFilePath, &QAction::triggered, this, [&]()
    {
        d->onCopyFilePath();
    });

    d->actionMove = new QAction(QIcon::fromTheme("edit-cut"), "Cut selected files to clipboard", this);
    d->actionMove->setShortcut(QKeySequence::Cut);
    d->actionMove->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(d->actionMove, &QAction::triggered, this, [&]()
    {
        d->onCopyToClipboard(ANPV::FileOperation::Move);
    });

    d->actionCopy = new QAction(QIcon::fromTheme("edit-copy"), "Copy selected files to clipboard", this);
    d->actionCopy->setShortcut(QKeySequence::Copy);
    d->actionCopy->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(d->actionCopy, &QAction::triggered, this, [&]()
    {
        d->onCopyToClipboard(ANPV::FileOperation::Copy);
    });

    d->actionMoveTo = new QAction(QIcon::fromTheme("edit-cut"), "Move checked files to", this);
    connect(d->actionMoveTo, &QAction::triggered, this, [&]()
    {
        d->onFileOperation(ANPV::FileOperation::Move);
    });

    d->actionCopyTo = new QAction(QIcon::fromTheme("edit-copy"), "HardLink checked files to", this);
    connect(d->actionCopyTo, &QAction::triggered, this, [&]()
    {
        d->onFileOperation(ANPV::FileOperation::HardLink);
    });

    this->addAction(d->actionOpenSelectionInternally);
    this->addAction(d->actionOpenSelectionExternally);
    this->addAction(d->actionOpenFolder);

    QAction *sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionCopyToFilePath);
    this->addAction(d->actionMove);
    this->addAction(d->actionCopy);

    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionToggle);
    this->addAction(d->actionCheck);
    this->addAction(d->actionUncheck);

    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionMoveTo);
    this->addAction(d->actionCopyTo);

    sep = new QAction(this);
    sep->setSeparator(true);
    this->addAction(sep);
    this->addAction(d->actionDelete);
}

ThumbnailListView::~ThumbnailListView() = default;

QModelIndex ThumbnailListView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    auto m = this->model();

    auto findAvailableRowForward = [&](int row)
    {
        int rowCount = m->rowCount();

        if(!rowCount)
        {
            return -1;
        }

        while(row < rowCount && !(m->flags(m->index(row, 0)) & Qt::ItemIsEnabled))
        {
            ++row;
        }

        if(row >= rowCount)
        {
            return -1;
        }

        return row;
    };

    // Make sure that pressing POS1 causes selection of the first active element, so that selection and scrolling actually works
    if(cursorAction == MoveHome)
    {
        int firstAvailRow = findAvailableRowForward(0);

        if(firstAvailRow >= 0)
        {
            // also scroll to the very top to ensure that the topmost section element is shown
            this->scrollToTop();
            return m->index(firstAvailRow, 0, QModelIndex());
        }
    }

    return QListView::moveCursor(cursorAction, modifiers);
}

/* Changes the visible size of the item delegate for the section items. */
void ThumbnailListView::resizeEvent(QResizeEvent *event)
{
    QSize sizeWithoutScrollbar = event->size();
    auto *vsb = this->verticalScrollBar();

    if(vsb != nullptr && vsb->isVisible())
    {
        sizeWithoutScrollbar.rwidth() -= vsb->width();
    }

    d->itemDelegate->resizeSectionSize(sizeWithoutScrollbar);
    QListView::resizeEvent(event);
}

void ThumbnailListView::wheelEvent(QWheelEvent *event)
{
    auto angleDelta = event->angleDelta();

    if(event->modifiers() & Qt::ControlModifier)
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
    if(state() == DragSelectingState)
    {
        // visual selection mode (rubberband selection)
        QListView::setSelection(rect, flags);
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

        if(firstIndex.isValid() && lastIndex.isValid())
        {
            selection << QItemSelectionRange(firstIndex, lastIndex);
        }

        this->selectionModel()->select(selection, flags);
    }
}

void ThumbnailListView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel *old = this->model();

    if(old)
    {
        QObject::disconnect(model, nullptr, this, nullptr);

        auto* pm = dynamic_cast<QSortFilterProxyModel*>(model);
        if (pm)
        {
            auto* sm = dynamic_cast<SortedImageModel*>(pm->sourceModel());
            if (sm)
            {
                QObject::disconnect(sm, nullptr, this, nullptr);
            }
        }
    }

    auto* sm = dynamic_cast<SortedImageModel*>(model);
    if (model && !sm)
    {
        auto* pm = dynamic_cast<QSortFilterProxyModel*>(model);
        if (pm)
        {
            sm = dynamic_cast<SortedImageModel*>(pm->sourceModel());
        }
    }

    if (sm)
    {
        QObject::connect(sm, &SortedImageModel::backgroundProcessingStarted, this, [&]()
            {
                auto* a = ANPV::globalInstance()->spinningIconHelper();
                QObject::connect(a, &ProgressIndicatorHelper::needsRepaint, this->viewport(), qOverload<>(&QWidget::update));
            });

        QObject::connect(sm, &SortedImageModel::backgroundProcessingStopped, this, [&]()
            {
                auto* a = ANPV::globalInstance()->spinningIconHelper();
                QObject::disconnect(a, &ProgressIndicatorHelper::needsRepaint, this->viewport(), qOverload<>(&QWidget::update));
            });
    }

    QListView::setModel(model);
}

QList<Image *> ThumbnailListView::checkedImages()
{
    auto sourceModel = ANPV::globalInstance()->fileModel();
    return sourceModel->checkedEntries();
}

QList<QSharedPointer<Image>> ThumbnailListView::selectedImages()
{
    QModelIndexList selectedIdx = this->selectionModel()->selectedRows();
    return this->selectedImages(selectedIdx);
}

QList<QSharedPointer<Image>> ThumbnailListView::selectedImages(const QModelIndexList &selectedIdx)
{
    QList<QSharedPointer<Image>> entries;
    auto sourceModel = ANPV::globalInstance()->fileModel();
    auto &proxyModel = dynamic_cast<QSortFilterProxyModel &>(*this->model());

    for(int i = 0; i < selectedIdx.size(); i++)
    {
        auto img = AbstractListItem::imageCast(sourceModel->item(proxyModel.mapToSource(selectedIdx[i])));

        if(img != nullptr)
        {
            entries.push_back(img);
        }
    }

    return entries;
}

void ThumbnailListView::fileOperationOnSelectedFiles(QAction *action)
{
    ANPV::FileOperation op = FileOperationConfigDialog::operationFromAction(action);
    QString targetDir = action->data().toString();
    d->startFileOperation(op, std::move(targetDir));
}

void ThumbnailListView::rowsInserted(const QModelIndex &parent, int start, int end)
{
    // reimplement this to avoid flickering when inserting items, caused by clearing of QIconModeViewBase's internal "tree" object, caused by:
    // https://github.com/qt/qtbase/blob/56aa065ab57d55cc832c45b8c260153447c57188/src/widgets/itemviews/qlistview.cpp#L714-L715

    // We should call this->QListView::doItemsLayout(); here to reflect the new item in the view. Instead, this is done in the model via the updateLayout timer.
    QAbstractItemView::rowsInserted(parent, start, end);
}

void ThumbnailListView::doItemsLayout()
{
    WaitCursor w;
    QElapsedTimer e;
    e.start();
    this->QListView::doItemsLayout();
    auto t = e.elapsed();
    auto m = ANPV::globalInstance()->fileModel();

    if(m != nullptr)
    {
        m->setLayoutTimerInterval(t * 3);
    }
}
