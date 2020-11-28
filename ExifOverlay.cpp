
#include "ExifOverlay.hpp"

#include "AfPointOverlay.hpp"
#include "ExifWrapper.hpp"

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

void ExifOverlay::setMetadata(ExifWrapper* exif)
{
    QString s = exif->formatToString();
    
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
