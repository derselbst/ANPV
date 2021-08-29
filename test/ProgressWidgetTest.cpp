
#include "CancellableProgressDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "DecodingState.hpp"

#include <chrono>
#include <thread>

#include <QDebug>
#include <QFuture>
#include <QPromise>
#include <QMainWindow>
#include <QApplication>

using namespace std::chrono_literals;

class MyTask : public QRunnable
{
    QPromise<DecodingState> promise;
public:
    QFuture<DecodingState> runAsync()
    {
        this->setAutoDelete(false);
        this->promise.setProgressRange(0, 100);
        QThreadPool::globalInstance()->start(this);
        return this->promise.future();
    }
    
    void run() override
    {
        this->promise.start();
        for(int i = 0; i<=100; i++)
        {
            std::this_thread::sleep_for(100ms);
            this->promise.setProgressValueAndText(i, QString("Workload: ") + QString::number(i/10));
        }
        this->promise.addResult(DecodingState::FullImage);
        this->promise.finish();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QScopedPointer<QMainWindow> mainWindow(new QMainWindow());
    
    MyTask t;
    QFuture<DecodingState> fut = t.runAsync();
    
    CancellableProgressDialog<DecodingState>* dialog = new CancellableProgressDialog<DecodingState>(fut, "Async Test Operation", mainWindow.data());
    QObject::connect(dialog, &QObject::destroyed, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    dialog->show();

    CancellableProgressWidget *progWid = new CancellableProgressWidget(fut, nullptr, mainWindow.data());
    mainWindow->setCentralWidget(progWid);
    mainWindow->show();
    
    return app.exec();
}
