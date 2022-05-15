
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
class ImageDecodeTask;
class Image;
class SmartImageDecoder;

class SortedImageModel : public QAbstractTableModel, public QRunnable
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
    static const QSharedPointer<Image>& image(const Entry_t& e);
    static const QSharedPointer<SmartImageDecoder>& decoder(const Entry_t& e);

    SortedImageModel(QObject* parent = nullptr);
    ~SortedImageModel() override;
    
    QFuture<DecodingState> changeDirAsync(const QString& dir);
    void run() override;
    void decodeAllImages(DecodingState state, int imageHeight);
    
    using QAbstractTableModel::index; // don't hide base member
    QModelIndex index(const QSharedPointer<Image>& img);
    QModelIndex index(const Image* img);
    QFileInfo fileInfo(const QModelIndex& idx) const;
    Entry_t goTo(const QSharedPointer<Image>& img, int stepsFromCurrent);
    Entry_t entry(const QModelIndex& idx) const;
    Entry_t entry(unsigned int row) const;

    void sort(Column column);
    void sort(Qt::SortOrder order);

public: // QAbstractItemModel
    
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
