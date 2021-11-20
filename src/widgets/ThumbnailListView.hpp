
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

signals:
    void moveFiles(QList<QString> imgs, QString source, QString destination);
    void copyFiles(QList<QString> imgs, QString source, QString destination);
    void deleteFiles(QList<QString> imgs, QString source);
        
protected:
    void wheelEvent(QWheelEvent *event) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
