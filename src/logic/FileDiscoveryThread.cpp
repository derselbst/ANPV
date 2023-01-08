
#include "FileDiscoveryThread.hpp"

struct Impl
{
    ImageSectionDataContainer* data;
};


/* Constructs the thread for loading the image thumbnails with size of the thumbnails (thumbsize) and
   the image list (data). */
FileDiscoveryThread::FileDiscoveryThread(ImageSectionDataContainer* data, QObject *parent)
   : d(std::make_unique<Impl>()), QThread(parent)
{
    d->data = data;
}

FileDiscoveryThread::~FileDiscoveryThread() = default;

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
