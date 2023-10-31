/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */

/* Modified by derselbst for ANPV */

#include "ImageSectionDataContainer.hpp"

#include "SectionItem.hpp"
#include "Image.hpp"
#include "SortedImageModel.hpp"
#include "DecoderFactory.hpp"
#include "SmartImageDecoder.hpp"
#include "ExifWrapper.hpp"

#include <QApplication>
#include <QPersistentModelIndex>
#include <QFutureWatcher>

#include <mutex>

struct ImageSectionDataContainer::Impl
{
    ImageSectionDataContainer* q = nullptr;

    // might stay null for standalone ImageSectionDataContainers
    SortedImageModel* model = nullptr;
    
    // mutex which protects concurrent access to members below
    std::recursive_mutex m;
    SectionList data;

    SortField sectionSortField = SortField::None;
    Qt::SortOrder sectionSortOrder = Qt::DescendingOrder;

    SortField imageSortField = SortField::None;
    Qt::SortOrder imageSortOrder = Qt::DescendingOrder;
    
    Qt::ConnectionType syncConnection()
    {
        if (QThread::currentThread() == this->model->thread())
        {
            return Qt::DirectConnection;
        }
        return Qt::BlockingQueuedConnection;
    }
    
    std::function<bool(const SectionList::value_type& left, const SectionList::value_type& right)> getSortFunction(Qt::SortOrder order)
    {
        switch (order)
        {
        default:
        case Qt::AscendingOrder:
            return [](const SectionList::value_type& left, const SectionList::value_type& right) { return left->getName().isEmpty() || (*left) < (*right); };

        case Qt::DescendingOrder:
            return [](const SectionList::value_type& left, const SectionList::value_type& right) { return left->getName().isEmpty() || (*left) > (*right); };
        };
    }

    SectionList::iterator findInsertPosition(const SectionList::value_type& section)
    {
        auto upper = std::upper_bound(this->data.begin(), this->data.end(), section, this->getSortFunction(this->sectionSortOrder));
        return upper;
    }
    
    std::list<QSharedPointer<AbstractListItem>> flatListForUI()
    {
        std::list<QSharedPointer<AbstractListItem>> result;
        for (SectionList::const_iterator sit = this->data.begin(); sit != this->data.end(); ++sit)
        {
            auto size = (*sit)->size();
            if(size != 0)
            {
                result.emplace_back(*sit);
                for (size_t i = 0; i < size; i++)
                {
                    auto item = (*sit)->at(i);
                    result.emplace_back(std::move(item));
                }
            }
        }
        
        return result;
    }
};

ImageSectionDataContainer::ImageSectionDataContainer(SortedImageModel* model) : d(std::make_unique<Impl>())
{
    d->q = this;
    d->model = model;
}

ImageSectionDataContainer::~ImageSectionDataContainer()
{
    qDebug() << "~ImageDatacontainer";
}

