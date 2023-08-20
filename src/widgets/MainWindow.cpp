#include "MainWindow.hpp"

#include <QWidget>
#include <QSplashScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemModel>
#include <QActionGroup>
#include <QAction>
#include <QSettings>
#include <QUndoStack>
#include <QToolTip>
#include <QSortFilterProxyModel>
#include <QCloseEvent>
#include <QWhatsThis>
#include <QMessageBox>

#include "DocumentView.hpp"
#include "PreviewAllImagesDialog.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "WaitCursor.hpp"
#include "xThreadGuard.hpp"
#include "ImageSectionDataContainer.hpp"


#include "ui_MainWindow.h"

#ifndef NDEBUG
#ifdef Q_OS_LINUX
#include "sanitizer/asan_interface.h"
#endif
#endif

struct MainWindow::Impl
{
    MainWindow* q = nullptr;
    std::unique_ptr<Ui::MainWindow> ui = std::make_unique<Ui::MainWindow>();
    
    QSortFilterProxyModel* proxyModel = nullptr;
    
    CancellableProgressWidget* cancellableWidget = nullptr;

    QActionGroup* actionGroupSectionSortField = nullptr;
    QActionGroup* actionGroupSectionSortOrder = nullptr;

    QActionGroup* actionGroupImageSortField = nullptr;
    QActionGroup* actionGroupImageSortOrder = nullptr;

    QAction *actionUndo = nullptr;
    QAction *actionRedo = nullptr;
    QAction *actionFileOperationConfigDialog = nullptr;
    QAction *actionExit = nullptr;

    QPointer<QAction> actionFilterSearch;
    QPointer<QAction> actionFilterReset;

    QPointer<QAction> actionBack = nullptr;
    QPointer<QAction> actionForward = nullptr;
    
    Impl(MainWindow* parent) : q(parent)
    {
    }
    
    void addSlowHint(QAction* action)
    {
        static const char tip[] = "This option requires to read EXIF metadata from the file. Therefore, performance greatly suffers when accessing directories that contain many files.";
        action->setToolTip(tip);
        action->setStatusTip(tip);
    }
    
    void createViewActions()
    {
        QActionGroup* viewMode = ANPV::globalInstance()->viewModeActionGroup();
        QActionGroup* viewFlag = ANPV::globalInstance()->viewFlagActionGroup();
        this->ui->menuView->insertSeparator(this->ui->menuView->actions().at(0));
        this->ui->menuView->insertActions(this->ui->menuView->actions().at(0), viewMode->actions());
        this->ui->menuView->insertSeparator(this->ui->menuView->actions().at(0));
        this->ui->menuView->insertActions(this->ui->menuView->actions().at(0), viewFlag->actions());

        this->actionFilterSearch = new QAction("Search", q);
        this->actionFilterSearch->setShortcuts({ Qt::Key_Enter, Qt::Key_Return });
        this->actionFilterSearch->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(this->actionFilterSearch, &QAction::triggered, this->ui->searchButton, &QAbstractButton::click);

        this->actionFilterReset = new QAction("Reset", q);
        this->actionFilterReset->setShortcut(Qt::Key_Escape);
        this->actionFilterReset->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(this->actionFilterReset, &QAction::triggered, this->ui->resetButton, &QAbstractButton::click);

        this->ui->filterGroupBox->addAction(this->actionFilterSearch);
        this->ui->filterGroupBox->addAction(this->actionFilterReset);

        connect(ui->actionReload, &QAction::triggered, q,
            [&](bool)
            {
                ANPV::globalInstance()->setCurrentDir(ANPV::globalInstance()->currentDir(), true);
            }
        );
        connect(ui->actionPreview_all_images, &QAction::triggered, q,
            [&](bool)
            {
                PreviewAllImagesDialog d;
                d.setImageHeight(ANPV::globalInstance()->iconHeight());
                if (d.exec() == QDialog::Accepted)
                {
                    auto imgHeight = d.imageHeight();
                    auto model = ANPV::globalInstance()->fileModel();
                    QMetaObject::invokeMethod(ANPV::globalInstance()->fileModel().get(), [=]()
                        {
                            model->cancelAllBackgroundTasks();
                            model->decodeAllImages(DecodingState::PreviewImage, imgHeight);
                        });
                }
            }
        );
    }
    
