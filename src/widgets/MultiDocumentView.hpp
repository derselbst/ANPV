
#pragma once

#include <memory>
#include <QMainWindow>
#include <QList>

class Image;
class QWidget;

class MultiDocumentView : public QMainWindow
{
Q_OBJECT

public:
    MultiDocumentView(QWidget *parent);
    ~MultiDocumentView() override;

    void addImages(QList<QSharedPointer<Image>> img);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
