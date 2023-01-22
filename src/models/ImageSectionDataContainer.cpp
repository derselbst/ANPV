
#include "ImageSectionDataContainer.hpp"

#include "SectionItem.hpp"
#include "Image.hpp"
#include "SortedImageModel.hpp"

#include <mutex>

struct ImageSectionDataContainer::Impl
{
    ImageSectionDataContainer* q;
    
    // mutex which protects concurrent access to data
    std::recursive_mutex m;
    SectionList data;
    SortedImageModel* model = nullptr;
};

ImageSectionDataContainer::ImageSectionDataContainer(SortedImageModel* model) : d(std::make_unique<Impl>())
{
    d->q = this;
    d->model = model;
}

ImageSectionDataContainer::~ImageSectionDataContainer() = default;

/* Adds a given item (item) to a given section item (section). If the section item does not exist, it will be created. */
void ImageSectionDataContainer::addImageItem(const QVariant& section, QSharedPointer<Image> item)
{
    SectionList::iterator it;
    std::lock_guard<std::recursive_mutex> l(d->m);

    for (it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        if ((*it).data() == section)
        {
            break;
        }
    }

    if (this->d->data.end() == it)
    {
        // no suitable section found, create a new one
        it = this->d->data.insert(it, SectionList::value_type(new SectionItem(section)));
    }

    auto insertIt = (*it)->findInsertPosition(item);
    int insertIdx = this->getLinearIndexOfItem((*insertIt).data());

    d->model->beginInsertRows(QModelIndex(), insertIdx, insertIdx);
    (*it)->insert(insertIt, item);
    d->model->endInsertRows();
}

/* Return the item of a given index (index). The 2D data list are handled like a 1D list. */ 
QSharedPointer<AbstractListItem> ImageSectionDataContainer::getItemByLinearIndex(int index) const
{
    QSharedPointer<AbstractListItem> retitem;
    std::lock_guard<std::recursive_mutex> l(d->m);

    if (this->d->data.empty() && index < 0)
    {
        return nullptr;
    }

    if (this->d->data.front() == this->d->data.back() && this->d->data.front()->getName().isEmpty())
    {
        if (index < this->d->data.front()->size())
        {
            retitem = this->d->data.front()->at(index);
        }
    }
    else
    {
        int revidx = index;
        for (auto it = this->d->data.begin(); it != this->d->data.end() && !retitem; ++it)
        {
            if (revidx <= (*it)->size())
            {
                if (revidx == 0)
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
int ImageSectionDataContainer::getLinearIndexOfItem(const AbstractListItem* item) const
{
    int itmidx = 0;

    if (!item)
    {
        return -1;
    }

    std::lock_guard<std::recursive_mutex> l(d->m);

    if (this->d->data.front() == this->d->data.back() && this->d->data.front()->getName().isEmpty())
    {
        if (this->d->data.front()->find(item, &itmidx))
        {
            return itmidx;
        }
    }
    else
    {
        for (SectionList::const_iterator sit = this->d->data.begin(); sit != this->d->data.end(); ++sit)
        {
            if ((*sit) == item)
            {
                return itmidx;
            }
            itmidx++;

            if ((*sit)->find(item, &itmidx))
            {
                return itmidx;
            }
        }
    }

    return -1;
}

void ImageSectionDataContainer::clear()
{
   std::lock_guard<std::recursive_mutex> l(d->m);

   for(SectionList::iterator it = this->d->data.begin(); it != this->d->data.end(); ++it)
   {
      (*it)->clear();
   }
   this->d->data.clear();
}

/* Returns the number of section items and its image items. If only one section item exists and 
   its name is empty, then the number of its image items is returned. */
int ImageSectionDataContainer::size() const
{
   int size = 0;
   std::lock_guard<std::recursive_mutex> l(d->m);
 
   if(this->d->data.empty())
   {
      return size;
   }

   if(this->d->data.front() == this->d->data.back() && this->d->data.front()->getName().isEmpty())
   {
      size = this->d->data.front()->size();
   }
   else
   {
      size = this->size();
      for(auto it = this->d->data.begin(); it != this->d->data.end(); ++it)
      {
         size += (*it)->size();
      }
   }
   
   return size;
}

/* Invoke the sorting the images items of the section items according to given the field (field) and the order (order). */
void ImageSectionDataContainer::sortImageItems(ImageSortField field, Qt::SortOrder order)
{
    std::lock_guard<std::recursive_mutex> l(d->m);
   for(auto it = this->d->data.begin(); it != this->d->data.end(); ++it)
   {
      (*it)->sortItems(field, order);
   } 
}

/* Sorts the section item according to given the order (order). */
void ImageSectionDataContainer::sortSections(Qt::SortOrder order)
{
    std::lock_guard<std::recursive_mutex> l(d->m);
    std::sort(this->d->data.begin(), this->d->data.end(), [order](SectionItem* itemA, SectionItem* itemB)
        {
            if (order == Qt::AscendingOrder)
            {
                return (*itemA) < (*itemB);
            }
            return  (*itemA) > (*itemB);
        });
}
