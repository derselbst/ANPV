
#pragma once

#include <QProgressDialog>
#include <QFutureWatcher>
#include <QString>
#include <QDebug>
#include <QFuture>

template <typename T>
class CancellableProgressDialog : public QProgressDialog
{
    QFutureWatcher<T> futureWatcher;
    QString operationName;
public:
    explicit CancellableProgressDialog(const QFuture<T>& future, const QString& operationName=QString(), QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags()) : QProgressDialog(parent, flags)
    {
        QObject::connect(this, &QProgressDialog::canceled, &futureWatcher, &QFutureWatcher<T>::cancel);

        // we must not use setAttribute(Qt::WA_DeleteOnClose); because it will emit QProgressDialog::canceled (once the dialog has reached 100%)
        // which in turn will cancel the future, which in turn will prevent other clients to obtain the result from the future
        QObject::connect(&futureWatcher, &QFutureWatcher<T>::finished, this, &QProgressDialog::deleteLater);
        QObject::connect(&futureWatcher, &QFutureWatcher<T>::progressRangeChanged, this, &QProgressDialog::setRange);
        QObject::connect(&futureWatcher, &QFutureWatcher<T>::progressValueChanged, this, &QProgressDialog::setValue);

        if(operationName.isEmpty())
        {
            QObject::connect(&futureWatcher, &QFutureWatcher<T>::progressTextChanged,
                             this, &QProgressDialog::setLabelText);
        }
        else
        {
            this->operationName = operationName + QString("\n\n");
            QObject::connect(&futureWatcher, &QFutureWatcher<T>::progressTextChanged,
                             this,
                             [this](QString progressMsg)
                             { this->setLabelText(this->operationName + progressMsg);});
        }
        futureWatcher.setFuture(future);
    }

    ~CancellableProgressDialog()
    {
        qDebug() << "Destroy Prog Diag " << this;
    }
};
