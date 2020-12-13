
#pragma once

#include <QListView>

class QWheelEvent;
class SortedImageModel;

class ThumbnailImageView : public QListView
{
Q_OBJECT

public:
    ThumbnailImageView(QWidget *parent=nullptr);
    ~ThumbnailImageView() override;
    
    void setModel(SortedImageModel* model);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    
private:
    SortedImageModel* model=nullptr;
};
