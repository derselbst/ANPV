
#pragma once

#include <QObject>
#include <QThread>
#include <QFuture>
#include <memory>

#include "ImageSectionDataContainer.hpp"

class FileDiscoveryThread : public QThread
{
   Q_OBJECT

   public:
     FileDiscoveryThread(ImageSectionDataContainer *data = nullptr, QObject *parent = nullptr);
     ~FileDiscoveryThread();

     void run() override;

   signals:
      void imageLoaded(int index);

   private:
     struct Impl;
     std::unique_ptr<Impl> d;
};

