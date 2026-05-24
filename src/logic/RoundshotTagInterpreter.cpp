
#include "RoundshotTagInterpreter.hpp"

#include <QString>
#include <string>
#include <stdexcept>

int RoundshotTagInterpreter::decodeChar(char c)
{
    if ('0' <= c && c <= '9')
    {
        return (int)c - '0';
    }
    else if ('A' <= c && c <= 'Z')
    {
        return ((int)c - 'A') + 10;
    }
    else if ('a' <= c && c <= 'z')
    {
        return ((int)c - 'a') + 36;
    }

    throw std::invalid_argument(std::string("Unexpected char argument: ") + c);
}

QString RoundshotTagInterpreter::decodeDateTime(const QString& panoTag)
{
    static const QLatin1Char filler('0');
    auto arr = panoTag.toLatin1();

    if (arr.size() < 7)
    {
        throw std::invalid_argument("panoTag had unexpected size");
    }

    return QString("%1%2%3%4%5%6")
        .arg(panoTag.mid(0, 2)) // YY
        .arg(QString::number(decodeChar(arr[2])), 2, filler) // MM
        .arg(QString::number(decodeChar(arr[3])), 2, filler) // DD
        .arg(QString::number(decodeChar(arr[4])), 2, filler) // hh
        .arg(QString::number(decodeChar(arr[5])), 2, filler) // mm
        .arg(QString::number(decodeChar(arr[6])), 2, filler) // ss
        ;
}

int RoundshotTagInterpreter::imageIndex(const QString& panoTag)
{
    if (panoTag.size() == 31)
    {
        return panoTag.mid(18, 4).toInt();
    }
    throw std::invalid_argument("panoTag had unexpected size");
}

int RoundshotTagInterpreter::numberOfImages(const QString& panoTag)
{
    if (panoTag.size() >= 12+4)
    {
        return panoTag.mid(11, 4).toInt();
    }
    throw std::invalid_argument("panoTag had unexpected size");
}

double RoundshotTagInterpreter::yaw(const QString& panoTag)
{
    if (panoTag.size() == 31)
    {
        return (panoTag.mid(22, 5)).toDouble() / 10.0;
    }
    throw std::invalid_argument("panoTag had unexpected size");
}

double RoundshotTagInterpreter::pitch(const QString& panoTag)
{
    if (panoTag.size() == 31)
    {
        return (panoTag.mid(27, 4)).toDouble() / 10.0;
    }
    throw std::invalid_argument("panoTag had unexpected size");
}

int RoundshotTagInterpreter::roll(const QString& panoTag)
{
    enum lensType
    {
        RectPortrait = 0,
        FishPortrait = 1,
        RectLandscape = 2,
        FishLandscape = 3,
    };

    if (panoTag.size() >= 19)
    {
        int lensType = panoTag.mid(17, 1).toInt();
        switch (lensType)
        {
        case RectPortrait:
        case FishPortrait:
            return 90;
        case RectLandscape:
        case FishLandscape:
            return 0;
        default:
            throw std::invalid_argument("unknown lenstype");
        }
    }
}

