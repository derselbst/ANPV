
#include "CancellableProgressDialog.hpp"
#include "CancellableProgressWidget.hpp"
#include "ProgressIndicatorHelper.hpp"
#include "DecodingState.hpp"
#include "ANPV.hpp"

#include <chrono>
#include <thread>

#include <QLabel>
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
    Q_INIT_RESOURCE(ANPV);
    QApplication app(argc, argv);
    ANPV anpv;

    QScopedPointer<QMainWindow> mainWindow(new QMainWindow());
    
    MyTask t;
    QFuture<DecodingState> fut = t.runAsync();
    
    CancellableProgressDialog<DecodingState>* dialog = new CancellableProgressDialog<DecodingState>(fut, "Async Test Operation", mainWindow.data());
    QObject::connect(dialog, &QObject::destroyed, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    dialog->show();

    CancellableProgressWidget *progWid = new CancellableProgressWidget(mainWindow.data());
    progWid->setFuture(fut);
    mainWindow->setCentralWidget(progWid);
    mainWindow->show();
    
    QLabel spinningIcon;
    QFutureWatcher<DecodingState> wat(&spinningIcon);
    wat.setFuture(fut);
    spinningIcon.resize(200,200);
    spinningIcon.show();
    
    ProgressIndicatorHelper spinner(&spinningIcon);
    QObject::connect(&spinner, &ProgressIndicatorHelper::needsRepaint, &spinningIcon,
                     [&]()
                     {
                         QPixmap frame = spinner.getProgressIndicator(wat);
                         spinningIcon.setPixmap(frame);
                     });
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::progressValueChanged, &spinningIcon,
                     [&]()
                     {
                         QPixmap frame = spinner.getProgressIndicator(wat);
                         spinningIcon.setPixmap(frame);
                     });
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::started, &spinningIcon, [&](){ spinner.startRendering(); });
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::finished, &spinningIcon, [&](){ spinner.stopRendering(); });
    QObject::connect(&wat, &QFutureWatcher<DecodingState>::canceled, &spinningIcon,
                     [&](){
                         QPixmap frame = spinner.getProgressIndicator(wat);
                         spinningIcon.setPixmap(frame);
                         spinner.stopRendering(); });
    
    return app.exec();
}
