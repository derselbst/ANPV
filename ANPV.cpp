#include "ANPV.hpp"

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
#include <QUndoStack>
#include <QMessageBox>
#include <QPair>
#include <QPointer>

#include "DocumentView.hpp"
#include "ThumbnailView.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfig.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"


struct ANPV::Impl
{
    ANPV* q;
    
    QUndoStack* undoStack;
    std::map<int, QPointer<CancellableProgressWidget>> progressWidgetGroupMap;
    QVBoxLayout* progressWidgetLayout;
    QWidget* progressWidgetContainer;
    QStackedLayout* stackedLayout;
    QWidget *stackedLayoutWidget;
    DocumentView* imageViewer;
    ThumbnailView* thumbnailViewer;
    
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
    
    ViewMode viewMode = ViewMode::Fit;
    
    QAction *actionUndo;
    QAction *actionRedo;
    QAction *actionFileOperationConfigDialog;
    QAction *actionExit;
    
    Impl(ANPV* parent) : q(parent)
    {
    }
    
    static QString getProgressStyle(DecodingState state)
    {
        constexpr char successStart[] = "#99ffbb";
        constexpr char successEnd[] = "#00cc44";
        constexpr char errorStart[] = "#ff9999";
        constexpr char errorEnd[] = "#d40000";
        
        const char* colorStart;
        const char* colorEnd;
        
        switch(state)
        {
            case DecodingState::Error:
            case DecodingState::Cancelled:
                colorStart = errorStart;
                colorEnd = errorEnd;
                break;
            default:
                colorStart = successStart;
                colorEnd = successEnd;
                break;
        }
        
        return QString(
            "QProgressBar {"
            "border: 2px solid grey;"
            "border-radius: 5px;"
            "text-align: center;"
            "}"
            ""
            "QProgressBar::chunk {"
            "background-color: qlineargradient(x1: 0, y1: 0.2, x2: 1, y2: 0, stop: 0 %1, stop: 1 %2);"
            "width: 20px;"
            "margin: 0px;"
            "}").arg(colorStart).arg(colorEnd);
    }
    
    void addSlowHint(QAction* action)
    {
        action->setToolTip("This option requires to read EXIF metadata from the file. Therefore, performance greatly suffers when accessing directories that contain many files.");
    }
    
    void createActions()
    {
        QAction* action;
        
        actionGroupFileOperation = new QActionGroup(q);
        connect(actionGroupFileOperation, &QActionGroup::triggered, q, [&](QAction* act)
        {
            QString targetDir = act->data().toString();
            q->moveFilesSlot(targetDir);
        });
        
        
        actionGroupViewMode = new QActionGroup(q);
        
        action = new QAction("View Mode", q);
        action->setSeparator(true);
        actionGroupViewMode->addAction(action);
        
        action = new QAction("No Change", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ this->viewMode = ViewMode::None; });
        actionGroupViewMode->addAction(action);
        
