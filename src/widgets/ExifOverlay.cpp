
#include "ExifOverlay.hpp"

#include "AfPointOverlay.hpp"
#include "Image.hpp"

#include <QGraphicsOpacityEffect>


struct ExifOverlay::Impl
{
    QGraphicsOpacityEffect* opaEffect = nullptr;
    void effect(double opa)
    {
        opaEffect->setOpacity(opa);
    }
    
    void opaqueEffect()
    {
        effect(1);
    }
    
    void transparentEffect()
    {
        effect(0.35);
    }
};

ExifOverlay::ExifOverlay(QWidget* parent)
: MessageWidget(parent), d(std::make_unique<Impl>())
{
    this->setCloseButtonVisible(false);
    this->setWordWrap(false);
    this->setAttribute(Qt::WA_Hover);
    this->setFocusPolicy(Qt::NoFocus);
    d->opaEffect = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(d->opaEffect);
    d->transparentEffect();
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
    d->opaqueEffect();
    QWidget::enterEvent(event);
}

void ExifOverlay::leaveEvent(QEvent * event)
{
    d->transparentEffect();
    QWidget::leaveEvent(event);
}
