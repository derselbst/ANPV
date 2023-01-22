
#pragma once

#include "DecodingState.hpp"
#include "types.hpp"
#include "AbstractListItem.hpp"
#include "SectionItem.hpp"

#include <QFileInfo>
#include <QSharedPointer>
#include <memory>

class Image;
class SortedImageModel;

class ImageSectionDataContainer
{
public:
    using SectionList = std::list<QSharedPointer<SectionItem>>;

    ImageSectionDataContainer(SortedImageModel* model);
    ~ImageSectionDataContainer();

    void addImageItem(const QVariant& section, QSharedPointer<Image> item);
    bool removeImageItem(QSharedPointer<Image> image);
    bool removeImageItem(QFileInfo info);
    
    QSharedPointer<AbstractListItem> getItemByLinearIndex(int idx) const;
    int getLinearIndexOfItem(const AbstractListItem* item) const;
    int size() const;
    void clear();

    void sortImageItems(ImageSortField field, Qt::SortOrder order);
    void sortSections(Qt::SortOrder order);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
