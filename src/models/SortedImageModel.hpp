
#pragma once

#include "DecodingState.hpp"

#include <QAbstractListModel>
#include <QRunnable>
#include <QModelIndex>
#include <QFileInfo>
#include <QFuture>
#include <memory>

class QDir;
class ImageDecodeTask;
class Image;

class SortedImageModel : public QAbstractListModel, public QRunnable
{
    Q_OBJECT
    
public:
    
    enum Column : int
    {
        Unknown = -1,
        FirstValid = 0,
        FileName = FirstValid,
        FileSize,
        DateModified,
        Resolution,
        DateRecorded,
        Aperture,
        Exposure,
        Iso,
        FocalLength,
        Lens,
        CameraModel,
        Count // must be last!
    };

    SortedImageModel(QObject* parent = nullptr);
    ~SortedImageModel() override;
    
    QFuture<DecodingState> changeDirAsync(const QDir& dir);
    void run() override;
    
    using QAbstractListModel::index; // don't hide base member
    QModelIndex index(const QFileInfo& info);
    QFileInfo fileInfo(const QModelIndex& idx) const;
    QSharedPointer<Image> goTo(const QString& currentUrl, int stepsFromCurrent, QModelIndex& idx);

    void sort(Column column);
    void sort(Qt::SortOrder order);
    
    int iconHeight() const;
    void setIconHeight(int);

public: // QAbstractItemModel
    
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
