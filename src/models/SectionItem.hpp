
#pragma once

#include "DecodingState.hpp"
#include "AbstractListItem.hpp"
#include "types.hpp"

#include <QFileInfo>
#include <QSharedPointer>
#include <memory>

class Image;
class SmartImageDecoder;


class SectionItem : public AbstractListItem
{
public:
    SectionItem();
    SectionItem(QVariant &itemid);
    SectionItem(QString &itemid);
    SectionItem(QDate &itemid);

    QString getName() const override;
    
    void setItemID(QVariant &itemid);
    void setItemID(QString &itemid);
    void setItemID(QDate &itemid);
    QVariant getItemID() const;

    void sortItems(ImageSortField field = ImageSortField::SortByName,
                    Qt::SortOrder order = Qt::AscendingOrder);

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
