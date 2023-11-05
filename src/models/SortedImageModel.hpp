
#pragma once

#include "DecodingState.hpp"
#include "types.hpp"

#include <QAbstractTableModel>
#include <QRunnable>
#include <QModelIndex>
#include <QFileInfo>
#include <QFuture>
#include <memory>

class QDir;
class Image;
class SmartImageDecoder;
class ImageSectionDataContainer;
class AbstractListItem;

class SortedImageModel : public QAbstractTableModel
{
    Q_OBJECT

public:

    enum ItemModelUserRoles
    {
        ItemName = Qt::DisplayRole,
        ItemThumbnail = Qt::DecorationRole,
        CheckAlignmentRole = Qt::UserRole,
        DecorationAlignmentRole,
        ItemFileName,
        ItemFileSize,
        ItemFileType,
        ItemFileLastModified,
        ItemIsSection,
        ItemImageResolution,
        ItemImageDateRecorded,
        ItemImageAperture,
        ItemImageExposure,
        ItemImageIso,
        ItemImageFocalLength,
        ItemImageLens,
        ItemImageCameraModel,
    };

    SortedImageModel(QObject *parent = nullptr);
    ~SortedImageModel() override;

    QSharedPointer<ImageSectionDataContainer> dataContainer();

    QFuture<DecodingState> changeDirAsync(const QString &dir);
    void decodeAllImages(DecodingState state, int imageHeight);

    using QAbstractTableModel::index; // don't hide base member
    QModelIndex index(const QSharedPointer<Image> &img);
    QModelIndex index(const Image *img);
    QSharedPointer<AbstractListItem> item(const QModelIndex &idx) const;
    QList<Image *> checkedEntries();

    QVariant data(const QSharedPointer<AbstractListItem> &item, int role) const;
    Qt::ItemFlags flags(const QSharedPointer<AbstractListItem> &item) const;

    bool isSafeToChangeDir();
    void welcomeImage(const QSharedPointer<Image> &image, const QSharedPointer<QFutureWatcher<DecodingState>> &watcher);

    bool insertRows(int row, std::list<QSharedPointer<AbstractListItem>> &items);
    void setLayoutTimerInterval(qint64 t);

public: // QAbstractItemModel

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

public slots:
    void cancelAllBackgroundTasks();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
