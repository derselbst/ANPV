
#pragma once

#include <QObject>
#include <QThread>
#include <QFuture>
#include <memory>

#include "ImageSectionDataContainer.hpp"

class DirectoryWorker : public QObject
{
    Q_OBJECT

public:
    DirectoryWorker(ImageSectionDataContainer* data = nullptr, QObject* parent = nullptr);
    ~DirectoryWorker();

    QFuture<DecodingState> changeDirAsync(const QString& dir);

signals:
    void discoverDirectory(QString newDir);

public slots:
    void onDiscoverDirectory(QString);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

