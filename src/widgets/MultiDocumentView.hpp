
#pragma once

#include "types.hpp"
#include <memory>
#include <QMainWindow>
#include <QList>

class Image;
class QWidget;
class ImageSectionDataContainer;

class MultiDocumentView : public QMainWindow
{
    Q_OBJECT

public:
    MultiDocumentView(QMainWindow *parent);
    ~MultiDocumentView() override;

    void addImages(const QList<std::pair<QSharedPointer<Image>, QSharedPointer<ImageSectionDataContainer>>> &imageWithModel);
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
