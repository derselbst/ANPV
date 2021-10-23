
#pragma once

#include <memory>
#include <QDir>
#include <QObject>
#include <QString>
#include <QFileInfo>
#include <QSharedPointer>

#include "DecodingState.hpp"
#include "SortedImageModel.hpp"

class QUndoCommand;
class MoveFileCommand;
class QSplashScreen;
class QFileSystemModel;
class QAbstractFileIconProvider;
class SmartImageDecoder;
class CancellableProgressWidget;

template<typename T>
class QFuture;

enum class ProgressGroup : int
{
    Directory,
    Image,
};

enum class ViewMode : int
{
    Unknown,
    None,
    Fit,
    CenterAf,
};

using ViewFlags_t = unsigned int;
enum class ViewFlag : ViewFlags_t
{
    None = 0,
    CombineRawJpg = 0x1,
    ShowAfPoints = 0x2,
    RespectExifOrientation = 0x4,
};


class ANPV : public QObject
{
Q_OBJECT

public:
    static constexpr int MaxIconHeight = 500/*px*/;
    static ANPV* globalInstance();

    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    void openImages(QList<QSharedPointer<Image>>);
    void showThumbnailView(QSharedPointer<Image> img);

    void addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut);
    void hideProgressWidget(CancellableProgressWidget* w);
    
    void moveFilesSlot(const QString& targetDir);
    void moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir);
    
    QAbstractFileIconProvider* iconProvider();
    QFileSystemModel* dirModel();

    ViewMode viewMode();
    void setViewMode(ViewMode);

    ViewFlags_t viewFlags();
    void setViewFlag(ViewFlag, bool on=true);
    void setViewFlags(ViewFlags_t);
    
    QDir currentDir();
    void setCurrentDir(QString str);
    
    Qt::SortOrder sortOrder();
    void setSortOrder(Qt::SortOrder);
    
    SortedImageModel::Column primarySortColumn();
    void setPrimarySortColumn(SortedImageModel::Column);
    
    int iconHeight();
    void setIconHeight(int);

signals:
    void currentDirChanged(QDir dir, QDir old);
    void viewModeChanged(ViewMode newView, ViewMode old);
    void viewFlagsChanged(ViewFlags_t, ViewFlags_t);
    void sortOrderChanged(Qt::SortOrder newOrder, Qt::SortOrder old);
    void primarySortColumnChanged(SortedImageModel::Column newCol, SortedImageModel::Column old);
    void iconHeightChanged(int h, int old);
    
public slots:
    void about();
//     void loadImage(QFileInfo str);
//     void loadImage(QSharedPointer<SmartImageDecoder> dec);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
