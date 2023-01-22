
#include "ImageSectionDataContainer.hpp"

#include "SectionItem.hpp"
#include "Image.hpp"
#include "SortedImageModel.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"

#include <QApplication>
#include <mutex>

struct ImageSectionDataContainer::Impl
{
    ImageSectionDataContainer* q;
    
    // mutex which protects concurrent access to data
    std::recursive_mutex m;
    SectionList data;
    SortedImageModel* model = nullptr;

    SortField sectionSortField = SortField::None;
    SortField imageSortField = SortField::None;

    // returns true if the column that is sorted against requires us to preload the image metadata
    // before we insert the items into the model
    static constexpr bool sortedColumnNeedsPreloadingMetadata(SortField sectionField, SortField imgField)
    {
        switch (sectionField)
        {
        case SortField::None:
        case SortField::FileName:
        case SortField::FileSize:
        case SortField::FileType:
        case SortField::DateModified:
            switch (imgField)
            {
            case SortField::FileName:
            case SortField::FileSize:
            case SortField::FileType:
            case SortField::DateModified:
                return false;
            default:
                break;
            }
        default:
            break;
        }

        return true;
    }

};

ImageSectionDataContainer::ImageSectionDataContainer(SortedImageModel* model) : d(std::make_unique<Impl>())
{
    d->q = this;
    d->model = model;
}

ImageSectionDataContainer::~ImageSectionDataContainer() = default;

bool ImageSectionDataContainer::addImageItem(const QFileInfo& info)
{
    std::vector<QSharedPointer<SmartImageDecoder>>* decoderList;

    auto image = DecoderFactory::globalInstance()->makeImage(info);
    auto decoder = QSharedPointer<SmartImageDecoder>(DecoderFactory::globalInstance()->getDecoder(image).release());

    // move both objects to the UI thread to ensure proper signal delivery
    image->moveToThread(QGuiApplication::instance()->thread());

    d->model->connect(image.data(), &Image::decodingStateChanged, d->model,
        [&](Image* img, quint32 newState, quint32 old)
        { onBackgroundImageTaskStateChanged(img, newState, old); }
    , Qt::QueuedConnection);
    d->model->connect(image.data(), &Image::thumbnailChanged, d->model,
        [&](Image*, QImage) { updateLayout(); });
    auto con = d->model->connect(image.data(), &Image::checkStateChanged, d->model,
        [&](Image*, int c, int old)
        {
            if (c != old)
            {
                this->numberOfCheckedImages += (c == Qt::Unchecked) ? -1 : +1;
            }
        });

    this->numberOfCheckedImages += (image->checked() == Qt::Unchecked) ? 0 : +1;
    this->entries.push_back(std::make_pair(image, decoder));

    if (decoder)
    {
        try
        {
            if (decoderList == nullptr || d->sortedColumnNeedsPreloadingMetadata(d->sectionSortField, d->imageSortField))
            {
                decoder->open();
                // decode synchronously
                decoder->decode(DecodingState::Metadata, iconSize);
                decoder->close();
            }
            else
            {
                decoderList->emplace_back(std::move(decoder));
            }

            QString str;
            QVariant var;
            switch (d->sectionSortField)
            {
            case SortField::DateModified:
                var = info.lastModified().date();
                break;

            case SortField::FileName:
                str = image->getName();
                if (str[0].isDigit())
                {
                    str = QStringLiteral("#");
                }
                else
                {
                    str = str.left(1).toUpper();
                }
                var = str;
                break;

            case SortField::FileType:
                var = info.suffix().toUpper();
                break;

            case SortField::None:
                break;
            default:
                throw std::logic_error("requested section sort type not yet implemented");
            }

            this->addImageItem(var, image);
        }
        catch (const std::runtime_error& e)
        {
            // Runtime errors that occurred while opening the file or decoding the image, should be ignored here.
            // Just keep adding the file to the list, any error will be visible in the ThumbnailView later.
        }

        return true;
    }
    else
    {
        QMetaObject::invokeMethod(image.data(), &Image::lookupIconFromFileType);
    }
    return false;
}

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

    auto rowCount = this->size();
    if (rowCount != 0)
    {
        d->model->beginRemoveRows(QModelIndex(), 0, rowCount - 1);
    }
    for (SectionList::iterator it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        (*it)->clear();
    }
    this->d->data.clear();

    if (rowCount != 0)
    {
        d->model->endRemoveRows();
    }
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
void ImageSectionDataContainer::sortImageItems(SortField imageSortField, Qt::SortOrder order)
{
    std::lock_guard<std::recursive_mutex> l(d->m);
    for (auto it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        (*it)->sortItems(imageSortField, order);
    }
    d->imageSortField = imageSortField;
}

/* Sorts the section item according to given the order (order). */
void ImageSectionDataContainer::sortSections(SortField sectionSortField, Qt::SortOrder order)
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
    d->sectionSortField = sectionSortField;
}
