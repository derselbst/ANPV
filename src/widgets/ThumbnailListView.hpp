
#pragma once

#include <QListView>
#include <QString>
#include <memory>

class QWheelEvent;
class QWidget;
class SortedImageModel;
class ThumbnailView;
class ANPV;

class ThumbnailListView : public QListView
{
Q_OBJECT

public:
    ThumbnailListView(QWidget *parent=nullptr);
    ~ThumbnailListView() override;

    void moveSelectedFiles(QString&& destination);
        
protected:
    void wheelEvent(QWheelEvent *event) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
