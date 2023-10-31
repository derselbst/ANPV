#include "ANPV.hpp"

#include <QWidget>
#include <QSplashScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QPixmap>
#include <QFileIconProvider>
#include <QtDebug>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemModel>
#include <QActionGroup>
#include <QAction>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QSvgRenderer>
#include <QDataStream>
#include <QFileDialog>
#include <QMimeData>

#include <atomic>

#include "DocumentView.hpp"
#include "DecoderFactory.hpp"
#include "Image.hpp"
#include "Formatter.hpp"
#include "SortedImageModel.hpp"
#include "SmartImageDecoder.hpp"
#include "HardLinkFileCommand.hpp"
#include "MoveFileCommand.hpp"
#include "FileOperationConfigDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "xThreadGuard.hpp"
#include "MultiDocumentView.hpp"
#include "MainWindow.hpp"
#include "WaitCursor.hpp"
#include "ProgressIndicatorHelper.hpp"

class MyDisabledFileIconProvider : public QAbstractFileIconProvider
{
public:
    MyDisabledFileIconProvider() = default;
    ~MyDisabledFileIconProvider() override = default;
    
    // The default implementation of this function is so horribly slow. It spams the UI thread with a bunch of useless events.
    // Disable this, to make it use the icon(QAbstractFileIconProvider::IconType type) overload.
    QIcon icon(const QFileInfo &) const override
    {
        return QIcon();
    }

