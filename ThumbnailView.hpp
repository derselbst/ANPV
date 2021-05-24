
#pragma once

#include <memory>
#include <QMainWindow>
#include <QImage>
#include <QList>
#include <QDir>

class ANPV;
class SortedImageModel;

/**
 * Container class for a MainWindow, that contains a directory tree on the left, and the ThumbnailImageView in the 
 * central part of the window
 */
class ThumbnailView : public QMainWindow
{
Q_OBJECT

public:
    ThumbnailView(SortedImageModel* model, ANPV *parent);
    ~ThumbnailView() override;
    
    void selectedFiles(QList<QString>& selectedFiles);
    QDir currentDir();

public slots:
    void changeDir(const QString& dir, bool skipScrollTo=false);
    void scrollToCurrentImage();
    void selectThumbnail(const QModelIndex& idx);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
