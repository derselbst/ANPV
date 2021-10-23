
#pragma once

#include <QListView>
#include <memory>

class QWheelEvent;
class QWidget;
class SortedImageModel;
class ThumbnailView;
class ANPV;
class Image;

class ThumbnailListView : public QListView
{
Q_OBJECT

public:
    ThumbnailListView(QWidget *parent=nullptr);
    ~ThumbnailListView() override;
    
    void setModel(SortedImageModel* model);
    void selectedFiles(QList<QString>& selectedFiles);
    void setCurrentIndex(const QSharedPointer<Image>& img);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
