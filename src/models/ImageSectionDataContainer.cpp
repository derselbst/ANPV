
#include "ImageSectionDataContainer.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QDir>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring> // for strverscmp()

#ifdef _WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#endif

#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "WaitCursor.hpp"
#include "Image.hpp"
#include "ANPV.hpp"
#include "ProgressIndicatorHelper.hpp"

struct ImageSectionDataContainer::Impl
{
    ImageSectionDataContainer* q;
    
    QList<QList<QSharedPointer<SectionItem>>> data;
    
};

ImageSectionDataContainer::ImageSectionDataContainer(QObject* parent) : d(std::make_unique<Impl>())
{
    d->q = this;
}

ImageSectionDataContainer::~ImageSectionDataContainer()
{
    xThreadGuard(this);
}


/* Adds a given item (item) to a given section item (section). If the section item does not exist, it will be created. */
void ImageSectionDataContainer::addImageItem(QVariant &section, Image *item)
{
   QList<SectionItem *>::iterator it;
   SectionItem *newsec;

   bool found = false;

   for(it = this->begin(); it != this->end() && !found; ++it)
   {
      if((*it) == section)
      {
        (*it)->append(item);
        found = true;
      }
   }

   if(!found)
   {
      newsec = new SectionItem(section);
      newsec->append(item);
      this->append(newsec);
   }  
}

/* Adds a given item (item) to a given section item (section). If the section item does not exist, it will be created. */
void ImageSectionDataContainer::addImageItem(QString &section, Image *item)
{
   QVariant sec = section;
   this->addImageItem(sec, item);
}

/* Adds a given item (item) to a given section item (section). If the section item does not exist, it will be created. */
void ImageSectionDataContainer::addImageItem(QDate &section, Image *item)
{
   QVariant sec = section;
   this->addImageItem(sec, item);
}

/* Return the item of a given index (index). The 2D data list are handled like a 1D list. */ 
AbstractListItem *ImageSectionDataContainer::getItemByLinearIndex(int index) const
{
   QList<SectionItem *>::const_iterator it;
   AbstractListItem *retitem = nullptr;
   int revidx;

   if(this->isEmpty() && index < 0)
   {
      return nullptr;
   }

   if(this->first() == this->last() && this->first()->getName().isEmpty())
   {
      if(index < this->first()->size())
      {
         retitem = this->first()->at(index);
      }
   }
   else
   {
      revidx = index;
      for(it = this->begin(); it != this->end() && !retitem; ++it)
      {
         if(revidx <= (*it)->size())
         {
            if(revidx == 0)
            {
               retitem = (*it); 
            }
            else
            {
               retitem = (*it)->at(revidx - 1);
            }
         }

         revidx -= (*it)->size() + 1;
      }
   }

   return retitem;
}

/* Return the index of a given item (item). The 2D data list are handled like a 1D list. */ 
int ImageSectionDataContainer::getLinearIndexOfItem(AbstractListItem *item) const
{
   QList<SectionItem *>::const_iterator sit;
   SectionItem::const_iterator iit;
   int itmidx = 0;
   
   if(!item)
   {
      return -1;
   }
 
   if(this->first() == this->last() && this->first()->getName().isEmpty())
   {
      for(iit = this->first()->begin(); iit != this->first()->end(); ++iit)
      {
         if((*iit) == item)
         {
            return itmidx;
         }
         itmidx++;
       }
   }
   else
   {
      for(sit = this->begin(); sit != this->end(); ++sit)
      {
         if((*sit) == item)
         {
            return itmidx; 
         }
         itmidx++;

         for(iit = (*sit)->begin(); iit != (*sit)->end(); ++iit)
         {
            if((*iit) == item)
            {
               return itmidx;
            }
            itmidx++;
         }
      }
   }

   return -1;
}

/* reimpl. */
void ImageSectionDataContainer::clear()
{
   QList<SectionItem *>::iterator it;

   for(it = this->begin(); it != this->end(); ++it)
   {
      (*it)->clear();
   }
   QList<SectionItem *>::clear();
}

/* Returns the number of section items and its image items. If only one section item exists and 
   its name is empty, then the number of its image items is returned. */
int ImageSectionDataContainer::totalSize() const
{
   QList<SectionItem *>::const_iterator it;
   int size;
 
   if(this->isEmpty())
   {
      return 0;
   }

   if(this->first() == this->last() && this->first()->getName().isEmpty())
   {
      size = this->first()->size();
   }
   else
   {
      size = this->size();
      for(it = this->begin(); it != this->end(); ++it)
      {
         size += (*it)->size();
      }
   }
   
   return size;
}

/* Invoke the sorting the images items of the section items according to given the field (field) and the order (order). */
void ImageSectionDataContainer::sortImageItems(IBImageListModel::IBImageSortField field, Qt::SortOrder order)
{
   QList<SectionItem *>::iterator it;

   for(it = this->begin(); it != this->end(); ++it)
   {
      (*it)->sortItems(field, order);
   } 
}

/* Sorts the section item according to given the order (order). */
void ImageSectionDataContainer::sortSections(Qt::SortOrder order)
{
   std::sort(this->begin(), this->end(), [order](SectionItem *itemA, SectionItem *itemB)
                                             { 
                                                if(order == Qt::AscendingOrder)
                                                {
                                                    return (*itemA) < (*itemB); 
                                                }
                                                return  (*itemA) > (*itemB);});
}
