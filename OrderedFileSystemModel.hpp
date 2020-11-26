
#pragma once

#include <QAbstractListModel>
#include <QModelIndex>
#include <memory>

class QDir;

class OrderedFileSystemModel : public QAbstractListModel
{
    Q_OBJECT
    
public:
    OrderedFileSystemModel();
    ~OrderedFileSystemModel() override;
    
    void changeDirAsync(const QDir& dir);

public: // QAbstractItemModel
    
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

signals:
    void directoryLoadingStatusMessage(QString msg);
    void directoryLoadingProgress(int progress);
    void directoryLoaded();
    void directoryLoadingFailed(QString msg, QString details);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
