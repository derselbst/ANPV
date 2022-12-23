
#pragma once

#include "DecodingState.hpp"
#include "types.hpp"

#include <QFileInfo>
#include <QSharedPointer>
#include <memory>

class Image;
class SmartImageDecoder;

class ImageSectionDataContainer
{
public:
    void addImageItem(QSharedPointer<Image> image);
    bool removeImageItem(QSharedPointer<Image> image);
    bool removeImageItem(QFileInfo info);
    
    QSharedPointer<AbstractListItem> getItemByLinearIndex(int idx);
    getLinearIndexOfItem(const AbstractListItem* item);
    int size();
    bool clear(bool force);
    
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
