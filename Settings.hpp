#pragma once

#include <QSettings>
#include <QString>

class Settings : public QSettings
{
public:
    Settings();
    ~Settings() override;

    QString fileOperationTargetDir(int entry=0);
    void setFileOperationTargetDir(int entry, const QString& dir);
};