    void createDebugActions()
    {
#ifndef NDEBUG
#ifdef Q_OS_LINUX
        QAction* action;
        action = new QAction("Asan Profile Memory Usage", q);
        connect(action, &QAction::triggered, q,
            [&](bool)
            {
                __sanitizer_print_memory_profile(90, 10);
            }
        );
            
        QMenu* debugMenu = ui->menuFile->addMenu("Debug");
        debugMenu->addAction(action);
#endif
#endif
    }
    
    void createSortActions()
    {
        QAction* action;

        auto makeOrderAction = [&](QActionGroup* actionGroup, QString&& name, Qt::SortOrder order)
        {
            QAction* action = new QAction(std::move(name), q);
            action->setCheckable(true);
            action->setData((int)order);
            actionGroup->addAction(action);
        };


        actionGroupImageSortOrder = new QActionGroup(q);
        
        action = new QAction("Image Sort Order", q);
        action->setSeparator(true);
        actionGroupImageSortOrder->addAction(action);

        makeOrderAction(actionGroupImageSortOrder, "Ascending (small to big)", Qt::AscendingOrder);
        makeOrderAction(actionGroupImageSortOrder, "Descending (big to small)", Qt::DescendingOrder);

        connect(actionGroupImageSortOrder, &QActionGroup::triggered, q, [&](QAction* a) { ANPV::globalInstance()->setImageSortOrder((Qt::SortOrder)a->data().toInt()); });
        connect(ANPV::globalInstance(), &ANPV::imageSortOrderChanged, action,
            [=](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                for (QAction* a : this->actionGroupImageSortOrder->actions())
                {
                    if (!a->isSeparator() && newOrder == (Qt::SortOrder)a->data().toInt())
                    {
                        a->trigger();
                        break;
                    }
                }
            });


        actionGroupSectionSortOrder = new QActionGroup(q);

        action = new QAction("Section Sort Order", q);
        action->setSeparator(true);
        actionGroupSectionSortOrder->addAction(action);

        makeOrderAction(actionGroupSectionSortOrder, "Ascending (small to big)", Qt::AscendingOrder);
        makeOrderAction(actionGroupSectionSortOrder, "Descending (big to small)", Qt::DescendingOrder);

        connect(actionGroupSectionSortOrder, &QActionGroup::triggered, q, [&](QAction* a) { ANPV::globalInstance()->setSectionSortOrder((Qt::SortOrder)a->data().toInt()); });
        connect(ANPV::globalInstance(), &ANPV::sectionSortOrderChanged, action,
            [=](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                for (QAction* a : this->actionGroupSectionSortOrder->actions())
                {
                    if (!a->isSeparator() && newOrder == (Qt::SortOrder)a->data().toInt())
                    {
                        a->trigger();
                        break;
                    }
                }
            });
        

        actionGroupImageSortField = new QActionGroup(q);
        
        action = new QAction("Sort images according to", q);
        action->setSeparator(true);
        actionGroupImageSortField->addAction(action);
        
        auto makeSortAction = [&](QActionGroup* actionGroup, QString&& name, SortField col)
        {
            bool isSlow = ImageSectionDataContainer::sortedColumnNeedsPreloadingMetadata(col, col);
            if (isSlow)
            {
                name += " (slow)";
            }
            QAction* action = new QAction(std::move(name), q);
            action->setCheckable(true);
            action->setData((int)col);
            if(isSlow)
            {
                addSlowHint(action);
            }
            actionGroup->addAction(action);
        };
        
        makeSortAction(actionGroupImageSortField, "File Name",            SortField::FileName);
        makeSortAction(actionGroupImageSortField, "File Size",            SortField::FileSize);
        makeSortAction(actionGroupImageSortField, "File Extension",       SortField::FileType);
        makeSortAction(actionGroupImageSortField, "Modified Date",        SortField::DateModified);
        makeSortAction(actionGroupImageSortField, "Image Resolution",     SortField::Resolution);
        makeSortAction(actionGroupImageSortField, "Original Record Date", SortField::DateRecorded);
        makeSortAction(actionGroupImageSortField, "Aperture",             SortField::Aperture);
        makeSortAction(actionGroupImageSortField, "Exposure",             SortField::Exposure);
        makeSortAction(actionGroupImageSortField, "ISO",                  SortField::Iso);
        makeSortAction(actionGroupImageSortField, "Camera Model",         SortField::CameraModel);
        makeSortAction(actionGroupImageSortField, "Focal Length",         SortField::FocalLength);
        makeSortAction(actionGroupImageSortField, "Lens Model",           SortField::Lens);

        connect(actionGroupImageSortField, &QActionGroup::triggered, q, [](QAction* act) { ANPV::globalInstance()->setImageSortField((SortField)act->data().toInt()); });
        connect(ANPV::globalInstance(), &ANPV::imageSortOrderChanged, actionGroupImageSortField,
            [&](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                for (QAction* a : this->actionGroupImageSortField->actions())
                {
                    if (!a->isSeparator() && newField == (SortField)a->data().toInt())
                    {
                        a->trigger();
                        break;
                    }
                }
            });

        actionGroupSectionSortField = new QActionGroup(q);

        action = new QAction("Sectioning by", q);
        action->setSeparator(true);
        actionGroupSectionSortField->addAction(action);

        makeSortAction(actionGroupSectionSortField, "No Sections",          SortField::None);
        makeSortAction(actionGroupSectionSortField, "File Name",            SortField::FileName);
        makeSortAction(actionGroupSectionSortField, "File Size",            SortField::FileSize);
        makeSortAction(actionGroupSectionSortField, "File Extension",       SortField::FileType);
        makeSortAction(actionGroupSectionSortField, "Modified Date",        SortField::DateModified);
        makeSortAction(actionGroupSectionSortField, "Image Resolution",     SortField::Resolution);
        makeSortAction(actionGroupSectionSortField, "Original Record Date", SortField::DateRecorded);
        makeSortAction(actionGroupSectionSortField, "Aperture",             SortField::Aperture);
        makeSortAction(actionGroupSectionSortField, "Exposure",             SortField::Exposure);
        makeSortAction(actionGroupSectionSortField, "ISO",                  SortField::Iso);
        makeSortAction(actionGroupSectionSortField, "Camera Model",         SortField::CameraModel);
        makeSortAction(actionGroupSectionSortField, "Focal Length",         SortField::FocalLength);
        makeSortAction(actionGroupSectionSortField, "Lens Model",           SortField::Lens);

        connect(actionGroupSectionSortField, &QActionGroup::triggered, q, [](QAction* act) { ANPV::globalInstance()->setSectionSortField((SortField)act->data().toInt()); });
        connect(ANPV::globalInstance(), &ANPV::sectionSortOrderChanged, actionGroupSectionSortField,
            [&](SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder)
            {
                for (QAction* a : this->actionGroupSectionSortField->actions())
                {
                    if (!a->isSeparator() && newField == (SortField)a->data().toInt())
                    {
                        a->trigger();
                        break;
                    }
                }
            });
    }
    
