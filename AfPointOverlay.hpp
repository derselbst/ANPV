 
#pragma once

#include <QAbstractGraphicsShapeItem>
#include <QSize>
#include <QRect>
#include <QRectF>
#include <memory>

class AfPointOverlay : public QAbstractGraphicsShapeItem
{
public:
    AfPointOverlay(long totalAfPoints, QSize size);
    ~AfPointOverlay() override;
    
    enum class AfType { Disabled, Selected, HasFocus, Normal };
    
    void addAfArea(QRect, AfType);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = 0) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
