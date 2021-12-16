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
#include <QFileIconProvider>
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
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QTabWidget>
#include <QSvgRenderer>
#include <QDataStream>
#include <QFileDialog>

#include "DocumentView.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"
#include "MultiDocumentView.hpp"
#include "MainWindow.hpp"

class MyDisabledFileIconProvider : public QAbstractFileIconProvider
{
public:
    MyDisabledFileIconProvider() = default;
    ~MyDisabledFileIconProvider() override = default;
    
    // The default implementation of this function is so horribly slow. It spams the UI thread with a bunch of useless events.
    // Disable this, to make it use the icon(QAbstractFileIconProvider::IconType type) overload.
    QIcon icon(const QFileInfo &info) const override
    {
        return QIcon();
    }
};

// Calling QFileIconProvider from multiple threads concurrently leads to the famous "corrputed double linked list" in malloc and free
class MyUIThreadOnlyIconProvider : public QFileIconProvider
{
//     QCache<QString, QIcon> iconCache;
public:
    MyUIThreadOnlyIconProvider() = default;
    ~MyUIThreadOnlyIconProvider() override = default;
    
    QIcon icon(IconType type) const override
    {
        xThreadGuard g(QGuiApplication::instance()->thread());
        return this->QFileIconProvider::icon(type);
    }
    QIcon icon(const QFileInfo &info) const override
    {
        xThreadGuard g(QGuiApplication::instance()->thread());
        QIcon ico = this->QFileIconProvider::icon(info);
        return ico;
    }
    QString type(const QFileInfo &info) const override
    {
        xThreadGuard g(QGuiApplication::instance()->thread());
        return this->QFileIconProvider::type(info);
    }
    void setOptions(Options options) override
    {
        xThreadGuard g(QGuiApplication::instance()->thread());
        return this->QFileIconProvider::setOptions(options);
    }
    Options options() const override
    {
        xThreadGuard g(QGuiApplication::instance()->thread());
        return this->QFileIconProvider::options();
    }
};

static QPointer<ANPV> global;

struct ANPV::Impl
{
    ANPV* q;
    
    // normal objects without parent
    QScopedPointer<QAbstractFileIconProvider> iconProvider;
    QScopedPointer<MyDisabledFileIconProvider> noIconProvider;
    QPixmap noIconPixmap;
    QPixmap noPreviewPixmap;
    
    // QObjects without parent
    QScopedPointer<MainWindow, QScopedPointerDeleteLater> mainWindow;
    
    // QObjects with parent
    QFileSystemModel* dirModel = nullptr;
    QSharedPointer<SortedImageModel> fileModel = nullptr;
    QActionGroup* actionGroupFileOperation = nullptr;
    QUndoStack* undoStack = nullptr;
    
    QDir currentDir;
    ViewMode viewMode = ViewMode::Unknown;
    ViewFlags_t viewFlags = static_cast<ViewFlags_t>(ViewFlag::None);
    Qt::SortOrder sortOrder = static_cast<Qt::SortOrder>(-1);
    SortedImageModel::Column primarySortColumn = SortedImageModel::Column::Unknown;
    int iconHeight = -1;
    
    Impl(ANPV* parent) : q(parent)
    {
    }
    
    void initLogic()
    {
        if(::global.isNull())
        {
            ::global = QPointer<ANPV>(q);
        }
        
        this->iconProvider.reset(new MyUIThreadOnlyIconProvider());
        this->iconProvider->setOptions(QAbstractFileIconProvider::DontUseCustomDirectoryIcons);
        this->noIconProvider.reset(new MyDisabledFileIconProvider());
        
        this->dirModel = new QFileSystemModel(q);
        this->dirModel->setIconProvider(this->noIconProvider.get());
        this->dirModel->setRootPath("");
        this->dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
        
        this->fileModel.reset(new SortedImageModel(q));
        
        this->actionGroupFileOperation = new QActionGroup(q);
        this->undoStack = new QUndoStack(q);
    }
    
