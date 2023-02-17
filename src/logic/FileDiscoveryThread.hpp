
#pragma once

#include <QObject>
#include <QThread>
#include <QFuture>
#include <memory>

#include "ImageSectionDataContainer.hpp"

class FileDiscoveryThread : public QObject
{
    Q_OBJECT

public:
    FileDiscoveryThread(ImageSectionDataContainer* data = nullptr, QObject* parent = nullptr);
    ~FileDiscoveryThread();

    QFuture<DecodingState> changeDirAsync(const QString& dir);

signals:
    void discoverDirectory(QString newDir);

public slots:
    void onDiscoverDirectory(QString);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

