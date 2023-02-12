
#pragma once

#include "DecodingState.hpp"
#include "types.hpp"
#include "AbstractListItem.hpp"
#include "SectionItem.hpp"

#include <QFileInfo>
#include <QSharedPointer>
#include <QFutureWatcher>
#include <memory>

class Image;
class SortedImageModel;

class ImageSectionDataContainer
{
public:
    using SectionList = std::vector<QSharedPointer<SectionItem>>;

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
            case SortField::None:
                throw std::logic_error("SortField::None should not be used for images");
            default:
                break;
            }
        default:
            break;
        }

        return true;
    }


    ImageSectionDataContainer(SortedImageModel* model);
    ~ImageSectionDataContainer();

    bool addImageItem(const QFileInfo& info);
    void addImageItem(const QVariant& section, QSharedPointer<Image>& item, QSharedPointer<QFutureWatcher<DecodingState>>& watcher);
    bool removeImageItem(QSharedPointer<Image> image);
    bool removeImageItem(const QFileInfo& info);
    
    QSharedPointer<AbstractListItem> getItemByLinearIndex(int idx) const;
    int getLinearIndexOfItem(const AbstractListItem* item) const;
    int size() const;
    void clear();

    void sortImageItems(SortField, Qt::SortOrder order);
    void sortSections(SortField, Qt::SortOrder order);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
