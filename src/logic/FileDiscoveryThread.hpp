
#pragma once

#include <QObject>
#include <QSize>
#include <QThread>

class FileDiscoveryThread : public QThread
{
   Q_OBJECT

   public:
     FileDiscoveryThread(QObject *parent = nullptr);
     FileDiscoveryThread(QSize &thumbsize, QList<IBImageListImageItem *> *data = nullptr, QObject *parent = nullptr); 

     void run() override;

     void setThumbnailSize(QSize &size);
     QSize getThumbnailSize() const;

     void setImageList(QList<IBImageListImageItem *> *data);
     QList<IBImageListImageItem *> *getImageList() const;

   signals:
      void imageLoaded(int index);

   private:
     /* represents a pointer to file data list to be handled */
     QList<IBImageListImageItem *> *lstFileData;
     /* size of the thumbnails */
     QSize szThumbnailSize;
};

