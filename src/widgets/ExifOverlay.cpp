
#include "ExifOverlay.hpp"

#include "AfPointOverlay.hpp"
#include "Image.hpp"

#include <QGraphicsOpacityEffect>


struct ExifOverlay::Impl
{
    void effect(ExifOverlay* e, double opa)
    {
        auto* eff = new QGraphicsOpacityEffect(e);
        eff->setOpacity(opa);
        e->setGraphicsEffect(eff);
    }
    
    void opaqueEffect(ExifOverlay* e)
    {
        effect(e, 1);
    }
    
    void transparentEffect(ExifOverlay* e)
    {
        effect(e, 0.5);
    }
};

ExifOverlay::ExifOverlay(QWidget* parent)
: MessageWidget(parent), d(std::make_unique<Impl>())
{
    this->setCloseButtonVisible(false);
    this->setWordWrap(false);
    this->setAttribute(Qt::WA_Hover);
    d->transparentEffect(this);
    this->hide();
}

ExifOverlay::~ExifOverlay() = default;

void ExifOverlay::setMetadata(QSharedPointer<Image> dec)
{
    QString s = dec->formatInfoString();
    
    if(s.isEmpty())
    {
        this->hide();
    }
    else
    {
        this->setText(s);
        this->setMessageType(MessageWidget::MessageType::Positive);
        this->adjustSize();
        this->show();
    }
}

void ExifOverlay::enterEvent(QEnterEvent * event)
{
    d->opaqueEffect(this);
    QWidget::enterEvent(event);
}

void ExifOverlay::leaveEvent(QEvent * event)
{
    d->transparentEffect(this);
    QWidget::leaveEvent(event);
}