    void refreshCopyMoveActions()
    {
        QActionGroup* actionGroupFileOperation = ANPV::globalInstance()->copyMoveActionGroup();
        ui->thumbnailListView->addActions(actionGroupFileOperation->actions());
        ui->menuEdit->addActions(actionGroupFileOperation->actions());
    }
    
    void createActions()
    {
        this->createViewActions();
        this->createSortActions();
        this->createDebugActions();

        QUndoStack* undoStack = ANPV::globalInstance()->undoStack();
        actionUndo = undoStack->createUndoAction(q, "Undo");
        actionUndo->setShortcuts(QKeySequence::Undo);
        actionUndo->setShortcutContext(Qt::ApplicationShortcut);

        actionRedo = undoStack->createRedoAction(q, "Redo");
        actionRedo->setShortcuts(QKeySequence::Redo);
        actionRedo->setShortcutContext(Qt::ApplicationShortcut);
        
        connect(ANPV::globalInstance()->copyMoveActionGroup(), &QActionGroup::triggered, q, [&](QAction* act)
        {
            QList<QObject*> objs = act->associatedObjects();
            for(QObject* o : objs)
            {
                if((o == ui->thumbnailListView && ui->thumbnailListView->hasFocus()) || (o == ui->menuEdit && ui->menuEdit->hasFocus()))
                {
                    ui->thumbnailListView->fileOperationOnSelectedFiles(act);
                    break;
                }
            }
        });

        actionFileOperationConfigDialog = new QAction("File Copy/Move Configuration", q);
        connect(actionFileOperationConfigDialog, &QAction::triggered, q, [&](bool)
        {
            FileOperationConfigDialog* dia = new FileOperationConfigDialog(ANPV::globalInstance()->copyMoveActionGroup(), q);
            connect(dia, &QDialog::accepted, q, [&]()
            {
                this->refreshCopyMoveActions();
            });
            
            dia->open();
        });

        connect(ui->actionAbout_ANPV, &QAction::triggered, ANPV::globalInstance(), &ANPV::about);
        connect(ui->actionAbout_Qt, &QAction::triggered, &QApplication::aboutQt);
    }
    
