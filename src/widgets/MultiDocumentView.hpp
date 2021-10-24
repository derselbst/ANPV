
#pragma once

#include <memory>
#include <QMainWindow>
#include <QList>

class Image;
class QWidget;
class SortedImageModel;

class MultiDocumentView : public QMainWindow
{
Q_OBJECT

public:
    MultiDocumentView(QWidget *parent);
    ~MultiDocumentView() override;

    void addImages(const QList<QSharedPointer<Image>>& image, QSharedPointer<SortedImageModel> model);
    void keyPressEvent(QKeyEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
