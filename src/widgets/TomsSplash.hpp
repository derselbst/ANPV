
#include <QSplashScreen>

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
            this->setPixmap(QPixmap(QLatin1String(frameFile[currentFrame++]), "jpg", Qt::NoFormatConversion));
        }
        QSplashScreen::showMessage(message, Qt::AlignLeft | Qt::AlignTop);
    }

private:
    static constexpr const char* frameFile[] =
    {
        ":/images/splash/1.jpg",
        ":/images/splash/2.jpg",
        ":/images/splash/3.jpg",
        ":/images/splash/4.jpg",
        ":/images/splash/5.jpg",
        ":/images/splash/6.jpg",
        ":/images/splash/7.jpg",
        ":/images/splash/8.jpg",
        ":/images/splash/9.jpg",
        ":/images/splash/10.jpg",
        ":/images/splash/11.jpg",
        ":/images/splash/12.jpg",
        ":/images/splash/13.jpg",
        ":/images/splash/14.jpg",
        ":/images/splash/15.jpg",
        ":/images/splash/16.jpg",
        ":/images/splash/17.jpg",
    };

    unsigned currentFrame = 4;
};
