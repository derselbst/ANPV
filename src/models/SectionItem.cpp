
#include "SectionItem.hpp"

#include "Image.hpp"
#include "xThreadGuard.hpp"
#include "types.hpp"

struct SectionItem::Impl
{
    SectionItem* q;
    
    Impl(SectionItem* q) : q(q) {}
    
    QList<QSharedPointer<Image>> data;
};

SectionItem::~SectionItem() = default;

/* Constructs an section object of the image list model. */ 
SectionItem::SectionItem()
   : d(std::make_unique<Impl>(this)), AbstractListItem(ListItemType::Section)
{
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QVariant. */ 
SectionItem::SectionItem(QVariant &itemid)
   : d(std::make_unique<Impl>(this)), AbstractListItem(ListItemType::Section)
{
   this->setItemID(itemid);
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QString. */
SectionItem::SectionItem(QString &itemid)
   : d(std::make_unique<Impl>(this)), AbstractListItem(ListItemType::Section)
{
   this->setItemID(itemid);
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QDate. */
SectionItem::SectionItem(QDate &itemid)
   : d(std::make_unique<Impl>(this)), AbstractListItem(ListItemType::Section)
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
void SectionItem::sortItems(ImageSortField field, Qt::SortOrder order)
{
    std::sort(this->d->data.begin(), this->d->data.end(), [field, order](QSharedPointer<Image>& itemA, QSharedPointer<Image>& itemB)
        {
            int aBeforeB;
            switch (field)
            {
            case ImageSortField::SortByName:
                aBeforeB = QString::compare(itemA->fileInfo().fileName(), itemB->fileInfo().fileName(), Qt::CaseInsensitive);
                return (order == Qt::AscendingOrder) ? aBeforeB < 0 : aBeforeB > 0;

            case ImageSortField::SortByDate:
                aBeforeB = itemA->fileInfo().lastModified().date() < itemB->fileInfo().lastModified().date();
                return (order == Qt::AscendingOrder) ? aBeforeB != 0 : aBeforeB == 0;

            case ImageSortField::SortByFileType:
                aBeforeB = QString::compare(itemA->fileInfo().suffix(), itemB->fileInfo().suffix(), Qt::CaseInsensitive);
                return (order == Qt::AscendingOrder) ? aBeforeB < 0 : aBeforeB > 0;

            default:
                throw std::logic_error("Unkown case in SectionItem::sortItems");
            }
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
