
#pragma once

#include "types.hpp"

#include <QListView>
#include <QString>
#include <QSharedPointer>
#include <QModelIndexList>
#include <QList>
#include <memory>

class QAbstractItemModel;
class QWheelEvent;
class QWidget;
class QAction;
class SortedImageModel;
class ThumbnailView;
class Image;
class ANPV;

class ThumbnailListView : public QListView
{
Q_OBJECT

public:
    ThumbnailListView(QWidget *parent=nullptr);
    ~ThumbnailListView() override;
    void setModel(QAbstractItemModel *model) override;

    void fileOperationOnSelectedFiles(QAction*);
    QList<Image*> checkedImages();
    QList<QSharedPointer<Image>> selectedImages();
    QList<QSharedPointer<Image>> selectedImages(const QModelIndexList& selectedIdx);
        
protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
    void setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags flags) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
