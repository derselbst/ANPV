#include "ANPV.hpp"

#include <QMainWindow>
#include <QProgressBar>
#include <QStackedLayout>
#include <QWidget>
#include <QSplashScreen>
#include <QStatusBar>
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

#include "DocumentView.hpp"
#include "ThumbnailView.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"


struct ANPV::Impl
{
    ANPV* q;
    
    QUndoStack* undoStack;
    QProgressBar* progressBar;
    QStackedLayout* stackedLayout;
    QWidget *stackedLayoutWidget;
    DocumentView* imageViewer;
    ThumbnailView* thumbnailViewer;
    
    SortedImageModel* fileModel;
    
    QMenu* menuFile;
    QMenu* menuEdit;
    QMenu* menuSort;
    
    QAction* actionSortFileName;
    QAction* actionSortFileSize;
    QActionGroup* actionGroupSortColumn;
    QActionGroup* actionGroupSortOrder;
    
    QAction *actionUndo;
    QAction *actionRedo;
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

        actionExit = new QAction("E&xit", q);
        actionExit->setShortcuts(QKeySequence::Quit);
        connect(actionExit, &QAction::triggered, q, &ANPV::close);

    }
    
    void createMenus()
    {
        menuFile = q->menuBar()->addMenu("&File");
        menuFile->addAction(actionExit);

        menuEdit = q->menuBar()->addMenu("&Edit");
        menuEdit->addAction(actionUndo);
        menuEdit->addAction(actionRedo);
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
    
    d->progressBar = new QProgressBar(this);
    d->progressBar->setMinimum(0);
    d->progressBar->setMaximum(100);
    this->statusBar()->addPermanentWidget(d->progressBar);
    
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
    
    this->notifyDecodingState(DecodingState::Ready);
}

ANPV::~ANPV() = default;


void ANPV::showImageView()
{
    d->stackedLayout->setCurrentWidget(d->imageViewer);
}

void ANPV::showThumbnailView()
{
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

void ANPV::notifyProgress(int progress, QString message)
{
    this->statusBar()->showMessage(message, 0);
    this->notifyProgress(progress);
}

void ANPV::notifyProgress(int progress)
{
    d->progressBar->setValue(progress);
}

void ANPV::notifyDecodingState(DecodingState state)
{
    d->progressBar->setStyleSheet(d->getProgressStyle(state));
}

void ANPV::executeMoveCommand(MoveFileCommand* cmd)
{
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
//     connect(cmd, &MoveFileCommand::moveSucceeded, [&](QList<QString> filesSucceeded) {});
    
    d->undoStack->push(cmd);
}