    void createMenus()
    {
        ui->menuFile->addAction(ANPV::globalInstance()->actionOpen());
        ui->menuFile->addSeparator();
        ui->menuFile->addAction(actionBack);
        ui->menuFile->addAction(actionForward);
        ui->menuFile->addAction(ANPV::globalInstance()->actionExit());
        
        ui->menuEdit->addAction(actionUndo);
        ui->menuEdit->addAction(actionRedo);
        ui->menuEdit->addSeparator();
        ui->menuEdit->addAction(actionFileOperationConfigDialog);
        ui->menuEdit->addSeparator();
        
        QMenu* sectionSortMenu = ui->menuSort->addMenu("Sections");
        sectionSortMenu->addActions(actionGroupSectionSortField->actions());
        sectionSortMenu->addActions(actionGroupSectionSortOrder->actions());

        QMenu* imageSortMenu = ui->menuSort->addMenu("Images");
        imageSortMenu->addActions(actionGroupImageSortField->actions());
        imageSortMenu->addActions(actionGroupImageSortOrder->actions());

        ui->menuHelp->insertAction(ui->actionAbout_ANPV, QWhatsThis::createAction(q));
        QAction* sep = new QAction();
        sep->setSeparator(true);
        ui->menuHelp->insertAction(ui->actionAbout_ANPV, sep);
    }
    
    void writeSettings()
    {
        auto& settings = ANPV::globalInstance()->settings();

        settings.beginGroup("MainWindow");
        settings.setValue("size", q->size());
        settings.setValue("pos", q->pos());
        settings.setValue("geometry", q->saveGeometry());
        settings.setValue("windowState", q->saveState());
        settings.endGroup();
    }

    void readSettings()
    {
        QScreen *ps = QGuiApplication::primaryScreen();
        QRect screenres = ps->geometry();

        auto& settings = ANPV::globalInstance()->settings();

        settings.beginGroup("MainWindow");
        // open the window on the primary screen
        // by moving and resize it explicitly
        q->resize(settings.value("size", QSize(screenres.width(), screenres.height())).toSize());
        q->move(settings.value("pos", screenres.topLeft()).toPoint());
        q->restoreGeometry(settings.value("geometry").toByteArray());
        q->restoreState(settings.value("windowState").toByteArray());
        settings.endGroup();
        
        this->refreshCopyMoveActions();
    }
    
    void onDirectoryTreeLoaded(const QString&)
    {
    }
    
    void resizeTreeColumn(const QModelIndex &index)
    {
        ui->fileSystemTreeView->resizeColumnToContents(0);
        ui->fileSystemTreeView->scrollTo(index);
    }
    
