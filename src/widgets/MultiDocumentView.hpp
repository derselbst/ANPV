
#pragma once

#include "types.hpp"
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
    MultiDocumentView(QMainWindow *parent);
    ~MultiDocumentView() override;

    void addImages(const QList<QSharedPointer<Image>>& image, QPointer<SortedImageModel> model);
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
