
#include "AfPointOverlay.hpp"

#include <vector>
#include <QPainter>

struct AfPointOverlay::Impl
{
    std::vector<AfPoint> afPoints;
    QSize imageSize;
};

AfPointOverlay::AfPointOverlay(const std::vector<AfPoint>& afPoints, QSize size) : d(std::make_unique<Impl>())
{
    d->afPoints = afPoints;
    d->imageSize = size;
}

AfPointOverlay::~AfPointOverlay() = default;

QRectF AfPointOverlay::boundingRect() const
{
    return QRectF(QPointF(0, 0), d->imageSize);
}

void AfPointOverlay::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    auto clip = painter->clipBoundingRect();
    auto viewport = painter->viewport();
    auto window = painter->window();
    
    qInfo() << "clip: " << clip << "\nviewport: " << viewport << "\nwindow: " << window;
    
    auto pen = painter->pen();
    
    for(size_t i=0; i < d->afPoints.size(); i++)
    {
        auto& af = d->afPoints[i];
        auto type = std::get<0>(af);
        auto rect = std::get<1>(af);
        switch(type)
        {
        case AfType::Disabled:
            pen.setColor(Qt::gray);
            pen.setStyle(Qt::DotLine);
            pen.setWidth(3);
            break;
            
        case AfType::Selected:
            pen.setColor(Qt::yellow);
            pen.setStyle(Qt::SolidLine);
            pen.setWidth(4);
            break;
            
        case AfType::HasFocus:
            pen.setColor(Qt::red);
            pen.setStyle(Qt::SolidLine);
            pen.setWidth(4);
            break;
            
        case AfType::Normal:
            pen.setColor(Qt::black);
            pen.setStyle(Qt::SolidLine);
            pen.setWidth(2);
            break;
        }
        
        painter->setPen(pen);
        painter->drawRect(rect);
    }
}
