
#include "ExifOverlay.hpp"

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"

#include <QGraphicsOpacityEffect>


struct ExifOverlay::Impl
{
};

ExifOverlay::ExifOverlay(QWidget* parent)
: MessageWidget(parent), d(std::make_unique<Impl>())
{
    this->setCloseButtonVisible(false);
    this->setWordWrap(false);
    this->hide();
    
    auto* feedbackEffect = new QGraphicsOpacityEffect(this);
    feedbackEffect->setOpacity(0.65);
    this->setGraphicsEffect(feedbackEffect);
}

ExifOverlay::~ExifOverlay() = default;

void ExifOverlay::setMetadata(SmartImageDecoder* dec)
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

// void ExifOverlay::unsetMetadata()
// {
//     this->setVisible(false);
//     this->clear();
// }
