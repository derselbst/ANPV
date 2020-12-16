#include "Settings.hpp"


Settings::Settings() :  QSettings (QSettings::IniFormat, QSettings::UserScope, "ANPV", "ANPV")
{
}

Settings::~Settings() = default;

QString Settings::fileOperationTargetDir(int entry)
{
    switch(entry)
    {
        case 0:
            return this->value("F1/targetDir").toString();
        case 1:
            return this->value("F2/targetDir").toString();
        default:
            return this->value("F3/targetDir").toString();
    }
}

void Settings::setFileOperationTargetDir(int entry, const QString& dir)
{
    switch(entry)
    {
        case 0:
            this->setValue("F1/targetDir", dir);
            break;
        case 1:
            this->setValue("F2/targetDir", dir);
            break;
        default:
            this->setValue("F3/targetDir", dir);
            break;
    }
}
