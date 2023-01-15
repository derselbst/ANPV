
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
    SectionItem(const QVariant &itemid);

    QString getName() const override;
    
    void setItemID(const QVariant &itemid);
    QVariant getItemID() const;

    void sortItems(ImageSortField field = ImageSortField::SortByName,
                    Qt::SortOrder order = Qt::AscendingOrder);

    ImageList::iterator findInsertPosition(const QSharedPointer<Image>& img);
    bool find(const AbstractListItem* item, int* externalIdx);
    void insert(ImageList::iterator it, QSharedPointer<Image>& img);
    size_t size() const;
    QSharedPointer<AbstractListItem> at(int idx) const;
    void clear();

    /*operator <*/
    bool operator< (const SectionItem &item) noexcept(false);
    friend inline bool operator<(const SectionItem *item, const QDate &date) noexcept(false)
            { return item->varId.toDate() < date; }
    friend inline bool operator<(const SectionItem *item, const QString &str) noexcept(false)
            { return item->varId.toString() < str; };

    /*operator >*/
    bool operator> (const SectionItem &item) noexcept(false);
    friend bool operator> (const SectionItem *item, const QVariant &data) noexcept(false);
    friend inline bool operator>(const SectionItem *item, const QDate &date) noexcept(false)
            { return item->varId.toDate() > date; }
    friend inline bool operator>(const SectionItem *item, const QString &str) noexcept(false)
            { return item->varId.toString() > str; };

    /*operator ==*/
    inline bool operator==(const SectionItem *item) noexcept(false)
            { return this->varId == item->varId; };
    friend inline bool operator==(const SectionItem *item, const QVariant &data) noexcept(false)
            { return item->varId == data; };
    friend inline bool operator==(const SectionItem *item, const QDate &date) noexcept(false)
            { return item->varId.toDate() == date; }
    friend inline bool operator==(const SectionItem *item, const QString &str) noexcept(false)
            { return item->varId.toString() == str; };

    /*operator !=*/
    inline bool operator!=(const SectionItem *item) noexcept(false)
            { return this->varId != item->varId; };
    friend inline bool operator!=(const SectionItem *item, const QVariant &data) noexcept(false)
            { return item->varId != data; };
    friend inline bool operator!=(const SectionItem *item, const QDate &date) noexcept(false)
            { return item->varId.toDate() != date; }
    friend inline bool operator!=(const SectionItem *item, const QString &str) noexcept(false)
            { return item->varId.toString() != str; };

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    /* contains the name of the section items */
    QVariant varId;
};