    QString type(const QFileInfo& info) const override
    {
        if (info.isFile())
        {
            // The base implementation would try to determine the mime type in this case, which means, that it has to open the file,
            // which in turn is incredibly slow when performed on Windows on a network share.
            return QGuiApplication::translate("QAbstractFileIconProvider", "File");
        }
        return this->QAbstractFileIconProvider::type(info);
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
        return this->QFileIconProvider::icon(type);
    }
    QIcon icon(const QFileInfo &info) const override
    {
        QIcon ico = this->QFileIconProvider::icon(info);
        return ico;
    }
    QString type(const QFileInfo &info) const override
    {
        return this->QFileIconProvider::type(info);
    }
    void setOptions(Options options) override
    {
        return this->QFileIconProvider::setOptions(options);
    }
    Options options() const override
    {
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
    QPointer<QFileSystemModel> dirModel;
    QPointer<SortedImageModel> fileModel;
    QPointer<QActionGroup> actionGroupFileOperation;
    QPointer<QActionGroup> actionGroupViewMode;
    QPointer<QActionGroup> actionGroupViewFlag;
    QPointer<QAction> actionOpen;
    QString lastOpenImageDir;
    QPointer<QAction> actionExit;
    QPointer<QUndoStack> undoStack;
    QPointer<QThread> backgroundThread;
    QPointer<ProgressIndicatorHelper> spinningIconHelper;
    QPointer<QSettings> globalSettings;
    
    // Use a simple string for the currentDir, because:
    // QDir lacks a "null" value, as it defaults to the current working directory
    // And
    // QFileInfo sometimes implicitly takes the parent directory, even if the path passed in already is a directory
    QString currentDir;
    ViewMode viewMode = ViewMode::Unknown;
    ViewFlags_t viewFlags = static_cast<ViewFlags_t>(ViewFlag::None);
    Qt::SortOrder imageSortOrder = static_cast<Qt::SortOrder>(-1);
    Qt::SortOrder sectionSortOrder = static_cast<Qt::SortOrder>(-1);
    SortField imageSortField = SortField::None;
    SortField sectionSortField = SortField::None;
    
    std::atomic<int> iconHeight{ -1 };
    
    Impl(ANPV* parent) : q(parent)
    {
    }
    
    void initLogic()
    {
        if(::global.isNull())
        {
            ::global = QPointer<ANPV>(q);
        }

        this->backgroundThread = (new QThread(q));
        this->backgroundThread->setObjectName("Background Thread");
        backgroundThread->start(QThread::NormalPriority);
        connect(qGuiApp, &QApplication::lastWindowClosed, this->backgroundThread.get(), &QThread::quit);
        

        connect(QApplication::instance(), &QApplication::aboutToQuit, q, 
            [&]()
            {
                qInfo() << "Abouttoquit!!!";
            });


        connect(this->backgroundThread.get(), &QThread::finished, q,
            [&]()
            {
                qInfo() << "Qthread::finished()";
            });


        this->iconProvider.reset(new MyUIThreadOnlyIconProvider());
        this->iconProvider->setOptions(QAbstractFileIconProvider::DontUseCustomDirectoryIcons);
        this->noIconProvider.reset(new MyDisabledFileIconProvider());
        
        this->dirModel = new QFileSystemModel(q);
        this->dirModel->setIconProvider(this->noIconProvider.get());
        this->dirModel->setRootPath("");
        this->dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
        
        this->fileModel = new SortedImageModel(q);
        connect(qGuiApp, &QGuiApplication::lastWindowClosed, q,
            [&]()
            {
                this->fileModel->cancelAllBackgroundTasks();
                qInfo() << "lastWindowClose!";
            });
        
        this->actionGroupFileOperation = new QActionGroup(q);
        this->actionExit = new QAction("Close", q);
        this->actionExit->setShortcuts(QKeySequence::Quit);
        this->actionExit->setShortcutContext(Qt::ApplicationShortcut);
        connect(this->actionExit, &QAction::triggered, q,
                [&]()
                {
                    QString pretty = QKeySequence(QKeySequence::Quit).toString();
                    if (QMessageBox::Yes == QMessageBox::question(QApplication::focusWidget(), "Close Confirmation", QString("%1 was hit, exit?").arg(pretty), QMessageBox::Yes | QMessageBox::No, QMessageBox::No))
                    {
                        QApplication::closeAllWindows();
                    }
                });
        this->actionOpen = new QAction("Open Image", q);
        this->actionOpen->setShortcut({Qt::CTRL | Qt::Key_O});
        this->actionOpen->setShortcutContext(Qt::ApplicationShortcut);
        connect(this->actionOpen, &QAction::triggered, q,
                [&]()
                {
                    QList<QString> files = q->getExistingFile(QApplication::focusWidget(), lastOpenImageDir);
                    
                    QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> images;
                    images.reserve(files.size());
                    for(auto& f : files)
                    {
                        images.emplace_back(DecoderFactory::globalInstance()->makeImage(QFileInfo(f)), this->fileModel->dataContainer());
                    }
                    q->openImages(images);
                });

        actionGroupViewMode = new QActionGroup(q);
        auto makeViewModeAction = [&](const QString& text, ViewMode view)
        {
            QAction* action = new QAction(text, actionGroupViewMode);
            action->setCheckable(true);
            action->setShortcutContext(Qt::ApplicationShortcut);
            connect(action, &QAction::triggered, q, [=](bool)
                { q->setViewMode(view); });
            connect(q, &ANPV::viewModeChanged, action,
                [=](ViewMode newMode, ViewMode)
                {
                    // trigger action to ensure proper selection
                    action->setChecked(newMode == view);
                });
            return action;
        };

        QAction* actionNo_Change = makeViewModeAction(QStringLiteral("No Change"), ViewMode::None);
        actionNo_Change->setToolTip(QStringLiteral("Do not change the scale, rotation or transformation when switching between images."));
        actionNo_Change->setStatusTip(actionNo_Change->toolTip());
        actionNo_Change->setShortcut({ Qt::Key_F3 });

        QAction* actionFit_in_FOV = makeViewModeAction(QStringLiteral("Fit in FOV"), ViewMode::Fit);
        actionFit_in_FOV->setToolTip(QStringLiteral("When switching between images, fit the entire image into the Field Of View, i.e. the available space of the window."));
        actionFit_in_FOV->setStatusTip(actionFit_in_FOV->toolTip());
        actionFit_in_FOV->setShortcut({ Qt::Key_F4 });

        actionGroupViewFlag = new QActionGroup(q);
        actionGroupViewFlag->setExclusive(false);
        auto makeViewFlagAction = [&](const QString& text, ViewFlag v)
        {
            QAction* action = new QAction(text, actionGroupViewFlag);
            action->setCheckable(true);
            action->setShortcutContext(Qt::ApplicationShortcut);
            connect(action, &QAction::triggered, q, [=](bool isChecked)
                { q->setViewFlag(v, isChecked); });
            connect(q, &ANPV::viewFlagsChanged, action,
                [=](ViewFlags_t newMode, ViewFlags_t)
                {
                    action->setChecked((newMode & static_cast<ViewFlags_t>(v)) != 0);
                });
            return action;
        };

        QAction* actionCombine_RAWs_and_JPGs = makeViewFlagAction(QStringLiteral("Combine RAWs and JPGs"), ViewFlag::CombineRawJpg);
        actionCombine_RAWs_and_JPGs->setToolTip(QStringLiteral("If a RAW file (e.g. .CR2 or .NEF) has a similar named .JPG file, only display the JPG and hide the RAWs."));
        actionCombine_RAWs_and_JPGs->setStatusTip(actionCombine_RAWs_and_JPGs->toolTip());
        actionCombine_RAWs_and_JPGs->setShortcut({ Qt::Key_F6 });

        QAction* actionShow_AF_Points = makeViewFlagAction(QStringLiteral("Show AF Points"), ViewFlag::ShowAfPoints);
        actionShow_AF_Points->setToolTip(QStringLiteral("Shows the AutoFocus Points available in the EXIF metadata. Currently only supported for Canon cameras."));
        actionShow_AF_Points->setStatusTip(actionShow_AF_Points->toolTip());
        actionShow_AF_Points->setShortcut({ Qt::Key_F7 });

        QAction* actionCenter_AF_focus_point = makeViewFlagAction(QStringLiteral("Center around AF focus point"), ViewFlag::CenterAf);
        actionCenter_AF_focus_point->setToolTip(QStringLiteral("This will preserve the zoom factor, while making sure to transpose the image so that the AF points which are \"in-focus\" are located in the center of the FOV. If no AF data is available, no transposing takes place."));
        actionCenter_AF_focus_point->setStatusTip(actionCenter_AF_focus_point->toolTip());
        actionCenter_AF_focus_point->setShortcut({ Qt::Key_F8 });

        QAction* actionRespect_EXIF_orientation = makeViewFlagAction(QStringLiteral("Respect EXIF orientation"), ViewFlag::RespectExifOrientation);
        actionRespect_EXIF_orientation->setToolTip(QStringLiteral("Automatically rotates the image as indicated by the EXIF metadata.If no such information is available, landscape orientation will be used by default."));
        actionRespect_EXIF_orientation->setStatusTip(actionRespect_EXIF_orientation->toolTip());

        this->undoStack = new QUndoStack(q);
        this->globalSettings = new QSettings(QSettings::UserScope, q);
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
                [&](QString,QString)
                {
                    auto fut = this->fileModel->changeDirAsync(this->currentDir);
                    this->mainWindow->setBackgroundTask(fut);
                });
    }
    