        action = new QAction("Fit in FOV", q);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, q, [&](bool){ this->viewMode = ViewMode::Fit; });
        actionGroupViewMode->addAction(action);
        
        action = new QAction("Center AF focus point", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ this->viewMode = ViewMode::CenterAf; });
        actionGroupViewMode->addAction(action);
        
        
        actionGroupSortOrder = new QActionGroup(q);
        
        action = new QAction("Sort Order", q);
        action->setSeparator(true);
        actionGroupSortOrder->addAction(action);
        
        action = new QAction("Ascending (small to big)", q);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(Qt::AscendingOrder); });
        actionGroupSortOrder->addAction(action);
        
        action = new QAction("Descending (big to small)", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(Qt::DescendingOrder); });
        actionGroupSortOrder->addAction(action);
        
        
        actionGroupSortColumn = new QActionGroup(q);
        
        action = new QAction("Sort according to", q);
        action->setSeparator(true);
        actionGroupSortColumn->addAction(action);
        
        action = new QAction("File Name", q);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::FileName); });
        actionGroupSortColumn->addAction(action);
        
        action = new QAction("File Size", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::FileSize); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Modified Date", q);
        action->setCheckable(true);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::DateModified); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Image Resolution (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::Resolution); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Original Record Date (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::DateRecorded); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Aperture (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::Aperture); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Exposure (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::Exposure); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("ISO (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::Iso); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Camera Model (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::CameraModel); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Focal Length (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::FocalLength); });
        actionGroupSortColumn->addAction(action);

        action = new QAction("Lens Model (slow)", q);
        action->setCheckable(true);
        addSlowHint(action);
        connect(action, &QAction::triggered, q, [&](bool){ q->d->fileModel->sort(SortedImageModel::Column::Lens); });
        actionGroupSortColumn->addAction(action);
        
        actionUndo = undoStack->createUndoAction(q, "&Undo");
        actionUndo->setShortcuts(QKeySequence::Undo);

        actionRedo = undoStack->createRedoAction(q, "&Redo");
        actionRedo->setShortcuts(QKeySequence::Redo);
        
        actionFileOperationConfigDialog = new QAction("File Copy/Move Configuration", q);
        connect(actionFileOperationConfigDialog, &QAction::triggered, q, [&](bool)
        {
            FileOperationConfig* dia = new FileOperationConfig(actionGroupFileOperation, q);
            connect(dia, &QDialog::accepted, q, [&]()
            {
                menuEdit->addActions(actionGroupFileOperation->actions());
            });
            
            dia->open();
        });

        actionExit = new QAction("E&xit", q);
        actionExit->setShortcuts(QKeySequence::Quit);
        connect(actionExit, &QAction::triggered, q, &ANPV::close);

    }
    
    void createMenus()
    {
        menuFile = q->menuBar()->addMenu("&File");
        menuFile->addAction(actionExit);
        
        menuView = q->menuBar()->addMenu("&View");
        menuView->addActions(actionGroupViewMode->actions());

        menuEdit = q->menuBar()->addMenu("&Edit");
        menuEdit->addAction(actionUndo);
        menuEdit->addAction(actionRedo);
        menuEdit->addSeparator();
        menuEdit->addAction(actionFileOperationConfigDialog);
        menuEdit->addSeparator();
        
        menuSort = q->menuBar()->addMenu("&Sort");
        menuSort->addActions(actionGroupSortColumn->actions());
        menuSort->addActions(actionGroupSortOrder->actions());
    }
    
    void onImageNavigate(const QString& url, int stepsForward)
    {
        QModelIndex idx;
        QSharedPointer<SmartImageDecoder> dec = fileModel->goTo(url, stepsForward, idx);
        if(dec && idx.isValid())
        {
            q->loadImage(dec);
            thumbnailViewer->selectThumbnail(idx);
        }
        else
        {
            q->showThumbnailView();
        }
    }
};

ANPV::ANPV(QSplashScreen *splash)
 : QMainWindow(), d(std::make_unique<Impl>(this))
{
    QScreen *ps = QGuiApplication::primaryScreen();
    QRect screenres = ps->geometry();
    // open the window on the primary screen
    // by moving and resize it explicitly
    this->move(screenres.topLeft());
    this->resize(screenres.width(), screenres.height());
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowTitle("ANPV");
    
    splash->showMessage("Creating UI Widgets");
    
    d->progressWidgetLayout = new QVBoxLayout(this);
    d->progressWidgetContainer = new QWidget(this);
    d->progressWidgetContainer->setLayout(d->progressWidgetLayout);
    this->statusBar()->showMessage(tr("Ready"));
    this->statusBar()->addPermanentWidget(d->progressWidgetContainer, 1);
    
    d->fileModel = new SortedImageModel(this);
    d->thumbnailViewer = new ThumbnailView(d->fileModel, this);
    
    d->imageViewer = new DocumentView(this);
    connect(d->imageViewer, &DocumentView::requestNext, this,
            [&](QString current) { d->onImageNavigate(current, 1); });
    connect(d->imageViewer, &DocumentView::requestPrev, this,
            [&](QString current) { d->onImageNavigate(current, -1); });

    d->stackedLayout = new QStackedLayout(this);
    d->stackedLayout->addWidget(d->thumbnailViewer);
    d->stackedLayout->addWidget(d->imageViewer);
    
    d->stackedLayoutWidget = new QWidget(this);
    d->stackedLayoutWidget->setLayout(d->stackedLayout);
    this->setCentralWidget(d->stackedLayoutWidget);
    
    d->undoStack = new QUndoStack(this);
    
    d->createActions();
    d->createMenus();
}

