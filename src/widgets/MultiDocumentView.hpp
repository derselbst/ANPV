
#pragma once

#include <memory>
#include <QTabWidget>

class ANPV;
class Image;
class QGraphicsScene;
class QWidget;
class QPixmap;
class QWheelEvent;
class QEvent;
class SmartImageDecoder;

class MultiDocumentView : public QTabWidget
{
Q_OBJECT

public:
    MultiDocumentView(QWidget *parent);
    ~MultiDocumentView() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