    QDir rememberedActivatedDir;
    void onTreeActivated(const QModelIndex& idx)
    {
        QFileInfo info = ANPV::globalInstance()->dirModel()->fileInfo(idx);
        rememberedActivatedDir = info.absoluteFilePath();
        ANPV::globalInstance()->setCurrentDir(info.absoluteFilePath());
    }
    
    QDir rememberedUrlNavigatorActivatedDir;
    void onUrlNavigatorNavigationTriggered(const QUrl& url)
    {
        QString path = url.path();
        if (!url.isValid() || path.isEmpty())
        {
            qInfo() << "onUrlNavigatorNavigationTriggered() got a null or empty url:" << url << " | " << path;
            return;
        }

#ifdef Q_OS_WIN
        if (path[0] == '/')
        {
            path.remove(0, 1);
        }
#endif
        rememberedUrlNavigatorActivatedDir = path;
        ANPV::globalInstance()->setCurrentDir(path);
    }
    
    void onCurrentDirChanged(QString& newDir, QString&)
    {
        QModelIndex mo = ANPV::globalInstance()->dirModel()->index(newDir);
        ui->fileSystemTreeView->setCurrentIndex(mo);

        // if the newDir was triggered by an activiation in the treeView, do not scroll around
        if(QDir(newDir) != rememberedActivatedDir)
        {
            // vertically scroll to center
            ui->fileSystemTreeView->scrollTo(mo, QAbstractItemView::PositionAtCenter);
            // and make sure we do not scroll to center horizontally
            ui->fileSystemTreeView->scrollTo(mo, QAbstractItemView::EnsureVisible);
        }

        rememberedActivatedDir = QDir();
        rememberedUrlNavigatorActivatedDir = QDir();

        q->setWindowTitle(newDir + " :: ANPV");
    }
    
    void onIconHeightChanged(int h, int)
    {
        if(!ui->iconSizeSlider->isSliderDown())
        {
            QSignalBlocker b(ui->iconSizeSlider);
            // this is the initial change event, set the value of the slider
            ui->iconSizeSlider->setValue(h);
        }
        ui->iconSizeSlider->setToolTip(QString("Icon height: %1 px").arg(h));
    }
    
    void onIconSizeSliderValueChanged(int val)
    {
        ANPV::globalInstance()->setIconHeight(val);
    }
    
    void onIconSizeSliderMoved(int val)
    {
        onIconSizeSliderValueChanged(val);
        QToolTip::showText(QCursor::pos(), QString("%1 px").arg(val), nullptr);
    }
    
    void resetRegularExpression()
    {
        this->ui->filterPatternLineEdit->setText("");
        this->ui->filterSyntaxComboBox->setCurrentIndex(0);
        this->ui->filterCaseSensitivityCheckBox->setCheckState(Qt::Unchecked);
    }

    void filterRegularExpressionChanged()
    {
        enum Syntax {
            FixedString,
            Wildcard,
            RegularExpression
        };
        
        Syntax s = Syntax(ui->filterSyntaxComboBox->currentIndex());
        QString pattern = ui->filterPatternLineEdit->text();
        switch (s) {
        case Wildcard:
            pattern = QRegularExpression::wildcardToRegularExpression(pattern);
            break;
        case FixedString:
            pattern = QRegularExpression::escape(pattern);
            break;
        default:
            break;
        }

        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!ui->filterCaseSensitivityCheckBox->isChecked())
        {
            options |= QRegularExpression::CaseInsensitiveOption;
        }

