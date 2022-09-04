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
#include <KUrlNavigator>

#include "DocumentView.hpp"
#include "PreviewAllImagesDialog.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"


#include "ui_MainWindow.h"

struct MainWindow::Impl
{
    MainWindow* q = nullptr;
    std::unique_ptr<Ui::MainWindow> ui = std::make_unique<Ui::MainWindow>();
    
    QSortFilterProxyModel* proxyModel = nullptr;
    
    CancellableProgressWidget* cancellableWidget = nullptr;
    
    QActionGroup* actionGroupSortColumn = nullptr;
    QActionGroup* actionGroupSortOrder = nullptr;
    QActionGroup* actionGroupViewMode = nullptr;
        
    QAction *actionUndo = nullptr;
    QAction *actionRedo = nullptr;
    QAction *actionFileOperationConfigDialog = nullptr;
    QAction *actionExit = nullptr;
    
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
        actionGroupViewMode = new QActionGroup(q);
        
        auto makeViewAction = [&](QAction* action, ViewMode view)
        {
            connect(action, &QAction::triggered, q, [=](bool){ ANPV::globalInstance()->setViewMode(view); });
            connect(ANPV::globalInstance(), &ANPV::viewModeChanged, action,
                    [=](ViewMode newMode, ViewMode)
                    {
                        // trigger action to ensure proper selection
                        action->setChecked(newMode == view);
                    });
            actionGroupViewMode->addAction(action);
        };
        
        makeViewAction(ui->actionNo_Change, ViewMode::None);
        makeViewAction(ui->actionFit_in_FOV, ViewMode::Fit);
        
        auto makeTriggerAction = [&](QAction* action, ViewFlag v)
        {
            connect(action, &QAction::triggered, q, [=](bool isChecked){ ANPV::globalInstance()->setViewFlag(v, isChecked); });
            connect(ANPV::globalInstance(), &ANPV::viewFlagsChanged, action,
                    [=](ViewFlags_t newMode, ViewFlags_t)
                    {
                        action->setChecked((newMode & static_cast<ViewFlags_t>(v)) != 0);
                    });
        };
        
        makeTriggerAction(ui->actionCombine_RAWs_and_JPGs, ViewFlag::CombineRawJpg);
        makeTriggerAction(ui->actionShow_AF_Points, ViewFlag::ShowAfPoints);
        makeTriggerAction(ui->actionRespect_EXIF_orientation, ViewFlag::RespectExifOrientation);
        makeTriggerAction(ui->actionCenter_AF_focus_point, ViewFlag::CenterAf);

