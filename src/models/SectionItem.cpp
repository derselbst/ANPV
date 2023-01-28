
#include "SectionItem.hpp"

#include "Image.hpp"
#include "xThreadGuard.hpp"
#include "types.hpp"

struct SectionItem::Impl
{
    SectionItem* q;
    
    Impl(SectionItem* q) : q(q) {}
    
    ImageList data;
};

SectionItem::~SectionItem() = default;

/* Constructs an section object of the image list model. */ 
SectionItem::SectionItem()
   : d(std::make_unique<Impl>(this)), AbstractListItem(ListItemType::Section)
{
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QVariant. */ 
SectionItem::SectionItem(const QVariant &itemid)
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
void SectionItem::setItemID(const QVariant &itemid)
{
   this->varId = itemid;
}

/* Returns the name of the section item as a QVariant value. */
QVariant SectionItem::getItemID() const
{
   return this->varId;
}

/* Sorts the images items of the item according to given the field (field) and the order (order). */ 
void SectionItem::sortItems(SortField field, Qt::SortOrder order)
{
    std::sort(this->d->data.begin(), this->d->data.end(), [field, order](QSharedPointer<Image>& itemA, QSharedPointer<Image>& itemB)
        {
            int aBeforeB;
            switch (field)
            {
            case SortField::FileName:
                aBeforeB = QString::compare(itemA->getName(), itemB->getName(), Qt::CaseInsensitive);
                return (order == Qt::AscendingOrder) ? aBeforeB < 0 : aBeforeB > 0;

            case SortField::DateModified:
                aBeforeB = itemA->fileInfo().lastModified().date() < itemB->fileInfo().lastModified().date();
                return (order == Qt::AscendingOrder) ? aBeforeB != 0 : aBeforeB == 0;

            case SortField::FileType:
                aBeforeB = QString::compare(itemA->fileInfo().suffix(), itemB->fileInfo().suffix(), Qt::CaseInsensitive);
                return (order == Qt::AscendingOrder) ? aBeforeB < 0 : aBeforeB > 0;

            default:
                throw std::logic_error("Unkown case in SectionItem::sortItems");
            }
        });
}

SectionItem::ImageList::iterator SectionItem::findInsertPosition(const QSharedPointer<Image>& img)
{
    // TODO add the correct comparator
    auto upper = std::upper_bound(this->d->data.begin(), this->d->data.end(), img);
    return upper;
}

bool SectionItem::find(const AbstractListItem* item, int* externalIdx)
{
    auto it = std::find_if(this->d->data.begin(), this->d->data.end(),
        [=](const QSharedPointer<Image>& entry)
        {
            return entry.data() == item;
        });

    externalIdx += std::distance(this->d->data.begin(), it);
    return it != this->d->data.end();
}

int SectionItem::find(const QFileInfo info, ImageList::iterator* itout)
{
    auto it = std::find_if(this->d->data.begin(), this->d->data.end(),
        [=](const QSharedPointer<Image>& entry)
        {
            return entry->fileInfo() == info;
        });

    if(it == this->d->data.end())
    {
        return -1;
    }

    if (itout)
    {
        *itout = it;
    }

    return std::distance(this->d->data.begin(), it);
}

void SectionItem::insert(ImageList::iterator it, QSharedPointer<Image>& img)
{
    this->d->data.insert(it, img);
}

void SectionItem::erase(ImageList::iterator it)
{
    this->d->data.erase(it);
}

size_t SectionItem::size() const
{
    return this->d->data.size();
}

QSharedPointer<AbstractListItem> SectionItem::at(int idx) const
{
    return this->d->data.at(idx);
}

void SectionItem::clear()
{
    this->d->data.clear();
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