        QRegularExpression regularExpression(pattern, options);
        if (regularExpression.isValid())
        {
            ui->filterPatternLineEdit->setPalette(ui->filterPatternLineEdit->style()->standardPalette());
            ui->filterPatternLineEdit->setToolTip(QString());
            // Emit layout changed to make sure all items are properly displayed
            proxyModel->layoutAboutToBeChanged();
            proxyModel->setFilterRegularExpression(regularExpression);
            proxyModel->layoutChanged();
        }
        else
        {
            QPalette palette;
            palette.setColor(QPalette::Text, Qt::red);
            ui->filterPatternLineEdit->setPalette(palette);
            ui->filterPatternLineEdit->setToolTip(regularExpression.errorString());
            proxyModel->layoutAboutToBeChanged();
            proxyModel->setFilterRegularExpression(QRegularExpression());
            proxyModel->layoutChanged();
        }
    }
    
    void clearInfoBox()
    {
        this->ui->infoBox->setText("");
    }

    void onImageCheckStateChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles)
    {
        if (!roles.contains(Qt::CheckStateRole))
        {
            return;
        }

        size_t size = 0;
        auto model = ANPV::globalInstance()->fileModel();
        QItemSelectionRange r(this->proxyModel->mapToSource(topLeft), this->proxyModel->mapToSource(bottomRight));
        QModelIndexList idx = r.indexes();
        for(const QModelIndex& i : idx)
        {
            auto img = model->imageFromItem(model->item(i));
            if (img)
            {
                size += img->fileInfo().size();
            }
        }
    }
    
    void onThumbnailListViewSelectionChanged(const QItemSelection &, const QItemSelection &)
    {
        auto imgs = this->ui->thumbnailListView->selectedImages();
        auto chkImgs = this->ui->thumbnailListView->checkedImages();
        
        if(imgs.isEmpty() && chkImgs.isEmpty())
        {
            this->clearInfoBox();
        }
        else
        {
            QString text;
            size_t count = imgs.size();
            if (count > 0)
            {
                size_t size = 0;
                for (QSharedPointer<Image>& e : imgs)
                {
                    size += e->fileInfo().size();
                }

                text += QString(
                    "%1 items selected<br />"
                    "%2").arg(QString::number(count)).arg(ANPV::formatByteHtmlString(size));
            }

            count = chkImgs.size();
            if (count > 0)
            {
                size_t size = 0;
                for (auto& i : chkImgs)
                {
                    size += i->fileInfo().size();
                }
                if (!text.isEmpty())
                {
                    text += "<br /><br />";
                }
                text += QString(
                    "%1 items checked<br />"
                    "%2").arg(QString::number(count)).arg(ANPV::formatByteHtmlString(size));
            }
            this->ui->infoBox->setText(text);
        }
    }
};

