
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

class ANPV : public QObject
{
Q_OBJECT

public:
    static ANPV* globalInstance();

    ANPV(QSplashScreen *splash);
    ~ANPV() override;

    void addBackgroundTask(ProgressGroup group, const QFuture<DecodingState>& fut);
    void hideProgressWidget(CancellableProgressWidget* w);
    
    void moveFilesSlot(const QString& targetDir);
    void moveFilesSlot(const QList<QString>& files, const QString& sourceDir, const QString& targetDir);
    
    ViewMode viewMode();
    void setViewMode(ViewMode);

    QDir currentDir();
    void setCurrentDir(QString str);
    
    Qt::SortOrder sortOrder();
    void setSortOrder(Qt::SortOrder);
    
    SortedImageModel::Column primarySortColumn();
    void setPrimarySortColumn(SortedImageModel::Column);

signals:
    void currentDirChanged(QDir dir, QDir old);
    void viewModeChanged(ViewMode newView, ViewMode old);
    void sortOrderChanged(Qt::SortOrder newOrder, Qt::SortOrder old);
    void primarySortColumnChanged(SortedImageModel::Column newCol, SortedImageModel::Column old);
    
public slots:
//     void showImageView();
//     void showThumbnailView();
//     void loadImage(QFileInfo str);
//     void loadImage(QSharedPointer<SmartImageDecoder> dec);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
