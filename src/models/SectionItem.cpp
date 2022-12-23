
#include "SectionItem.hpp"

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

struct SectionItem::Impl
{
    SectionItem* q;
    
    Impl(SectionItem* q) : q(q) {}
    
    QList<QSharedPointer<Image>> data;
    /* contains the name of the section items */
    QVariant varId;
};

SectionItem::~SectionItem()
{
    xThreadGuard(this);
}

/* Constructs an section object of the image list model. */ 
SectionItem::SectionItem()
   : d(std::make_unique<Impl>(this)), AbstractItem(ListItemType::Section)
{
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QVariant. */ 
SectionItem::SectionItem(QVariant &itemid)
   : d(std::make_unique<Impl>(this)), AbstractItem(ListItemType::Section)
{
   this->setItemID(itemid);
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QString. */
SectionItem::SectionItem(QString &itemid)
   : d(std::make_unique<Impl>(this)), AbstractItem(ListItemType::Section)
{
   this->setItemID(itemid);
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QDate. */
SectionItem::SectionItem(QDate &itemid)
   : d(std::make_unique<Impl>(this)), AbstractItem(ListItemType::Section)
{
   this->setItemID(itemid);
}

/* Returns the name of the section item as a QString value. If no name is set, an empty value is returned. */
QString SectionItem::getName() const
{
   if(this->varId.isValid())
   {
      return this->varId.toString();
   }
   else
   {
      return QString();
   }
}

/* Sets the name (itemid) of the section item as the type of QVariant. */
void SectionItem::setItemID(QVariant &itemid)
{
   this->varId = itemid;
}

/* Sets the name (itemid) of the section item as the type of QString. */
void SectionItem::setItemID(QString &itemid)
{
   this->varId = QVariant(itemid);
}

/* Sets the name (itemid) of the section item as the type of QDate. */
void SectionItem::setItemID(QDate &itemid)
{
   this->varId = QVariant(itemid);
}

/* Returns the name of the section item as a QVariant value. */
QVariant SectionItem::getItemID() const
{
   return this->varId;
}

/* Sorts the images items of the item according to given the field (field) and the order (order). */ 
void SectionItem::sortItems(IBImageListModel::IBImageSortField field, Qt::SortOrder order)
{
   std::sort(this->begin(), this->end(), [field, order](IBImageListImageItem *itemA, IBImageListImageItem *itemB)
     {
        switch(field)
        {
           case IBImageListModel::SortByName:
              if(order ==  Qt::AscendingOrder)
              {
                 return QString::compare(itemA->getName(), itemB->getName(), Qt::CaseInsensitive) < 0;
              }
              else
              {
                 return QString::compare(itemA->getName(), itemB->getName(), Qt::CaseInsensitive) > 0;
              }

           case IBImageListModel::SortByDate:
              if(order ==  Qt::AscendingOrder)
              {
                 return itemA->getLastModified().date() < itemB->getLastModified().date();
              }
              else
              {
                 return itemA->getLastModified().date() > itemB->getLastModified().date();
              }


           case IBImageListModel::SortByFileType:
              if(order ==  Qt::AscendingOrder)
              {
                 return QString::compare(itemA->getFileType(), itemB->getFileType(), Qt::CaseInsensitive) > 0;
              }
              else
              {
                 return QString::compare(itemA->getFileType(), itemB->getFileType(), Qt::CaseInsensitive) > 0;
              }
        }
        return false;
     });
}

/* Returns true if the name of the item is less than the given item (item). Otherwise false is returned. 
   If the name is of type QString, then the name of item is lexically less than the name of the given item. 
   If the name is of type QDate, then the name of item is older than the name of the given item. */
bool SectionItem::operator< (const SectionItem &item) noexcept(false)
{
   if(this->varId.typeId() != item.varId.typeId())
   {
     return false;
   }

   if(this->varId.typeId() == QMetaType::QDate)
   {
      return this->varId.toDate() < item.varId.toDate();
   }
   else
   {
      return this->varId.toString() < item.varId.toString();
   }
}

/* Returns true if the name of the item is greater than the given item (item). Otherwise false is returned. 
   If the name is of type QString, then the name of item is lexically greater than the name of the given item. 
   If the name is of type QDate, then the name of item is newer than the name of the given item. */
bool SectionItem::operator> (const SectionItem &item) noexcept(false)
{
   if(this->varId.typeId() != item.varId.typeId())
   {
     return false;
   }

   if(this->varId.typeId() == QMetaType::QDate)
   {
      return this->varId.toDate() > item.varId.toDate();
   }
   else
   {
      return this->varId.toString() > item.varId.toString();
   }
}
