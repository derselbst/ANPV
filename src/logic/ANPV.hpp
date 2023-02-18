
#pragma once

#include <memory>
#include <QDir>
#include <QObject>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>

#include "DecodingState.hpp"
#include "SortedImageModel.hpp"
#include "types.hpp"

class MoveFileCommand;
class QSplashScreen;
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

template<typename T>
class QFuture;


class ANPV : public QObject
{
Q_OBJECT

public:
    static constexpr int MaxIconHeight = 500/*px*/;
    static ANPV* globalInstance();
    static QString formatByteHtmlString(float fsize);
    static void setClipboardDataCut(QMimeData *mimeData, bool cut);
    static void setUrls(QMimeData *mimeData, const QList<QUrl> &localUrls);

    ANPV();
    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    QThread* backgroundThread();

    void openImages(const QList<QSharedPointer<Image>>&);
    void showThumbnailView();
    void showThumbnailView(QSplashScreen*);
    
    enum FileOperation { Move, Copy, HardLink, Delete };
    Q_ENUM(FileOperation);
    void hardLinkFiles(QList<QString>&& files, QString&& source, QString&& destination);
    void moveFiles(QList<QString>&& files, QString&& source, QString&& destination);
    
    QAbstractFileIconProvider* iconProvider();
    QFileSystemModel* dirModel();
    QSharedPointer<SortedImageModel> fileModel();

    ViewMode viewMode();
    void setViewMode(ViewMode);

    ViewFlags_t viewFlags();
    void setViewFlag(ViewFlag, bool on=true);
    void setViewFlags(ViewFlags_t);
    
    QString currentDir();
    void setCurrentDir(const QString& str, bool force);
    QString savedCurrentDir();
    void fixupAndSetCurrentDir(QString str);
    
    Qt::SortOrder imageSortOrder();
    void setImageSortOrder(Qt::SortOrder order);
    SortField imageSortField();
    void setImageSortField(SortField field);

    Qt::SortOrder sectionSortOrder();
    void setSectionSortOrder(Qt::SortOrder order);
    SortField sectionSortField();
    void setSectionSortField(SortField field);
    
    int iconHeight();
    void setIconHeight(int);
    ProgressIndicatorHelper* spinningIconHelper();
    
    QPixmap noIconPixmap();
    QPixmap noPreviewPixmap();
    
    QAction* actionOpen();
    QAction* actionExit();
    QActionGroup* copyMoveActionGroup();
    QActionGroup* viewFlagActionGroup();
    QActionGroup* viewModeActionGroup();
    QUndoStack* undoStack();
    
    QList<QString> getExistingFile(QWidget* parent, QString& proposedDirToOpen);
    QString getExistingDirectory(QWidget* parent, QString& proposedDirToOpen);

signals:
    void currentDirChanged(QString dir, QString old);
    void viewModeChanged(ViewMode newView, ViewMode old);
    void viewFlagsChanged(ViewFlags_t, ViewFlags_t);
    void imageSortOrderChanged(SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder);
    void sectionSortOrderChanged(SortField newField, Qt::SortOrder newOrder, SortField oldField, Qt::SortOrder oldOrder);
    void iconHeightChanged(int h, int old);
    
public slots:
    void about();
    void setCurrentDir(const QString& str);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
