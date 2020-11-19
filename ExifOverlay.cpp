
#include "ExifOverlay.hpp"

#include "AfPointOverlay.hpp"

#include <QGraphicsOpacityEffect>
#include <QLabel>


struct ExifOverlay::Impl
{
};

ExifOverlay::ExifOverlay(QWidget* parent)
: QLabel(parent), d(std::make_unique<Impl>())
{
    this->setVisible(false);
    this->setMargin(3);
    this->setTextFormat(Qt::PlainText);
    this->setStyleSheet("QLabel { background-color : black; color : white; border-radius: 3px} ");
    
    auto* feedbackEffect = new QGraphicsOpacityEffect(this);
    feedbackEffect->setOpacity(0.5);
    feedbackLabel->setGraphicsEffect(feedbackEffect);
}

ExifOverlay::~ExifOverlay() = default;

void ExifOverlay::setMetadata(ExifWrapper* exif)
{
    this->setText("TEST");
    this->adjustSize();
    this->setVisible(true);
}

void ExifOverlay::unsetMetadata()
{
    this->setVisible(false);
    this->clear();
}
