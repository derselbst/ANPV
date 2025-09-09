
#include <QString>

class RoundshotTagInterpreter
{
public:
    RoundshotTagInterpreter() = delete;

    static QString decodeDateTime(const QString& panoTag);

    static int imageIndex(const QString& panoTag);
    static int numberOfImages(const QString& panoTag);
    static double yaw(const QString& panoTag);
    static double pitch(const QString& panoTag);
    static int roll(const QString& panoTag);

private:
    static int decodeChar(char c);
};