    void writeSettings()
    {
        auto& settings = q->settings();

        QString curDir = q->currentDir();
        if(!curDir.isEmpty())
        {
            settings.setValue("currentDir", curDir);
        }
        else
        {
            qDebug() << "ANPV::writeSettings(): currentDir is empty, skipping";
        }
        settings.setValue("viewMode", static_cast<int>(q->viewMode()));
        settings.setValue("viewFlags", q->viewFlags());
        settings.setValue("imageSortOrder", static_cast<int>(q->imageSortOrder()));
        settings.setValue("sectionSortOrder", static_cast<int>(q->sectionSortOrder()));
        settings.setValue("imageSortField", static_cast<int>(q->imageSortField()));
        settings.setValue("sectionSortField", static_cast<int>(q->sectionSortField()));
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
        auto& settings = q->settings();

        q->setViewMode(static_cast<ViewMode>(settings.value("viewMode", static_cast<int>(ViewMode::Fit)).toInt()));
        q->setViewFlags(settings.value("viewFlags", static_cast<ViewFlags_t>(ViewFlag::ShowScrollBars)).toUInt());
        q->setImageSortOrder(static_cast<Qt::SortOrder>(settings.value("imageSortOrder", Qt::AscendingOrder).toInt()));
        q->setSectionSortOrder(static_cast<Qt::SortOrder>(settings.value("sectionSortOrder", Qt::AscendingOrder).toInt()));
        q->setImageSortField(static_cast<SortField>(settings.value("imageSortField", static_cast<int>(SortField::FileName)).toInt()));
        q->setSectionSortField(static_cast<SortField>(settings.value("sectionSortField", static_cast<int>(SortField::None)).toInt()));
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
        if (!image.isNull())
        {
            image.fill(0);

            QPainter painter(&image);
            renderer.render(&painter);
        }
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
    
    // https://api.kde.org/frameworks/kcoreaddons/html/kurlmimedata_8cpp_source.html#l00029
    static QString kdeUriListMime()
    {
        return QStringLiteral("application/x-kde4-urilist");
    }
    
    static QString kdeCutMime()
    {
        return QStringLiteral("application/x-kde-cutselection");
    }

    static QByteArray uriListData(const QList<QUrl> &urls)
    {
        // compatible with qmimedata.cpp encoding of QUrls
        QByteArray result;
        for (int i = 0; i < urls.size(); ++i)
        {
            result += urls.at(i).toEncoded();
            result += "\r\n";
        }
        return result;
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
    d->spinningIconHelper = new ProgressIndicatorHelper(d->mainWindow.get());

    
    splash->showMessage("Reading latest settings");
    d->readSettings();
    d->mainWindow->readSettings();
    
    splash->showMessage("ANPV initialized, waiting for Qt-Framework getting it's events processed...");
}

ANPV::~ANPV()
{
    if(d->fileModel)
    {
        d->fileModel->cancelAllBackgroundTasks();
    }
    d->backgroundThread->quit();
    if(!d->backgroundThread->wait(5000))
    {
        qCritical() << "backgroundThread did not terminate within time. Terminating forcefully!";
        d->backgroundThread->terminate();
    }
    d->writeSettings();
}

QAbstractFileIconProvider* ANPV::iconProvider()
{
    return d->iconProvider.get();
}

QThread* ANPV::backgroundThread()
{
    Q_ASSERT(d->backgroundThread != nullptr);
    xThreadGuard g(this);
    return d->backgroundThread.get();
}

QSettings& ANPV::settings()
{
    return *d->globalSettings;
}

QFileSystemModel* ANPV::dirModel()
{
    xThreadGuard g(this);
    return d->dirModel;
}

QPointer<SortedImageModel> ANPV::fileModel()
{
    xThreadGuard g(this);
    return d->fileModel;
}

QString ANPV::currentDir()
{
    xThreadGuard g(this);
    return d->currentDir;
}

void ANPV::setCurrentDir(const QString& str)
{
    this->setCurrentDir(str, false);
}
void ANPV::setCurrentDir(const QString& str, bool force)
{
    xThreadGuard g(this);
    
    QDir old(d->currentDir);
    QDir newDir(str);
    
    if(!newDir.exists())
    {
        qWarning() << "ANPV::setCurrentDir(): ignoring non-existing new directory " << newDir;
        return;
    }

    if(d->currentDir != str || force)
    {
        auto model = this->fileModel();
        if (model && !model->isSafeToChangeDir())
        {
            QMessageBox::StandardButton reply = QMessageBox::question(d->mainWindow.get(), "Changing directory will discard check selection", "You have checked images recently. Changing the directory now will discard any selection. Are you sure to change directory?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::No)
            {
                return;
            }
        }

        d->currentDir = newDir.absolutePath();
        emit this->currentDirChanged(d->currentDir, old.absolutePath());
    }
}

QString ANPV::savedCurrentDir()
{
    xThreadGuard g(this);
    return this->settings().value("currentDir", qgetenv("HOME")).toString();
}

void ANPV::fixupAndSetCurrentDir(QString str)
{
    xThreadGuard g(this);

    QDir fixedDir;
    QDir wantedDir(str);
    if (wantedDir.exists() && wantedDir.isReadable())
    {
        fixedDir = wantedDir;
    }
    else
    {
        QString absoluteWantedPath = wantedDir.absolutePath();
        while(true)
        {
            absoluteWantedPath += "/..";
            QDir parent(absoluteWantedPath);
            absoluteWantedPath = parent.absolutePath();
            // we must clean up the QDir before asking whether it exists
            parent = QDir(absoluteWantedPath);
            if(parent.exists())
            {
                fixedDir = parent;
                goto ok;
            }
            if(parent.isRoot())
            {
                break;
            }
        }

        wantedDir = QDir::home();
        if (wantedDir.exists() && wantedDir.isReadable())
        {
            fixedDir = wantedDir;
        }
        else
        {
            fixedDir = QDir::root();
        }
ok:
        QString text = QStringLiteral(
            "ANPV was unable to access the last opened directory:\n\n"
            "%1\n\n"
            "Using %2 instead."
        ).arg(str).arg(fixedDir.absolutePath());
        QMessageBox::information(nullptr, "Unable to restore last directory", text);
    }

    this->setCurrentDir(fixedDir.absolutePath());
}

ViewMode ANPV::viewMode()
{
    xThreadGuard g(this);
    return d->viewMode;
}

void ANPV::setViewMode(ViewMode v)
{
    xThreadGuard g(this);
    ViewMode old = d->viewMode;
    if(true) // always emit, to allow user to press F4 to fit image again
    {
        d->viewMode = v;
        emit this->viewModeChanged(v, old);
    }
}

ViewFlags_t ANPV::viewFlags()
{
    xThreadGuard g(this);
    return d->viewFlags;
}

void ANPV::setViewFlags(ViewFlags_t newFlags)
{
    xThreadGuard g(this);
    ViewFlags_t old = d->viewFlags;
    if(old != newFlags)
    {
        d->viewFlags = newFlags;
        emit this->viewFlagsChanged(newFlags, old);
    }
}

void ANPV::setViewFlag(ViewFlag v, bool on)
{
    xThreadGuard g(this);
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

Qt::SortOrder ANPV::imageSortOrder()
{
    xThreadGuard g(this);
    return d->imageSortOrder;
}

void ANPV::setImageSortOrder(Qt::SortOrder order)
{
    xThreadGuard g(this);
    Qt::SortOrder old = d->imageSortOrder;
    if (order != old)
    {
        d->imageSortOrder = order;
        emit this->imageSortOrderChanged(d->imageSortField, order, d->imageSortField, old);
    }
}

SortField ANPV::imageSortField()
{
    xThreadGuard g(this);
    return d->imageSortField;
}

void ANPV::setImageSortField(SortField field)
{
    xThreadGuard g(this);
    SortField old = d->imageSortField;
    if (field != old)
    {
        d->imageSortField = field;
        emit this->imageSortOrderChanged(field, d->imageSortOrder, old, d->imageSortOrder);
    }
}

Qt::SortOrder ANPV::sectionSortOrder()
{
    xThreadGuard g(this);
    return d->sectionSortOrder;
}

void ANPV::setSectionSortOrder(Qt::SortOrder order)
{
    xThreadGuard g(this);
    Qt::SortOrder old = d->sectionSortOrder;
    if (order != old)
    {
        d->sectionSortOrder = order;
        emit this->sectionSortOrderChanged(d->sectionSortField, order, d->sectionSortField, old);
    }
}

SortField ANPV::sectionSortField()
{
    xThreadGuard g(this);
    return d->sectionSortField;
}

void ANPV::setSectionSortField(SortField field)
{
    xThreadGuard g(this);
    SortField old = d->sectionSortField;
    if (field != old)
    {
        d->sectionSortField = field;
        emit this->sectionSortOrderChanged(field, d->sectionSortOrder, old, d->sectionSortOrder);
    }
}

int ANPV::iconHeight()
{
    // accessed by UI Thread, ThreadPool, and maybe elsewhere
    return d->iconHeight;
}

void ANPV::setIconHeight(int h)
{
    xThreadGuard g(this);
    int old = d->iconHeight;
    h = std::clamp(h, 0, ANPV::MaxIconHeight);
    if(old != h)
    {
        d->iconHeight = h;
        emit iconHeightChanged(h, old);
    }
}

// called by multiple threads
ProgressIndicatorHelper* ANPV::spinningIconHelper()
{
    Q_ASSERT(d->spinningIconHelper != nullptr);
    return d->spinningIconHelper;
}

void ANPV::showThumbnailView()
{
    xThreadGuard g(this);
    d->mainWindow->show();
    d->mainWindow->setWindowState( (d->mainWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    d->mainWindow->raise();
    d->mainWindow->activateWindow();
}

void ANPV::showThumbnailView(QSplashScreen* splash)
{
    this->showThumbnailView();
    splash->finish(d->mainWindow.get());
}

void ANPV::openImages(const QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>>& image)
{
    xThreadGuard g(this);
    if(image.isEmpty())
    {
        qDebug() << "ANPV::openImages() received an empty list, ignoring.";
        return;
    }
    WaitCursor w;
    MultiDocumentView* mdv = new MultiDocumentView(d->mainWindow.get());
    mdv->addImages(image);
    mdv->show();
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
    xThreadGuard g(this);
    return d->actionGroupFileOperation;
}

QActionGroup* ANPV::viewModeActionGroup()
{
    xThreadGuard g(this);
    return d->actionGroupViewMode;
}

QActionGroup* ANPV::viewFlagActionGroup()
{
    xThreadGuard g(this);
    return d->actionGroupViewFlag;
}

QUndoStack* ANPV::undoStack()
{
    xThreadGuard g(this);
    return d->undoStack;
}

void ANPV::hardLinkFiles(QList<QString>&& fileNames, QString&& source, QString&& destination)
{
    xThreadGuard g(this);
    HardLinkFileCommand* cmd = new HardLinkFileCommand(std::move(fileNames), std::move(source), std::move(destination));
    
    connect(cmd, &HardLinkFileCommand::failed, this, [&](QList<QPair<QString, QString>> failedFilesWithReason)
    {
        QMessageBox box(QMessageBox::Critical,
                    "Hardlink operation failed",
                    "Some files could not be hardlinked to the destination folder. See details below.",
                    QMessageBox::Ok,
                    QApplication::focusWidget());
        
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
    }, Qt::QueuedConnection); // use queued connection, to avoid displaying the WaitCursor when operation failed
    
    this->undoStack()->push(cmd);
}

void ANPV::moveFiles(QList<QString>&& fileNames, QString&& source, QString&& destination)
{
    xThreadGuard g(this);
    MoveFileCommand* cmd = new MoveFileCommand(std::move(fileNames), std::move(source), std::move(destination));
    
    connect(cmd, &MoveFileCommand::failed, this, [&](QList<QPair<QString, QString>> failedFilesWithReason)
    {
        QMessageBox box(QMessageBox::Critical,
            "Move operation failed",
            "Some files could not be moved to the destination folder. See details below.",
            QMessageBox::Ok,
            QApplication::focusWidget());

        QString details;
        for (int i = 0; i < failedFilesWithReason.size(); i++)
        {
            QPair<QString, QString>& p = failedFilesWithReason[i];
            details += p.first;

            if (!p.second.isEmpty())
            {
                details += QString(": ") + p.second;
                details += "\n";
            }
        }
        box.setDetailedText(details);
        box.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        box.exec();
    }, Qt::QueuedConnection); // use queued connection, to avoid displaying the WaitCursor when operation failed
    
    this->undoStack()->push(cmd);
}

void ANPV::setClipboardDataCut(QMimeData *mimeData, bool cut)
{
    const QByteArray cutSelectionData = cut ? "1" : "0";
    mimeData->setData(Impl::kdeCutMime(), cutSelectionData);

    // Windows specific implementation: https://stackoverflow.com/a/47445073
    const int dropEffect = cut ? 2 : 5; // 2 for cut and 5 for copy
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << dropEffect;
    mimeData->setData("Preferred DropEffect", data);
}

void ANPV::setUrls(QMimeData *mimeData, const QList<QUrl> &localUrls)
{
    // Export the most local urls as text/uri-list and plain text, for non KDE apps.
    mimeData->setUrls(localUrls); // set text/uri-list and text/plain

    // Export the real KIO urls as a kde-specific mimetype
    mimeData->setData(Impl::kdeUriListMime(), Impl::uriListData(localUrls));
}

QList<QString> ANPV::getExistingFile(QWidget* parent, QString& proposedDirToOpen)
{
    xThreadGuard g(this);
    QString dirToOpen = proposedDirToOpen.isEmpty() ? this->currentDir() : proposedDirToOpen;
    
    QFileDialog diag(parent, "Select Target Directory", dirToOpen);
    diag.setFileMode(QFileDialog::ExistingFiles);
    diag.setViewMode(QFileDialog::Detail);
    diag.setIconProvider(d->noIconProvider.get());
    diag.setOptions(QFileDialog::DontUseCustomDirectoryIcons);
    
    if (diag.exec() == QDialog::Accepted)
    {
        proposedDirToOpen = QFileInfo(diag.selectedFiles().value(0)).absolutePath();
        return diag.selectedFiles();
    }
    return {};
}

QString ANPV::getExistingDirectory(QWidget* parent, QString& proposedDirToOpen)
{
    xThreadGuard g(this);
    QString dirToOpen = proposedDirToOpen.isEmpty() ? this->currentDir() : proposedDirToOpen;
    
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

QAction* ANPV::actionOpen()
{
    return d->actionOpen;
}

QAction* ANPV::actionExit()
{
    return d->actionExit;
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
