
#pragma once

#include "DecodingState.hpp"
#include "AbstractListItem.hpp"
#include "types.hpp"

#include <QFileInfo>
#include <QSharedPointer>
#include <memory>
#include <deque>

class Image;
class SmartImageDecoder;

class SectionItem : public AbstractListItem
{
public:
    using ImageList = std::deque<QSharedPointer<Image>>;

    SectionItem();
    SectionItem(const QVariant &itemid, SortField field, Qt::SortOrder order);
    ~SectionItem() override;

    QString getName() const override;
    
    void setItemID(const QVariant &itemid);
    QVariant getItemID() const;

    void sortItems(SortField field,
                    Qt::SortOrder order = Qt::AscendingOrder);

    ImageList::iterator findInsertPosition(const QSharedPointer<Image>& img);
    ImageList::iterator begin();
    bool isEnd(const SectionItem::ImageList::iterator& it) const;
    bool find(const AbstractListItem* item, int* externalIdx);
    bool find(QFileInfo item, int* externalIdx);
    int find(const QFileInfo info, ImageList::iterator* itout);
    void insert(ImageList::iterator it, QSharedPointer<Image>& img);
    void erase(ImageList::iterator it);
    size_t size() const;
    QSharedPointer<AbstractListItem> at(int idx) const;
    void clear();

    /*operator <*/
    bool operator< (const SectionItem &item) noexcept(false);

    /*operator >*/
    bool operator> (const SectionItem &item) noexcept(false);

    /*operator ==*/
    inline bool operator==(const SectionItem *item) noexcept(false)
            { return this->varId == item->varId; };
    friend inline bool operator==(const SectionItem *item, const QVariant &data) noexcept(false)
            { return item->varId == data; };

    /*operator !=*/
    inline bool operator!=(const SectionItem *item) noexcept(false)
            { return this->varId != item->varId; };
    friend inline bool operator!=(const SectionItem *item, const QVariant &data) noexcept(false)
            { return item->varId != data; };

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    /* contains the name of the section items */
    QVariant varId;
};
