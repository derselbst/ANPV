
#include "ProgressIndicatorHelper.hpp"
#include "ANPV.hpp"
#include "xThreadGuard.hpp"

#include <QFutureWatcher>
#include <QFuture>
#include <QPointer>
#include <QSvgRenderer>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QString>
#include <QTimer>

struct ProgressIndicatorHelper::Impl
{
    ProgressIndicatorHelper* q;
    
    QMetaObject::Connection renderingConnection;
    QPointer<QSvgRenderer> renderer;
    // currentFrame must be destroyed after the painter is being destroyed, mind the order of declaration!
    QImage currentFrame;
    std::unique_ptr<QPainter> painter;

    Impl(ProgressIndicatorHelper* parent) : q(parent)
    {}
    
    void renderSvg()
    {
        this->currentFrame.fill(0);
        this->renderer->render(this->painter.get());
        
        emit q->needsRepaint();
    }
    
    void onIconHeightChanged(int neu)
    {
        QSize imgSize = this->renderer->defaultSize().scaled(neu, neu, Qt::KeepAspectRatio);
        QImage image(imgSize, QImage::Format_ARGB32);
        this->currentFrame = image;
        this->painter = std::make_unique<QPainter>(&this->currentFrame);
    }
};

ProgressIndicatorHelper::ProgressIndicatorHelper(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this))
{
    d->renderer = new QSvgRenderer(QStringLiteral(":/images/decoding.svg"), this);
    
    d->onIconHeightChanged(ANPV::globalInstance()->iconHeight());
    connect(ANPV::globalInstance(), &ANPV::iconHeightChanged, this, [&](int neu, int){ d->onIconHeightChanged(neu); });
}

ProgressIndicatorHelper::~ProgressIndicatorHelper() = default;

void ProgressIndicatorHelper::startRendering()
{
    d->renderingConnection = connect(d->renderer, &QSvgRenderer::repaintNeeded, this, [&](){ d->renderSvg(); });
}

void ProgressIndicatorHelper::stopRendering()
{
    disconnect(d->renderingConnection);
}

QPixmap ProgressIndicatorHelper::getProgressIndicator(const QFutureWatcher<DecodingState>& future)
{
    xThreadGuard(this);
    int prog = future.progressValue();
    
    QImage image = d->currentFrame.copy();
    QPainter localPainter(&image);
    localPainter.setPen(future.isCanceled() ? Qt::red : Qt::blue);
    localPainter.setFont(QFont("Arial", 30));
    QRect rect(0,0, image.width(), image.height());
    localPainter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, QString("%1%").arg(QString::number(prog)));
    
    return QPixmap::fromImage(image);
}
