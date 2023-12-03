
#include <QSplashScreen>
#include <QStringLiteral>

class TomsSplash : public QSplashScreen
{
public:
    TomsSplash() : QSplashScreen()
    {
        this->show();
    }

    void showMessage(const QString& message)
    {
        constexpr unsigned int len = sizeof(frameFile) / sizeof(frameFile[0]);
        if (currentFrame < len)
        {
            this->setPixmap(QPixmap(frameFile[currentFrame++], "jpg", Qt::NoFormatConversion));
        }
        QSplashScreen::showMessage(message, Qt::AlignLeft | Qt::AlignTop);
    }

private:
    static const inline QString frameFile[] =
    {
        QStringLiteral(":/images/splash/1.jpg"),
        QStringLiteral(":/images/splash/2.jpg"),
        QStringLiteral(":/images/splash/3.jpg"),
        QStringLiteral(":/images/splash/4.jpg"),
        QStringLiteral(":/images/splash/5.jpg"),
        QStringLiteral(":/images/splash/6.jpg"),
        QStringLiteral(":/images/splash/7.jpg"),
        QStringLiteral(":/images/splash/8.jpg"),
        QStringLiteral(":/images/splash/9.jpg"),
        QStringLiteral(":/images/splash/10.jpg"),
        QStringLiteral(":/images/splash/11.jpg"),
        QStringLiteral(":/images/splash/12.jpg"),
        QStringLiteral(":/images/splash/13.jpg"),
        QStringLiteral(":/images/splash/14.jpg"),
        QStringLiteral(":/images/splash/15.jpg"),
        QStringLiteral(":/images/splash/16.jpg"),
        QStringLiteral(":/images/splash/17.jpg"),
    };

    unsigned currentFrame = 4;
};
