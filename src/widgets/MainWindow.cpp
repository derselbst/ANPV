#include "MainWindow.hpp"

#include <QMainWindow>
#include <QProgressBar>
#include <QStackedLayout>
#include <QWidget>
#include <QSplashScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QSplashScreen>
#include <QScreen>
#include <QtDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <QDir>
#include <QFileSystemModel>
#include <QListView>
#include <QActionGroup>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QUndoStack>
#include <QMessageBox>
#include <QPair>
#include <QPointer>

#include "DocumentView.hpp"
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
    MainWindow* q;
    std::unique_ptr<Ui::MainWindow> ui = std::make_unique<Ui::MainWindow>();
    
    QUndoStack* undoStack;
    std::map<ProgressGroup, QPointer<CancellableProgressWidget>> progressWidgetGroupMap;
    QVBoxLayout* progressWidgetLayout;
    QWidget* progressWidgetContainer;
    QStackedLayout* stackedLayout;
    QWidget *stackedLayoutWidget;
    DocumentView* imageViewer;
    
    SortedImageModel* fileModel;
    
    QMenu* menuFile;
    QMenu* menuView;
    QMenu* menuEdit;
    QMenu* menuSort;
    
    QAction* actionSortFileName;
    QAction* actionSortFileSize;
    QActionGroup* actionGroupSortColumn;
    QActionGroup* actionGroupSortOrder;
    QActionGroup* actionGroupFileOperation;
    QActionGroup* actionGroupViewMode;
        
    QAction *actionUndo;
    QAction *actionRedo;
    QAction *actionFileOperationConfigDialog;
    QAction *actionExit;
    
    Impl(MainWindow* parent) : q(parent)
    {
    }
    
    void addSlowHint(QAction* action)
    {
        action->setToolTip("This option requires to read EXIF metadata from the file. Therefore, performance greatly suffers when accessing directories that contain many files.");
    }
    
    void createActions()
    {
        QAction* action;
//         
//         actionGroupFileOperation = new QActionGroup(q);
//         connect(actionGroupFileOperation, &QActionGroup::triggered, q, [&](QAction* act)
//         {
//             QString targetDir = act->data().toString();
//             q->moveFilesSlot(targetDir);
//         });

        actionGroupViewMode = new QActionGroup(q);
        
        auto makeViewAction = [&](QAction* action, ViewMode view)
        {
            connect(action, &QAction::triggered, q, [=](bool){ ANPV::globalInstance()->setViewMode(view); });
            connect(ANPV::globalInstance(), &ANPV::viewModeChanged, action,
                    [=](ViewMode newMode, ViewMode old)
                    {
                        if(newMode == view)
                        {
                            action->trigger();
                        }
                    });
            actionGroupViewMode->addAction(action);
        };
        
        
        makeViewAction(ui->actionNo_Change, ViewMode::None);
        makeViewAction(ui->actionFit_in_FOV, ViewMode::Fit);
        makeViewAction(ui->actionCenter_AF_focus_point, ViewMode::CenterAf);
        
        
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
        makeSortAction("Modified Date", SortedImageModel::Column::DateModified);
        makeSortAction("Image Resolution (slow)", SortedImageModel::Column::Resolution, true);
        makeSortAction("Original Record Date (slow)", SortedImageModel::Column::DateRecorded, true);
        makeSortAction("Aperture (slow)", SortedImageModel::Column::Aperture, true);
        makeSortAction("Exposure (slow)", SortedImageModel::Column::Exposure, true);
        makeSortAction("ISO (slow)", SortedImageModel::Column::Iso, true);
        makeSortAction("Camera Model (slow)", SortedImageModel::Column::CameraModel, true);
        makeSortAction("Focal Length (slow)", SortedImageModel::Column::FocalLength, true);
        makeSortAction("Lens Model (slow)", SortedImageModel::Column::Lens, true);
        

        actionUndo = undoStack->createUndoAction(q, "Undo");
        actionUndo->setShortcuts(QKeySequence::Undo);
        actionUndo->setShortcutContext(Qt::ApplicationShortcut);

        actionRedo = undoStack->createRedoAction(q, "Redo");
        actionRedo->setShortcuts(QKeySequence::Redo);
        actionRedo->setShortcutContext(Qt::ApplicationShortcut);
        
        actionFileOperationConfigDialog = new QAction("File Copy/Move Configuration", q);
//         connect(actionFileOperationConfigDialog, &QAction::triggered, q, [&](bool)
//         {
//             FileOperationConfigDialog* dia = new FileOperationConfigDialog(actionGroupFileOperation, q);
//             connect(dia, &QDialog::accepted, q, [&]()
//             {
//                 menuEdit->addActions(actionGroupFileOperation->actions());
//             });
//             
//             dia->open();
//         });

        ui->actionExit->setShortcuts(QKeySequence::Quit);
        connect(ui->actionExit, &QAction::triggered, q, &QApplication::closeAllWindows);
        
        connect(ui->actionAbout_Qt, &QAction::triggered, &QApplication::aboutQt);
    }
    
    // should be called after settings have been read!
    void initializeActions()
    {
//         ViewMode v = ANPV::globalInstance()->viewMode();
//         switch(v)
//         {
//             case ViewMode::None:
//                 ui->actionNo_Change->setChecked(true);
//                 break;
//             case ViewMode::Fit:
//                 ui->actionFit_in_FOV->setChecked(true);
//                 break;
//             case ViewMode::CenterAf:
//                 ui->actionCenter_AF_focus_point->setChecked(true);
//                 break;
//             default:
//                 throw std::logic_error("case not implemented: viewMode");
//         }
        
    }
    
    void createMenus()
    {
        ui->menuEdit->addAction(actionUndo);
        ui->menuEdit->addAction(actionRedo);
        ui->menuEdit->addSeparator();
        ui->menuEdit->addAction(actionFileOperationConfigDialog);
        
        ui->menuSort->addActions(actionGroupSortColumn->actions());
        ui->menuSort->addActions(actionGroupSortOrder->actions());
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
        
        settings.setValue("currentDir", ANPV::globalInstance()->currentDir().absolutePath());
        settings.setValue("viewMode", static_cast<int>(ANPV::globalInstance()->viewMode()));
        settings.setValue("sortOrder", static_cast<int>(ANPV::globalInstance()->sortOrder()));
        settings.setValue("primarySortColumn", static_cast<int>(ANPV::globalInstance()->primarySortColumn()));
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
        
        ANPV::globalInstance()->setCurrentDir(settings.value("currentDir", qgetenv("HOME")).toString());
        ANPV::globalInstance()->setViewMode(static_cast<ViewMode>(settings.value("viewMode", static_cast<int>(ViewMode::Fit)).toInt()));
        ANPV::globalInstance()->setSortOrder(static_cast<Qt::SortOrder>(settings.value("sortOrder", Qt::AscendingOrder).toInt()));
        ANPV::globalInstance()->setPrimarySortColumn(static_cast<SortedImageModel::Column>(settings.value("primarySortColumn", static_cast<int>(SortedImageModel::Column::FileName)).toInt()));
    }
};

