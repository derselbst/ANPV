
#include "CancellableProgressWidget.hpp"
#include "ui_CancellableProgressWidget.h"
#include "ANPV.hpp"

#include <QFutureWatcher>
#include <QFuture>
#include <QDebug>
#include <QString>
#include <QTimer>

struct CancellableProgressWidget::Impl
{
    std::unique_ptr<Ui::CancellableProgressWidget> ui = std::make_unique<Ui::CancellableProgressWidget>();

    QFutureWatcher<DecodingState> future;
    // https://bugreports.qt.io/browse/QTBUG-91048
    // the behaviour of QFutureWatcher::isFinished() changed with QT6.2: now there is no reyable way to detect if the finished event has already been sent out :/
    bool futureFinishedEventReceived = false;

    QTimer *hideTimer = nullptr;

    static QString getProgressStyle(DecodingState state)
    {
        constexpr char successStart[] = "#99ffbb";
        constexpr char successEnd[] = "#00cc44";
        constexpr char errorStart[] = "#ff9999";
        constexpr char errorEnd[] = "#d40000";

        const char *colorStart;
        const char *colorEnd;

        switch(state)
        {
        case DecodingState::Error:
        case DecodingState::Cancelled:
            colorStart = errorStart;
            colorEnd = errorEnd;
            break;

        default:
            colorStart = successStart;
            colorEnd = successEnd;
            break;
        }

        return QString(
                   "QProgressBar {"
                   "border: 2px solid grey;"
                   "border-radius: 5px;"
                   "text-align: center;"
                   "}"
                   ""
                   "QProgressBar::chunk {"
                   "background-color: qlineargradient(x1: 0, y1: 0.2, x2: 1, y2: 0, stop: 0 %1, stop: 1 %2);"
                   "width: 20px;"
                   "margin: 0px;"
                   "}").arg(colorStart).arg(colorEnd);
    }

    void onStarted()
    {
        this->ui->progressBar->setStyleSheet(getProgressStyle(DecodingState::Ready));
        this->ui->cancelButton->setEnabled(true);
        this->ui->label->setText("undefined");
        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    }

    void onFinished()
    {
        DecodingState result = DecodingState::Error;

        if(future.isCanceled())
        {
            result = DecodingState::Cancelled;
        }
        else
        {
            this->ui->progressBar->setValue(this->ui->progressBar->maximum());
            auto resC = future.future().resultCount();

            if(resC > 0)
            {
                result = future.result();
            }
        }

        this->ui->progressBar->setStyleSheet(getProgressStyle(result));
        this->ui->cancelButton->setEnabled(false);
        QGuiApplication::restoreOverrideCursor();

        hideTimer->start();

        this->futureFinishedEventReceived = true;
    }
};

CancellableProgressWidget::CancellableProgressWidget(QWidget *parent, Qt::WindowFlags f) : QWidget(parent, f), d(std::make_unique<Impl>())
{
    d->ui->setupUi(this);
    d->hideTimer = new QTimer(this);
    d->hideTimer->setSingleShot(true);
    d->hideTimer->setInterval(2000);

    QObject::connect(d->hideTimer, &QTimer::timeout, this, [&]()
    {
        emit this->expired(this);
    });

    QObject::connect(d->ui->cancelButton, &QPushButton::clicked, &d->future, &QFutureWatcher<DecodingState>::cancel);
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::progressTextChanged, d->ui->label, &QLabel::setText);
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::progressTextChanged, d->ui->label, &QLabel::setToolTip);
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::progressRangeChanged, d->ui->progressBar, &QProgressBar::setRange);
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::progressValueChanged, d->ui->progressBar, &QProgressBar::setValue);
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::started, this, [&]()
    {
        d->onStarted();
    });
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::finished, this, [&]()
    {
        d->onFinished();
    });
    QObject::connect(&d->future, &QFutureWatcher<DecodingState>::canceled, this, [&]()
    {
        d->onFinished();
    });
}

CancellableProgressWidget::~CancellableProgressWidget()
{
    qDebug() << "Destroy Prog Widget " << this;
}

bool CancellableProgressWidget::isFinished()
{
    return d->futureFinishedEventReceived;
}

void CancellableProgressWidget::setFuture(const QFuture<DecodingState> &future)
{
    if(!d->futureFinishedEventReceived) // finished event already emitted?
    {
        d->future.cancel();
        // manually finish up ourselves
        d->onFinished();
    }

    d->hideTimer->stop();
    d->future.setFuture(future);
    d->futureFinishedEventReceived = false;
}
