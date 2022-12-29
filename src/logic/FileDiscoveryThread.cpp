
#include "FileDiscoveryThread.hpp"


/* Constructs the thread for loading the image thumbnails */
FileDiscoveryThread::FileDiscoveryThread(QObject *parent)
   : QThread(parent), lstFileData(nullptr), szThumbnailSize(QSize(0,0))
{
}

/* Constructs the thread for loading the image thumbnails with size of the thumbnails (thumbsize) and
   the image list (data). */
FileDiscoveryThread::FileDiscoveryThread(QSize &thumbsize, QList<IBImageListImageItem *> *data, QObject *parent)
   : QThread(parent), lstFileData(data), szThumbnailSize(thumbsize)
{
}

/* Loads the images and invokes the creation of the thumbnail. It is finished, the signal imageLoaded is emitted. */
void FileDiscoveryThread::run()
{
   QList<IBImageListImageItem *>::iterator it;
   QPixmap pixtmp;

   if(!this->lstFileData)
   {
      return;
   }
  
   for(it = this->lstFileData->begin(); it != this->lstFileData->end(); ++it)
   {
      if((*it)->getType() == IBImageListAbstractItem::Image)
      {
         dynamic_cast<IBImageListImageItem *>(*it)->loadImage(this->szThumbnailSize);
         emit imageLoaded(it - this->lstFileData->begin());
      }
   }
}

/* Sets the size (size) of the thumbnails. */
void FileDiscoveryThread::setThumbnailSize(QSize &size)
{
   this->szThumbnailSize = size;
}

/* Returns the size of the thumbnails. */
QSize FileDiscoveryThread::getThumbnailSize() const
{
   return this->szThumbnailSize; 
}

/* Sets the image list (data) to be handled. */
void FileDiscoveryThread::setImageList(QList<IBImageListImageItem *> *data)
{
   this->lstFileData = data;
}

/* Returns the handled image list. */
QList<IBImageListImageItem *> *FileDiscoveryThread::getImageList() const
{
   return this->lstFileData;
}
