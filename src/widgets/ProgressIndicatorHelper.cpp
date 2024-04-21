
#include "ProgressIndicatorHelper.hpp"
#include "ANPV.hpp"
#include "xThreadGuard.hpp"

#include <QFutureWatcher>
#include <QFuture>
#include <QPointer>
#include <QSvgRenderer>
#include <QImage>
#include <QPainter>
#include <QString>

struct ProgressIndicatorHelper::Impl
{
    ProgressIndicatorHelper *q;

    QMetaObject::Connection renderingConnection;
    QScopedPointer<QSvgRenderer, QScopedPointerDeleteLater> renderer;
    // currentFrame must be destroyed after the painter is being destroyed, mind the order of declaration!
    QImage currentFrame;
    std::unique_ptr<QPainter> painter;

    // keep track of all image decoding tasks we spawn in the background, guarded by mutex, because accessed by UI thread and directory worker thread
    std::recursive_mutex m;

    Impl(ProgressIndicatorHelper *parent) : q(parent)
    {}

    void renderSvg()
    {
        std::unique_lock<std::recursive_mutex> l(m);
        if(this->painter->isActive())
        {
            this->currentFrame.fill(0);
            this->renderer->render(this->painter.get());

            l.unlock();
            emit q->needsRepaint();
        }
    }

    void onIconHeightChanged(int neu)
    {
        std::lock_guard<std::recursive_mutex> l(m);
        if(this->painter->isActive())
        {
            this->painter->end();
        }

        QSize imgSize = this->renderer->defaultSize().scaled(neu, neu, Qt::KeepAspectRatio);
        QImage image(imgSize, QImage::Format_ARGB32);
        this->currentFrame = image;

        if(image.isNull())
        {
            return;
        }

        bool ok = this->painter->begin(&this->currentFrame);
        Q_ASSERT(ok);
    }
};

ProgressIndicatorHelper::ProgressIndicatorHelper(QObject *parent) : QObject(parent), d(std::make_unique<Impl>(this))
{
    d->renderer.reset(new QSvgRenderer(QStringLiteral(":/images/decoding.svg")));
    d->renderer->moveToThread(ANPV::globalInstance()->backgroundThread());

    d->painter = std::make_unique<QPainter>();

    auto i = ANPV::globalInstance()->iconHeight();

    if(i > 0)
    {
        d->onIconHeightChanged(i);
    }

    connect(ANPV::globalInstance(), &ANPV::iconHeightChanged, this, [&](int neu, int)
    {
        d->onIconHeightChanged(neu);
    });
}

ProgressIndicatorHelper::~ProgressIndicatorHelper() = default;

// method is reentrant
void ProgressIndicatorHelper::startRendering()
{
    if(!d->renderingConnection)
    {
        d->renderingConnection = connect(d->renderer.get(), &QSvgRenderer::repaintNeeded, d->renderer.get(), [&]()
        {
            d->renderSvg();
        });
    }
}

void ProgressIndicatorHelper::stopRendering()
{
    disconnect(d->renderingConnection);
}

void ProgressIndicatorHelper::drawProgressIndicator(QPainter* localPainter, const QRect& bounds, const QFutureWatcher<DecodingState> &future)
{
    xThreadGuard(this);

    std::unique_lock<std::recursive_mutex> l(d->m);
    QRect icoRect = d->currentFrame.rect();
    icoRect.moveTo(bounds.topLeft());
    icoRect = icoRect.intersected(bounds);
    localPainter->drawImage(icoRect, d->currentFrame);
    l.unlock();

    int prog = future.progressValue();
    localPainter->setPen(future.isCanceled() ? Qt::red : Qt::blue);
    localPainter->setFont(QFont("Arial", 30));
    localPainter->drawText(icoRect, Qt::AlignHCenter | Qt::AlignVCenter, QString("%1%").arg(QString::number(prog)));
}