    void connectLogic()
    {
        connect(q, &ANPV::iconHeightChanged, q,
                [&](int, int)
                { 
                    this->drawNotFoundPixmap();
                    this->drawNoPreviewPixmap();
                });
        connect(q, &ANPV::currentDirChanged, q,
                [&](QDir,QDir)
                {
                    auto fut = this->fileModel->changeDirAsync(this->currentDir);
                    this->mainWindow->setBackgroundTask(fut);
                });
    }
    
    void writeSettings()
    {
        QSettings settings;

        settings.setValue("currentDir", q->currentDir().absolutePath());
        settings.setValue("viewMode", static_cast<int>(q->viewMode()));
        settings.setValue("viewFlags", q->viewFlags());
        settings.setValue("sortOrder", static_cast<int>(q->sortOrder()));
        settings.setValue("primarySortColumn", static_cast<int>(q->primarySortColumn()));
        settings.setValue("iconHeight", q->iconHeight());
        
        QByteArray actionsArray;
        {
            QDataStream out(&actionsArray, QIODeviceBase::WriteOnly);
            out.setVersion(QDataStream::Qt_6_2);
            QList<QAction*> actions = this->actionGroupFileOperation->actions();
            for(QAction* a : actions)
            {
                out << a->text();
                out << a->data();
                out << a->shortcut();
                out << a->shortcutContext();
            }
        }
        settings.setValue("actionGroupFileOperation", actionsArray);
    }

    void readSettings()
    {
        QSettings settings;

        q->setCurrentDir(settings.value("currentDir", qgetenv("HOME")).toString());
        q->setViewMode(static_cast<ViewMode>(settings.value("viewMode", static_cast<int>(ViewMode::Fit)).toInt()));
        q->setViewFlags(settings.value("viewFlags", static_cast<ViewFlags_t>(ViewFlag::ShowScrollBars)).toUInt());
        q->setSortOrder(static_cast<Qt::SortOrder>(settings.value("sortOrder", Qt::AscendingOrder).toInt()));
        q->setPrimarySortColumn(static_cast<SortedImageModel::Column>(settings.value("primarySortColumn", static_cast<int>(SortedImageModel::Column::FileName)).toInt()));
        q->setIconHeight(settings.value("iconHeight", 150).toInt());
        
        QByteArray actionsArray = settings.value("actionGroupFileOperation").toByteArray();
        if(!actionsArray.isEmpty())
        {
            QDataStream in(actionsArray);
            in.setVersion(QDataStream::Qt_6_2);
            QVariant data;
            QString text;
            QKeySequence seq;
            Qt::ShortcutContext ctx;
            while(!in.atEnd())
            {
                in >> text;
                in >> data;
                in >> seq;
                in >> ctx;
                QAction* action = new QAction(text, q);
                action->setData(data);
                action->setShortcut(seq);
                action->setShortcutContext(ctx);
                this->actionGroupFileOperation->addAction(action);
            }
        }
    }
    
    QPixmap renderSvg(QString resource)
    {
        QSvgRenderer renderer(resource);

        QSize imgSize = renderer.defaultSize().scaled(this->iconHeight, this->iconHeight, Qt::KeepAspectRatio);
        QImage image(imgSize, QImage::Format_ARGB32);
        image.fill(0);

        QPainter painter(&image);
        renderer.render(&painter);

        return QPixmap::fromImage(image);
    }
    
    void drawNotFoundPixmap()
    {
        this->noIconPixmap = this->renderSvg(QString(":/images/FileNotFound.svg"));
    }
    
    void drawNoPreviewPixmap()
    {
        this->noPreviewPixmap = this->renderSvg(QString(":/images/NoPreview.svg"));
    }
};

ANPV* ANPV::globalInstance()
{
    return global.get();
}