bool ImageSectionDataContainer::addImageItem(const QFileInfo& info)
{
    auto image = DecoderFactory::globalInstance()->makeImage(info);
    auto decoder = QSharedPointer<SmartImageDecoder>(DecoderFactory::globalInstance()->getDecoder(image).release());

    // Let the image live in the same thread as the SortedDirModel. This allows Qt::DirectConnection between Image::destroyed and SortedDirModel to remove it from the list of checked images.
    // Additionally, the image should live in the UI thread to allow direct Image::previewImageUpdated event delivery to the DocumentView, even when the background thread is busy. Otherwise the DocumentView would show an incompletely decoded image until the background thread (which would own the image by default) enqueues that particular event...
    Q_ASSERT(d->model == nullptr || QGuiApplication::instance()->thread() == d->model->thread());
    image->moveToThread(QGuiApplication::instance()->thread());

    QSharedPointer<QFutureWatcher<DecodingState>> watcher;
    QVariant var;
    if (decoder)
    {
        try
        {
            image->setDecoder(decoder);
            if (this->sortedColumnNeedsPreloadingMetadata(d->sectionSortField, d->imageSortField))
            {
                decoder->open();
                // decode synchronously
                decoder->decode(DecodingState::Metadata, QSize());
                decoder->close();
                d->model ? d->model->welcomeImage(image, watcher) : (void)0;
            }
            else
            {
                watcher.reset(new QFutureWatcher<DecodingState>());
                // Keep the watcher in the background thread, as there seems to be no need to move it to UI thread.
                //watcher->moveToThread(QGuiApplication::instance()->thread());
                d->model ? d->model->welcomeImage(image, watcher) : (void)0;

                // decode asynchronously
                auto fut = decoder->decodeAsync(DecodingState::Metadata, Priority::Background, QSize());
                watcher->setFuture(fut);
            }

            QString str;
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
            {
                double f;
                long l;
                QDateTime dt;
                auto exif = image->exif();
                if (!exif.isNull())
                {
                    switch (d->sectionSortField)
                    {
                    case SortField::DateRecorded:
                        dt = exif->dateRecorded();
                        if (dt.isValid())
                        {
                            var = dt.date();
                        }
                        break;
                    case SortField::Aperture:
                        if (exif->aperture(f))
                        {
                            var = f;
                        }
                        break;
                    case SortField::Exposure:
                        var = exif->exposureTime();
                        break;
                    case SortField::Iso:
                        if (exif->iso(l))
                        {
                            var = qlonglong(l);
                        }
                        break;
                    case SortField::FocalLength:
                        var = exif->focalLength();
                        break;
                    case SortField::Lens:
                        var = exif->lens();
                        break;
                    case SortField::CameraModel:
                        throw std::logic_error("ItemImageCameraModel not yet implemented");
                    default:
                        throw std::logic_error("requested section sort type not yet implemented");
                    }
                }
                break;
            }
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
    d->model ? d->model->welcomeImage(image, watcher) : (void)0;
    this->addImageItem(var, image);
    return false;
}

/* Adds a given item (item) to a given section item (section). If the section item does not exist, it will be created. */
void ImageSectionDataContainer::addImageItem(const QVariant& section, QSharedPointer<Image>& item)
{
    SectionList::iterator it;
    std::unique_lock<std::recursive_mutex> l(d->m);

    int insertIdx = 0;
    for (it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        if ((*it).data() == section)
        {
            break;
        }
        insertIdx += (*it)->size();
    }

    std::list<QSharedPointer<AbstractListItem>> itemsForUIModel;
    if (this->d->data.end() == it)
    {
        // no suitable section found, create a new one
        insertIdx = 0;
        auto s = SectionList::value_type(new SectionItem(section, d->imageSortField, d->imageSortOrder));
        auto sectionInsertPos = d->findInsertPosition(s);
        for (auto sit = this->d->data.begin(); sit != sectionInsertPos; ++sit)
        {
            insertIdx += (*sit)->size();
            insertIdx++; // for the section itself
        }

        it = this->d->data.insert(sectionInsertPos, s);
        auto itemInsertIt = (*it)->findInsertPosition(item);
        (*it)->insert(itemInsertIt, item);

        int check = this->getLinearIndexOfItem(item.data());

        itemsForUIModel.push_back(s);
        itemsForUIModel.push_back(item);
    }
    else
    {
        auto insertIt = (*it)->findInsertPosition(item);
        auto d = std::distance((*it)->begin(), insertIt);

        (*it)->insert(insertIt, item);

        insertIdx = this->getLinearIndexOfItem(item.data());

        itemsForUIModel.push_back(item);
    }
    
    if (d->model)
    {
        QMetaObject::invokeMethod(d->model, [insertIdx, itemsForUIModel, this]()
            {
                auto copy = itemsForUIModel;
                d->model->insertRows(insertIdx, copy);
                Q_ASSERT(copy.empty());
            }, Qt::AutoConnection);
    }
}

bool ImageSectionDataContainer::removeImageItem(const QFileInfo& info)
{
    std::lock_guard<std::recursive_mutex> l(d->m);
    int globalIdx = 0;
    for (SectionList::const_iterator sit = this->d->data.begin(); sit != this->d->data.end(); ++sit)
    {
        ++globalIdx; // increment because we've entered a new section
        SectionItem::ImageList::iterator it;
        int localIdx = (*sit)->find(info, &it);
        if (localIdx >= 0)
        {
            int endIdxToRemove = globalIdx + localIdx;
            int startIdxToRemove = endIdxToRemove;
            if ((*sit)->size() == 1)
            {
                // There is only one item left in that section which we are going to remove. Therefore, remove the entire section
                --startIdxToRemove;
            }

            (*sit)->erase(it);
            if ((*sit)->size() == 0)
            {
                this->d->data.erase(sit);
            }

            if (d->model)
            {
                QMetaObject::invokeMethod(d->model, [startIdxToRemove, endIdxToRemove, this]()
                    {
                        d->model->removeRows(startIdxToRemove, endIdxToRemove - startIdxToRemove + 1);
                    }, Qt::AutoConnection);
            }
            return true;
        }
        globalIdx += (*sit)->size();
    }

    return false;
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

    if (this->d->data.empty())
    {
        return -1;
    }

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

    return -1;
}

void ImageSectionDataContainer::clear()
{
    std::unique_lock<std::recursive_mutex> l(d->m);
    
    auto rowCount = this->size();
    if (rowCount == 0)
    {
        return;
    }

    if (d->model)
    {
        QMetaObject::invokeMethod(d->model, [rowCount, this]()
            {
                d->model->removeRows(0, rowCount);
            }, Qt::AutoConnection);
    }

    for (SectionList::iterator it = this->d->data.begin(); it != this->d->data.end(); ++it)
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

    if (this->d->data.empty())
    {
        return size;
    }

    size = this->d->data.size();
    for (auto it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        size += (*it)->size();
    }

    return size;
}

/* Invoke the sorting the images items of the section items according to given the field (field) and the order (order). */
void ImageSectionDataContainer::sortImageItems(SortField imageSortField, Qt::SortOrder order)
{
    std::lock_guard<std::recursive_mutex> l(d->m);
    
    d->imageSortField = imageSortField;
    d->imageSortOrder = order;
    
    auto rowCount = this->size();
    if(rowCount == 0)
    {
        return;
    }

    if (d->model)
    {
        QMetaObject::invokeMethod(d->model, [rowCount, this]()
            {
                d->model->removeRows(0, rowCount);
            }, Qt::AutoConnection);
    }

    for (auto it = this->d->data.begin(); it != this->d->data.end(); ++it)
    {
        (*it)->sortItems(imageSortField, order);
    }
    
    if (d->model)
    {
        auto itemsForUIModel = d->flatListForUI();
        QMetaObject::invokeMethod(d->model, [itemsForUIModel, this]()
            {
                auto copy = itemsForUIModel;
                d->model->insertRows(0, copy);
                Q_ASSERT(copy.empty());
            }, Qt::AutoConnection);
    }
}

/* Sorts the section item according to given the order (order). */
void ImageSectionDataContainer::sortSections(SortField sectionSortField, Qt::SortOrder order)
{
    std::lock_guard<std::recursive_mutex> l(d->m);

    d->sectionSortOrder = order;
    d->sectionSortField = sectionSortField;

    // the code below is incomplete...
    return;

    auto rowCount = this->size();
    if(rowCount == 0)
    {
        return;
    }
    
    if (d->model)
    {
        QMetaObject::invokeMethod(d->model, [rowCount, this]()
            {
                d->model->removeRows(0, rowCount);
            }, Qt::AutoConnection);
    }
    std::sort(this->d->data.begin(), this->d->data.end(), d->getSortFunction(order));

    // ...because it is missing recreation of all section items here

    if (d->model)
    {
        auto itemsForUIModel = d->flatListForUI();
        QMetaObject::invokeMethod(d->model, [itemsForUIModel, this]()
            {
                auto copy = itemsForUIModel;
                d->model->insertRows(0, copy);
                Q_ASSERT(copy.empty());
            }, Qt::AutoConnection);
    }
}

void ImageSectionDataContainer::decodeAllImages(DecodingState state, int imageHeight)
{
    std::unique_lock<std::recursive_mutex> l(d->m);

    for (SectionList::const_iterator sit = this->d->data.begin(); sit != this->d->data.end(); ++sit)
    {
        auto size = (*sit)->size();
        for (size_t i = 0; i < size; i++)
        {
            auto item = (*sit)->at(i);
            auto image = AbstractListItem::imageCast(item);
            const QSharedPointer<SmartImageDecoder>& decoder = image->decoder();
            if (decoder)
            {
                bool taken = QThreadPool::globalInstance()->tryTake(decoder.get());
                if (taken)
                {
                    qWarning() << "Decoder '0x" << (void*)decoder.get() << "' was surprisingly taken from the ThreadPool's Queue???";
                }
                QImage thumb = image->thumbnail();
                if (state == DecodingState::PreviewImage && !thumb.isNull())
                {
                    qDebug() << "Skipping preview decoding of " << image->fileInfo().fileName() << " as it already has a thumbnail of sufficient size.";
                    continue;
                }

                QSharedPointer<QFutureWatcher<DecodingState>> watcher(new QFutureWatcher<DecodingState>());

                QSize fullResSize = image->size();
                QSize desiredResolution = fullResSize.isValid()
                    ? fullResSize.scaled(1, imageHeight, Qt::KeepAspectRatioByExpanding)
                    : QSize(imageHeight, imageHeight);
                // decode asynchronously
                auto fut = decoder->decodeAsync(state, Priority::Background, desiredResolution);
                watcher->setFuture(fut);
                fut.then(
                        [=](DecodingState result)
                        {
                            decoder->releaseFullImage();
                            return result;
                        }).onCanceled(
                            [=]
                            {
                                decoder->releaseFullImage();
                                return DecodingState::Cancelled;
                            }).onFailed(
                                [=](DecodingState result)
                                {
                                    decoder->releaseFullImage();
                                    return result;
                                }
                            );

                d->model ? d->model->welcomeImage(image, watcher) : (void)0;
            }
        }
    }
}

QSharedPointer<Image> ImageSectionDataContainer::goTo(const ViewFlags_t& viewFlags, const Image* img, int stepsFromCurrent) const
{
    std::lock_guard<std::recursive_mutex> l(d->m);

    auto idx = this->getLinearIndexOfItem(img);
    if (idx < 0)
    {
        qWarning() << "ImageSectionDataContainer::goTo(): requested image not found";
        return {};
    }

    int size = this->size();
    int step = (stepsFromCurrent < 0) ? -1 : 1;

    QSharedPointer<Image> returnImg;
    do
    {
        if (idx >= size - step || // idx + step >= size
            idx < -step) // idx + step < 0
        {
            return {};
        }

        idx += step;

        auto item = this->getItemByLinearIndex(idx);
        if (item->getType() != ListItemType::Image)
        {
            continue;
        }

        returnImg = AbstractListItem::imageCast(item);
        Q_ASSERT(!returnImg.isNull());


        QFileInfo eInfo = returnImg->fileInfo();
        bool shouldSkip = eInfo.suffix() == "bak";
        shouldSkip |= returnImg->hideIfNonRawAvailable(viewFlags);
        if (returnImg->hasDecoder() && !shouldSkip)
        {
            stepsFromCurrent -= step;
        }
        else
        {
            // skip unsupported files
        }

    } while (stepsFromCurrent);

    return returnImg;
}

