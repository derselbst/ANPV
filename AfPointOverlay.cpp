
#include "AfPointOverlay.hpp"

#include <vector>
#include <QPainter>

struct AfPointOverlay::Impl
{
    QSize imageSize;
    
    struct AfContainer
    {
        AfType type;
        QRect rect;
    };
    
    std::vector<AfContainer> afPoints;
};

AfPointOverlay::AfPointOverlay(long totalAfPoints, QSize size) : d(std::make_unique<Impl>())
{
    d->imageSize = size;
    d->afPoints.reserve(totalAfPoints);
}

AfPointOverlay::~AfPointOverlay() = default;

void AfPointOverlay::addAfArea(QRect af, AfType type)
{
    d->afPoints.push_back({type,af});
}
    
QRectF AfPointOverlay::boundingRect() const
{
    return QRectF(QPointF(0, 0), d->imageSize);
}

void AfPointOverlay::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    auto pen = painter->pen();
    
    for(size_t i=0; i < d->afPoints.size(); i++)
    {
        auto& af = d->afPoints[i];
        switch(af.type)
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
        painter->drawRect(af.rect);
    }
}
