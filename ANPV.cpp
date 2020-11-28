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

#include "DocumentView.hpp"
#include "ThumbnailView.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"


struct ANPV::Impl
{
    ANPV* p;
    
    QProgressBar* progressBar;
    QStackedLayout* stackedLayout;
    QWidget *stackedLayoutWidget;
    DocumentView* imageViewer;
    ThumbnailView* thumbnailViewer;
    
    SortedImageModel* fileModel;
    
    Impl(ANPV* parent) : p(parent)
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
            [&](QString current)
            {
                QFileInfo i = d->fileModel->goNext(current);
                this->loadImage(i.absoluteFilePath());
                this->setThumbnailDir(i.absoluteDir().absolutePath());
            });
    connect(d->imageViewer, &DocumentView::requestPrev, this,
            [&](QString current)
            {
                QFileInfo i = d->fileModel->goPrev(current);
                this->loadImage(i.absoluteFilePath());
                this->setThumbnailDir(i.absoluteDir().absolutePath());
            });

    d->stackedLayout = new QStackedLayout(this);
    d->stackedLayout->addWidget(d->thumbnailViewer);
    d->stackedLayout->addWidget(d->imageViewer);
    
    d->stackedLayoutWidget = new QWidget(this);
    d->stackedLayoutWidget->setLayout(d->stackedLayout);
    this->setCentralWidget(d->stackedLayoutWidget);
    
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

void ANPV::loadImage(QString str)
{
    d->imageViewer->loadImage(str);
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
