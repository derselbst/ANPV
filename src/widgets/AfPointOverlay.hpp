 
#pragma once

#include <QAbstractGraphicsShapeItem>
#include <QSize>
#include <QRect>
#include <QRectF>
#include <memory>
#include <tuple>

enum class AfType { Disabled, Selected, HasFocus, Normal };

using AfPoint = std::tuple<AfType, QRect>;

class AfPointOverlay : public QAbstractGraphicsShapeItem
{
public:
    AfPointOverlay(const std::vector<AfPoint>& afPoints, QSize size);
    ~AfPointOverlay() override;
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = 0) override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
