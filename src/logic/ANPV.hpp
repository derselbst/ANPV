
#pragma once

#include <memory>
#include <QDir>
#include <QObject>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>
#include <QProperty>

#include "DecodingState.hpp"
#include "SortedImageModel.hpp"
#include "types.hpp"

class MoveFileCommand;
class TomsSplash;
class QFileSystemModel;
class QActionGroup;
class QUndoStack;
class SortedImageModel;
class QAbstractFileIconProvider;
class SmartImageDecoder;
class CancellableProgressWidget;
class QMimeData;
class QAction;
class ProgressIndicatorHelper;
class QThread;
class QSettings;
class QThreadPool;

template<typename T>
class QFuture;


class ANPV : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode BINDABLE bindableViewMode)
    Q_PROPERTY(ViewFlags_t viewFlags READ viewFlags WRITE setViewFlags BINDABLE bindableViewFlags)
    Q_PROPERTY(Qt::SortOrder imageSortOrder READ imageSortOrder WRITE setImageSortOrder BINDABLE bindableImageSortOrder)
    Q_PROPERTY(SortField imageSortField READ imageSortField WRITE setImageSortField BINDABLE bindableImageSortField)
    Q_PROPERTY(Qt::SortOrder sectionSortOrder READ sectionSortOrder WRITE setSectionSortOrder BINDABLE bindableSectionSortOrder)
    Q_PROPERTY(SortField sectionSortField READ sectionSortField WRITE setSectionSortField BINDABLE bindableSectionSortField)

public:
    static constexpr int MaxIconHeight = 500/*px*/;
    static ANPV *globalInstance();
    static QString formatByteHtmlString(float fsize);
    static void setClipboardDataCut(QMimeData *mimeData, bool cut);
    static void setUrls(QMimeData *mimeData, const QList<QUrl> &localUrls);

    ANPV();
    ANPV(TomsSplash *splash);
    ~ANPV() override;

    QThread *backgroundThread();
    QThreadPool* threadPool();
    QSettings &settings();

    void openImages(const QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> &);
    bool showThumbnailView();
    void showThumbnailView(TomsSplash *);

    enum FileOperation { Move, Copy, HardLink, Delete };
    Q_ENUM(FileOperation);
    void hardLinkFiles(QList<QString> &&files, QString &&source, QString &&destination);
    void moveFiles(QList<QString> &&files, QString &&source, QString &&destination);
    void deleteFiles(QList<QString> &&files, QString &&source);

    QAbstractFileIconProvider *iconProvider();
    QFileSystemModel *dirModel();
    QPointer<SortedImageModel> fileModel();

    ViewMode viewMode();
    void setViewMode(ViewMode);
    QBindable<ViewMode> bindableViewMode();

    ViewFlags_t viewFlags();
    void setViewFlag(ViewFlag, bool on = true);
    void setViewFlags(ViewFlags_t);
    QBindable<ViewFlags_t> bindableViewFlags();

    QString currentDir();
    void setCurrentDir(const QString &str, bool force);
    QString savedCurrentDir();
    void fixupAndSetCurrentDir(QString str);

    Qt::SortOrder imageSortOrder();
    void setImageSortOrder(Qt::SortOrder order);
    QBindable<Qt::SortOrder> bindableImageSortOrder();
    SortField imageSortField();
    void setImageSortField(SortField field);
    QBindable<SortField> bindableImageSortField();

    Qt::SortOrder sectionSortOrder();
    void setSectionSortOrder(Qt::SortOrder order);
    QBindable<Qt::SortOrder> bindableSectionSortOrder();
    SortField sectionSortField();
    void setSectionSortField(SortField field);
    QBindable<SortField> bindableSectionSortField();

    int iconHeight();
    void setIconHeight(int);
    ProgressIndicatorHelper *spinningIconHelper();

    QPixmap noIconPixmap();
    QPixmap noPreviewPixmap();

    QAction *actionOpen();
    QAction *actionExit();
    QActionGroup *copyMoveActionGroup();
    QActionGroup *viewFlagActionGroup();
    QActionGroup *viewModeActionGroup();
    QUndoStack *undoStack();

    QList<QString> getExistingFile(QWidget *parent, QString &proposedDirToOpen);
    QString getExistingDirectory(QWidget *parent, QString &proposedDirToOpen);

signals:
    void currentDirChanged(QString dir, QString old);
    void viewModeChanged(ViewMode newView, ViewMode old);
    void viewFlagsChanged(ViewFlags_t, ViewFlags_t);
    void imageSortOrderChanged(SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder);
    void sectionSortOrderChanged(SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder);
    void iconHeightChanged(int h, int old);

public slots:
    void about();
    void setCurrentDir(const QString &str);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
