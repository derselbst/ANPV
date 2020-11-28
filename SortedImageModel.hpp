
#pragma once

#include "DecodingState.hpp"

#include <QAbstractListModel>
#include <QModelIndex>
#include <QFileInfo>
#include <memory>

class QDir;
class ImageDecodeTask;
class SmartImageDecoder;

class SortedImageModel : public QAbstractListModel
{
    Q_OBJECT
    
public:
    SortedImageModel(QObject* parent = nullptr);
    ~SortedImageModel() override;
    
    void changeDirAsync(const QDir& dir);

public: // QAbstractItemModel
    
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    QFileInfo fileInfo(const QModelIndex &index) const;
    
signals:
    void directoryLoadingStatusMessage(int progress, QString msg);
    void directoryLoadingProgress(int progress);
    void directoryLoaded();
    void directoryLoadingFailed(QString msg, QString details);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    
private slots:
    void onBackgroundImageTaskStateChanged(SmartImageDecoder* dec, quint32, quint32);
};