        connect(ui->actionReload, &QAction::triggered, q,
            [&](bool)
            {
                ANPV::globalInstance()->fileModel()->changeDirAsync(ANPV::globalInstance()->currentDir());
            }
        );
        connect(ui->actionPreview_all_images, &QAction::triggered, q,
            [&](bool)
            {
                PreviewAllImagesDialog d;
                d.setImageHeight(ANPV::globalInstance()->iconHeight());
                if(d.exec() == QDialog::Accepted)
                {
                    ANPV::globalInstance()->fileModel()->decodeAllImages(DecodingState::PreviewImage, d.imageHeight());
                }
            }
        );
    }
    
    void createSortActions()
    {
        QAction* action;

        actionGroupSortOrder = new QActionGroup(q);
        
        action = new QAction("Sort Order", q);
        action->setSeparator(true);
        actionGroupSortOrder->addAction(action);
        
        action = new QAction("Ascending (small to big)", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ ANPV::globalInstance()->setSortOrder(Qt::AscendingOrder); });
        connect(ANPV::globalInstance(), &ANPV::sortOrderChanged, action,
                [=](Qt::SortOrder newOrd, Qt::SortOrder)
                {
                    if(newOrd == Qt::AscendingOrder)
                    {
                        action->trigger();
                    }
                });
        actionGroupSortOrder->addAction(action);
        
        action = new QAction("Descending (big to small)", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ ANPV::globalInstance()->setSortOrder(Qt::DescendingOrder); });
        connect(ANPV::globalInstance(), &ANPV::sortOrderChanged, action,
                [=](Qt::SortOrder newOrd, Qt::SortOrder)
                {
                    if(newOrd == Qt::DescendingOrder)
                    {
                        action->trigger();
                    }
                });
        actionGroupSortOrder->addAction(action);
        
        
        actionGroupSortColumn = new QActionGroup(q);
        
        action = new QAction("Sort according to", q);
        action->setSeparator(true);
        actionGroupSortColumn->addAction(action);
        
        auto makeSortAction = [&](QString&& name, SortedImageModel::Column col, bool isSlow=false)
        {
            QAction* action = new QAction(std::move(name), q);
            action->setCheckable(true);
            if(isSlow)
            {
                addSlowHint(action);
            }
            connect(action, &QAction::triggered, q, [=](bool){ ANPV::globalInstance()->setPrimarySortColumn(col); });
            connect(ANPV::globalInstance(), &ANPV::primarySortColumnChanged, action,
                    [=](SortedImageModel::Column newCol, SortedImageModel::Column)
                    {
                        if(newCol == col)
                        {
                            action->trigger();
                        }
                    });
            actionGroupSortColumn->addAction(action);
        };
        
        makeSortAction("File Name", SortedImageModel::Column::FileName);
        makeSortAction("File Size", SortedImageModel::Column::FileSize);
        makeSortAction("File Extension", SortedImageModel::Column::FileType);
        makeSortAction("Modified Date", SortedImageModel::Column::DateModified);
        makeSortAction("Image Resolution (slow)", SortedImageModel::Column::Resolution, true);
        makeSortAction("Original Record Date (slow)", SortedImageModel::Column::DateRecorded, true);
        makeSortAction("Aperture (slow)", SortedImageModel::Column::Aperture, true);
        makeSortAction("Exposure (slow)", SortedImageModel::Column::Exposure, true);
        makeSortAction("ISO (slow)", SortedImageModel::Column::Iso, true);
        makeSortAction("Camera Model (slow)", SortedImageModel::Column::CameraModel, true);
        makeSortAction("Focal Length (slow)", SortedImageModel::Column::FocalLength, true);
        makeSortAction("Lens Model (slow)", SortedImageModel::Column::Lens, true);
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
        
        actionBack = new QAction(QIcon::fromTheme("go-previous"), "Previous Folder", q);
        actionBack->setShortcuts({QKeySequence(Qt::Key_Back)});
        actionBack->setShortcutContext(Qt::WidgetShortcut);
        connect(actionBack, &QAction::triggered, q, [&](bool){ this->ui->urlNavigator->goBack(); });

        actionForward = new QAction(QIcon::fromTheme("go-next"), "Next Folder", q);
        actionForward->setShortcuts({QKeySequence(Qt::Key_Forward)});
        actionForward->setShortcutContext(Qt::WidgetShortcut);
        connect(actionForward, &QAction::triggered, q, [&](bool){ this->ui->urlNavigator->goForward(); });
    
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
        
        ui->menuSort->addActions(actionGroupSortColumn->actions());
        ui->menuSort->addActions(actionGroupSortOrder->actions());
        
        ui->menuHelp->insertAction(ui->actionAbout_ANPV, QWhatsThis::createAction(q));
        QAction* sep = new QAction();
        sep->setSeparator(true);
        ui->menuHelp->insertAction(ui->actionAbout_ANPV, sep);
    }
    
    void writeSettings()
    {
        QSettings settings;

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

        QSettings settings;

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

        if(QDir(newDir) != rememberedUrlNavigatorActivatedDir)
        {
            // avoid infinite recursion
            ui->urlNavigator->setLocationUrl(QUrl::fromLocalFile(newDir));
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
            proxyModel->setFilterRegularExpression(regularExpression);
        }
        else
        {
            QPalette palette;
            palette.setColor(QPalette::Text, Qt::red);
            ui->filterPatternLineEdit->setPalette(palette);
            ui->filterPatternLineEdit->setToolTip(regularExpression.errorString());
            proxyModel->setFilterRegularExpression(QRegularExpression());
        }
    }
    
    void clearInfoBox()
    {
        this->ui->infoBox->setText("");
    }
    
    void onThumbnailListViewSelectionChanged(const QItemSelection &, const QItemSelection &)
    {
        auto imgs = this->ui->thumbnailListView->selectedImages();
        
        if(imgs.isEmpty())
        {
            this->clearInfoBox();
        }
        else
        {
            size_t count = imgs.size();
            size_t size = 0;
            for(Entry_t& e : imgs)
            {
                size += SortedImageModel::image(e)->fileInfo().size();
            }
            
            QString text = QString(
                "%1 items selected<br />"
                "%2").arg(QString::number(count)).arg(ANPV::formatByteHtmlString(size));
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

    connect(d->ui->filterPatternLineEdit, &QLineEdit::textChanged,
            this, [&](){ d->filterRegularExpressionChanged(); });
    connect(d->ui->filterSyntaxComboBox, &QComboBox::currentIndexChanged,
            this, [&](){ d->filterRegularExpressionChanged(); });
    connect(d->ui->filterCaseSensitivityCheckBox, &QAbstractButton::toggled,
            this, [&](){ d->filterRegularExpressionChanged(); });
    
    connect(d->proxyModel, &QSortFilterProxyModel::modelAboutToBeReset, this, [&](){ d->clearInfoBox(); });
    connect(d->ui->thumbnailListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [&](const QItemSelection &selected, const QItemSelection &deselected)
    {
        d->onThumbnailListViewSelectionChanged(selected, deselected);
    });
    
    connect(d->ui->urlNavigator, &KUrlNavigator::urlChanged, this, [&](const QUrl& url){ d->onUrlNavigatorNavigationTriggered(url); });
//     connect(d->cancellableWidget, &CancellableProgressWidget::expired, this, &MainWindow::hideProgressWidget);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
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