QString ANPV::formatByteHtmlString(float fsize)
{
    static const char *const sizeUnit[] = {" Bytes", " KiB", " MiB", " <b>GiB</b>"};

    unsigned i;
    for(i = 0; i<sizeof(sizeUnit)/sizeof(sizeUnit[0]) && fsize > 1024; i++)
    {
        fsize /= 1024.f;
    }
    
    return QString::number(fsize, 'f', 2) + sizeUnit[i];
}


ANPV::ANPV() : d(std::make_unique<Impl>(this))
{
    d->initLogic();
    d->readSettings();
    d->drawNotFoundPixmap();
    d->drawNoPreviewPixmap();
}

ANPV::ANPV(QSplashScreen *splash)
 : d(std::make_unique<Impl>(this))
{
    QCoreApplication::setOrganizationName("derselbst");
    QCoreApplication::setOrganizationDomain("");
    QCoreApplication::setApplicationName("ANPV");
    QApplication::setWindowIcon(QIcon(":/images/ANPV.ico"));

    splash->showMessage("Creating logic");
    d->initLogic();

    splash->showMessage("Connecting logic");
    d->connectLogic();
    
    splash->showMessage("Creating UI Widgets");
    d->mainWindow.reset(new MainWindow(splash));
    d->mainWindow->show();
    
    splash->showMessage("Reading latest settings");
    d->readSettings();
    d->mainWindow->readSettings();
    
    splash->showMessage("ANPV initialized, waiting for Qt-Framework getting it's events processed...");
    splash->finish(d->mainWindow.get());
}

ANPV::~ANPV()
{
    d->writeSettings();
}

QAbstractFileIconProvider* ANPV::iconProvider()
{
    return d->iconProvider.get();
}

QFileSystemModel* ANPV::dirModel()
{
    xThreadGuard g(this);
    return d->dirModel;
}

QSharedPointer<SortedImageModel> ANPV::fileModel()
{
    xThreadGuard g(this);
    return d->fileModel;
}

QDir ANPV::currentDir()
{
    xThreadGuard(this);
    return d->currentDir;
}

void ANPV::setCurrentDir(QString str)
{
    xThreadGuard(this);
    
    QDir old = d->currentDir;
    if(old != str)
    {
        d->currentDir = str;
        emit this->currentDirChanged(str, old);
    }
}

ViewMode ANPV::viewMode()
{
    xThreadGuard(this);
    return d->viewMode;
}

void ANPV::setViewMode(ViewMode v)
{
    xThreadGuard(this);
    ViewMode old = d->viewMode;
    if(old != v)
    {
        d->viewMode = v;
        emit this->viewModeChanged(v, old);
    }
}

ViewFlags_t ANPV::viewFlags()
{
    xThreadGuard(this);
    return d->viewFlags;
}

void ANPV::setViewFlags(ViewFlags_t newFlags)
{
    xThreadGuard(this);
    ViewFlags_t old = d->viewFlags;
    if(old != newFlags)
    {
        d->viewFlags = newFlags;
        emit this->viewFlagsChanged(newFlags, old);
    }
}

void ANPV::setViewFlag(ViewFlag v, bool on)
{
    xThreadGuard(this);
    ViewFlags_t newFlags = d->viewFlags;
    
    if(on)
    {
        newFlags |= static_cast<ViewFlags_t>(v);
    }
    else
    {
        newFlags &= ~static_cast<ViewFlags_t>(v);
    }
    this->setViewFlags(newFlags);
}

Qt::SortOrder ANPV::sortOrder()
{
    xThreadGuard(this);
    return d->sortOrder;
}

void ANPV::setSortOrder(Qt::SortOrder order)
{
    xThreadGuard(this);
    Qt::SortOrder old = d->sortOrder;
    if(order != old)
    {
        d->sortOrder = order;
        emit this->sortOrderChanged(order, old);
    }
}

SortedImageModel::Column ANPV::primarySortColumn()
{
    xThreadGuard(this);
    return d->primarySortColumn;
}

