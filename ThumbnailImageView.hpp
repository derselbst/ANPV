
#pragma once

#include <QListView>
#include <memory>

class QWheelEvent;
class SortedImageModel;
class ThumbnailView;
class ANPV;

class ThumbnailImageView : public QListView
{
Q_OBJECT

public:
    ThumbnailImageView(ANPV *anpv, ThumbnailView *parent=nullptr);
    ~ThumbnailImageView() override;
    
    void setModel(SortedImageModel* model);
    void selectedFiles(QList<QString>& selectedFiles);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