MainWindow::MainWindow(QSplashScreen *splash)
 : QMainWindow(), d(std::make_unique<Impl>(this))
{
    this->setWindowTitle("ANPV");
    
    d->undoStack = new QUndoStack(this);
    
    splash->showMessage("Creating MainWindow Widgets");
    d->ui->setupUi(this);
    d->createActions();
    d->createMenus();
    
    d->readSettings();
    d->initializeActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    d->writeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut)
{
    xThreadGuard(this);

    CancellableProgressWidget* w;
    try
    {
        w = d->progressWidgetGroupMap.at(group);
        w->setFuture(fut);
    }
    catch(std::out_of_range& e)
    {
        w = new CancellableProgressWidget(fut, this);
        w->connect(w, &CancellableProgressWidget::expired, this, &MainWindow::hideProgressWidget);
        d->progressWidgetGroupMap[group] = w;
    }
    d->progressWidgetLayout->addWidget(w);
    w->show();

    for (const auto& [key, value] : d->progressWidgetGroupMap)
    {
        if(!value.isNull() && value->isFinished())
        {
            this->hideProgressWidget(value);
        }
    }
}

void MainWindow::hideProgressWidget(CancellableProgressWidget* w)
{
    if(d->progressWidgetLayout->count() >= 2)
    {
        d->progressWidgetLayout->removeWidget(w);
        w->hide();
    }
}