MainWindow::MainWindow(QSplashScreen *splash)
 : QMainWindow(), d(std::make_unique<Impl>(this))
{
    this->setWindowTitle("ANPV");
    this->setWindowFlags(this->windowFlags() | Qt::WindowContextHelpButtonHint);
    
    d->proxyModel = new QSortFilterProxyModel(this);
    d->proxyModel->setSourceModel(ANPV::globalInstance()->fileModel().get());
    
    splash->showMessage("Creating MainWindow Widgets");
    d->ui->setupUi(this);
    d->createActions();
    d->createMenus();
    
    splash->showMessage("Initializing MainWindow Widgets");
    d->ui->fileSystemTreeView->setHeaderHidden(true);
    d->ui->fileSystemTreeView->setModel(ANPV::globalInstance()->dirModel());
    d->ui->fileSystemTreeView->showColumn(0);
    d->ui->fileSystemTreeView->hideColumn(1);
    d->ui->fileSystemTreeView->hideColumn(2);
    d->ui->fileSystemTreeView->hideColumn(3);
    d->ui->fileSystemTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->ui->fileSystemTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    d->ui->fileSystemTreeView->setRootIndex(ANPV::globalInstance()->dirModel()->index(ANPV::globalInstance()->dirModel()->rootPath()));
    
    d->ui->iconSizeSlider->setRange(0, ANPV::MaxIconHeight);
    d->ui->thumbnailListView->setModel(d->proxyModel);
    
    splash->showMessage("Connecting MainWindow Signals / Slots");
    
    connect(d->ui->fileSystemTreeView, &QTreeView::activated, this, [&](const QModelIndex &idx){d->onTreeActivated(idx);});
    connect(d->ui->fileSystemTreeView, &QTreeView::expanded, this, [&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    connect(d->ui->fileSystemTreeView, &QTreeView::collapsed, this,[&](const QModelIndex &idx){d->resizeTreeColumn(idx);});
    connect(ANPV::globalInstance()->dirModel(), &QFileSystemModel::directoryLoaded, this, [&](const QString& s){d->onDirectoryTreeLoaded(s);});

    connect(ANPV::globalInstance(), &ANPV::currentDirChanged, this, [&](QString newD, QString old){ d->onCurrentDirChanged(newD,old);}, Qt::QueuedConnection);
    connect(ANPV::globalInstance(), &ANPV::iconHeightChanged, this, [&](int h, int old){ d->onIconHeightChanged(h,old);}, Qt::DirectConnection);
    
    connect(d->ui->iconSizeSlider, &QSlider::sliderMoved, this, [&](int value){d->onIconSizeSliderMoved(value);}, Qt::DirectConnection);
    connect(d->ui->iconSizeSlider, &QSlider::valueChanged, this, [&](int value){d->onIconSizeSliderValueChanged(value);}, Qt::DirectConnection);

    connect(d->ui->filterPatternLineEdit, &QLineEdit::returnPressed,
        d->ui->searchButton, &QAbstractButton::click);
    connect(d->ui->searchButton, &QAbstractButton::pressed,
        this, [&]() { d->filterRegularExpressionChanged(); });
    connect(d->ui->resetButton, &QAbstractButton::pressed,
        this, [&]() { d->resetRegularExpression(); });
    connect(d->ui->resetButton, &QAbstractButton::pressed,
        d->ui->searchButton, &QAbstractButton::click);

    connect(d->proxyModel, &QSortFilterProxyModel::modelAboutToBeReset, this, [&](){ d->clearInfoBox(); });
    connect(d->proxyModel, &QSortFilterProxyModel::dataChanged, this, [&](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) { d->onImageCheckStateChanged(topLeft, bottomRight, roles); });
    connect(d->ui->thumbnailListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [&](const QItemSelection &selected, const QItemSelection &deselected)
    {
        d->onThumbnailListViewSelectionChanged(selected, deselected);
    });

//     connect(d->cancellableWidget, &CancellableProgressWidget::expired, this, &MainWindow::hideProgressWidget);
    connect(d->ui->urlNavigator, &UrlNavigatorWidget::pathChangedByUser, ANPV::globalInstance(), QOverload<const QString&>::of(&ANPV::setCurrentDir));
}

MainWindow::~MainWindow() = default;

bool MainWindow::event(QEvent* evt)
{
    if (evt->type() == QEvent::Close)
    {
        auto model = ANPV::globalInstance()->fileModel();
        if (model && !model->isSafeToChangeDir())
        {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm closing", "You have checked images recently. If you proceed, the selection of images will be lost. Are you sure to proceed?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::No)
            {
                evt->ignore();
                return false;
            }
        }
    }
    return QWidget::event(evt);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // cancel any background task
    this->setBackgroundTask(QFuture<DecodingState>());
    d->writeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    auto button = event->button();
    switch(button)
    {
        case Qt::BackButton:
            d->actionBack->trigger();
            event->accept();
            return;
        case Qt::ForwardButton:
            d->actionForward->trigger();
            event->accept();
            return;
        default:
            break;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::setBackgroundTask(const QFuture<DecodingState>& fut)
{
    xThreadGuard g(this);

    d->ui->cancellableWidget->setFuture(fut);
    d->ui->cancellableWidget->show();
}

void MainWindow::hideProgressWidget(CancellableProgressWidget*)
{
    xThreadGuard g(this);

    d->ui->cancellableWidget->hide();
}

void MainWindow::setCurrentIndex(QSharedPointer<Image> img)
{
    QModelIndex wantedIdx = ANPV::globalInstance()->fileModel()->index(img);
    if(!wantedIdx.isValid())
    {
        return;
    }

    d->ui->thumbnailListView->selectionModel()->setCurrentIndex(wantedIdx, QItemSelectionModel::NoUpdate);
    d->ui->thumbnailListView->scrollTo(wantedIdx, QAbstractItemView::PositionAtCenter);
}

void MainWindow::readSettings()
{
    d->readSettings();
}