ANPV::~ANPV() = default;


void ANPV::showImageView()
{
    d->stackedLayout->setCurrentWidget(d->imageViewer);
}

void ANPV::showThumbnailView()
{
    d->thumbnailViewer->scrollToCurrentImage();
    d->stackedLayout->setCurrentWidget(d->thumbnailViewer);
}

void ANPV::loadImage(QFileInfo inf)
{
    this->setWindowTitle(inf.fileName());
    d->imageViewer->loadImage(inf.absoluteFilePath());
    this->setThumbnailDir(inf.absoluteDir().absolutePath());
}

void ANPV::loadImage(QSharedPointer<SmartImageDecoder> dec)
{
    this->setWindowTitle(dec->fileInfo().fileName());
    d->imageViewer->loadImage(dec);
}

void ANPV::setThumbnailDir(QString str)
{
    d->thumbnailViewer->changeDir(str);
}

void ANPV::addBackgroundTask(int idx, const QFuture<DecodingState>& fut)
{
    xThreadGuard(this);
    QPointer w = new CancellableProgressWidget(fut, this, d->progressWidgetContainer);
    
    for (const auto& [key, value] : d->progressWidgetGroupMap)
    {
        if(!value.isNull() && value->isFinished())
        {
            value->hide();
        }
    }
    
    auto* old = d->progressWidgetGroupMap[idx].data();
    if(old != nullptr)
    {
        auto* oldLayout = d->progressWidgetLayout->replaceWidget(old, w.data());
        old->deleteLater();
        delete oldLayout;
    }
    else
    {
        d->progressWidgetLayout->addWidget(w.data());
    }
    
    d->progressWidgetGroupMap[idx] = w;
}

bool ANPV::shouldHideProgressWidget()
{
    int vis = 0;
    for (const auto& [key, value] : d->progressWidgetGroupMap)
    {
        if(!value.isNull() && value->isVisible())
        {
            vis++;
        }
    }
    return vis > 1;
}

void ANPV::moveFilesSlot(const QString& targetDir)
{
    if(targetDir.isEmpty())
    {
        return;
    }
    
    if(d->stackedLayout->currentWidget() == d->thumbnailViewer)
    {
        QList<QString> selectedFileNames;
        QString curDir;
        d->thumbnailViewer->getSelectedFiles(selectedFileNames, curDir);
        
        this->moveFilesSlot(selectedFileNames, curDir, targetDir);
    }
    else if(d->stackedLayout->currentWidget() == d->imageViewer)
    {
        QFileInfo info = d->imageViewer->currentFile();
        if(!info.filePath().isEmpty())
        {
            QList<QString> files;
            files.append(info.fileName());
            this->moveFilesSlot(files, info.absoluteDir().absolutePath(), targetDir);
        }
    }
}

void ANPV::moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir)
{
    MoveFileCommand* cmd = new MoveFileCommand(files, sourceDir, targetDir);
    
    connect(cmd, &MoveFileCommand::moveFailed, this, [&](QList<QPair<QString, QString>> failedFilesWithReason)
    {
        QMessageBox box(QMessageBox::Critical,
                    "Move operation failed",
                    "Some files could not be moved to the destination folder. See details below.",
                    QMessageBox::Ok,
                    this);
        
        QString details;
        for(int i=0; i<failedFilesWithReason.size(); i++)
        {
            QPair<QString, QString>& p = failedFilesWithReason[i];
            details += p.first;
            
            if(!p.second.isEmpty())
            {
                details += QString(": ") + p.second;
                details += "\n";
            }
        }
        box.setDetailedText(details);
        box.setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        box.exec();
    });
    
    d->undoStack->push(cmd);
}

ViewMode ANPV::viewMode()
{
    return d->viewMode;
}