void ANPV::setPrimarySortColumn(SortedImageModel::Column col)
{
    xThreadGuard(this);
    SortedImageModel::Column old = d->primarySortColumn;
    if(old != col)
    {
        d->primarySortColumn = col;
        emit this->primarySortColumnChanged(col, old);
    }
}

int ANPV::iconHeight()
{
    xThreadGuard(this);
    return d->iconHeight;
}

void ANPV::setIconHeight(int h)
{
    xThreadGuard(this);
    int old = d->iconHeight;
    h = std::min(h, ANPV::MaxIconHeight);
    if(old != h)
    {
        d->iconHeight = h;
        emit iconHeightChanged(h, old);
    }
}

void ANPV::showThumbnailView(QSharedPointer<Image> img)
{
    xThreadGuard(this);
    d->mainWindow->setWindowState( (d->mainWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    d->mainWindow->raise();
    d->mainWindow->activateWindow();
}

void ANPV::openImages(const QList<QSharedPointer<Image>>& image)
{
    xThreadGuard(this);
    MultiDocumentView* mdv = new MultiDocumentView(d->mainWindow.get());
    mdv->show();
    mdv->addImages(image, d->fileModel);
}

QPixmap ANPV::noIconPixmap()
{
    return d->noIconPixmap;
}

QPixmap ANPV::noPreviewPixmap()
{
    return d->noPreviewPixmap;
}

QActionGroup* ANPV::copyMoveActionGroup()
{
    xThreadGuard(this);
    return d->actionGroupFileOperation;
}

QUndoStack* ANPV::undoStack()
{
    xThreadGuard(this);
    return d->undoStack;
}

void ANPV::moveFiles(QList<QString>&& files, QString&& source, QString&& destination)
{
    xThreadGuard(this);
    MoveFileCommand* cmd = new MoveFileCommand(std::move(files), std::move(source), std::move(destination));
    
    connect(cmd, &MoveFileCommand::moveFailed, this, [&](QList<QPair<QString, QString>> failedFilesWithReason)
    {
        QMessageBox box(QMessageBox::Critical,
                    "Move operation failed",
                    "Some files could not be moved to the destination folder. See details below.",
                    QMessageBox::Ok,
                    d->mainWindow.get());
        
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
    
    this->undoStack()->push(cmd);
}

QString ANPV::getExistingDirectory(QWidget* parent, QString& proposedDirToOpen)
{
    xThreadGuard(this);
    QString dirToOpen = proposedDirToOpen.isEmpty() ? this->currentDir().absolutePath() : proposedDirToOpen;
    
    static const QStringList schemes = QStringList(QStringLiteral("file"));
    
    QFileDialog diag(parent, "Select Target Directory", dirToOpen);
    diag.setSupportedSchemes(schemes);
    diag.setFileMode(QFileDialog::Directory);
    diag.setViewMode(QFileDialog::List);
    diag.setIconProvider(d->noIconProvider.get());
    diag.setOptions(QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseCustomDirectoryIcons);
    
    QString dir;
    if (diag.exec() == QDialog::Accepted)
    {
        dir = diag.selectedFiles().value(0);
        proposedDirToOpen = dir;
    }
    
    return dir;
}

void ANPV::about()
{
    xThreadGuard g(this);
    // build up the huge constexpr about anmp string
    static constexpr char text[] = "<p>\n"
                                   "<b>ANPV - Another Nameless Picture Viewer</b><br />\n"
                                   "<br />\n"
                                   "Version: " ANPV_VERSION "<br />\n"
                                   "<br />\n"
    "Website: <a href=\"https://github.com/derselbst/ANPV\">https://github.com/derselbst/ANPV</a><br />\n"
    "<br />\n"
    "<small>"
    "&copy;Tom Moebert (derselbst)<br />\n"
    "<br />\n"
    "This program is free software; you can redistribute it and/or modify it"
    "<br />\n"
    "under the terms of the GNU Affero Public License version 3."
    "</small>"
    "</p>\n";

    QMessageBox::about(d->mainWindow.get(), "About ANPV", text);
}
