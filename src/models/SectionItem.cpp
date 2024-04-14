/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */

/* Modified by derselbst for ANPV */

#include "SectionItem.hpp"

#include "Image.hpp"
#include "xThreadGuard.hpp"
#include "types.hpp"
#include "ExifWrapper.hpp"
#include "ImageSectionDataContainer.hpp"

#ifdef _WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#endif

struct SectionItem::Impl
{
    SectionItem *q;

    Impl(SectionItem *q) : q(q) {}

    ImageList data;

    SortField imageSortField = SortField::None;
    Qt::SortOrder imageSortOrder = Qt::DescendingOrder;

    static bool compareFileName(const QFileInfo &linfo, const QFileInfo &rinfo)
    {
#ifdef _WINDOWS
        std::wstring l = linfo.fileName().toCaseFolded().toStdWString();
        std::wstring r = rinfo.fileName().toCaseFolded().toStdWString();
        return StrCmpLogicalW(l.c_str(), r.c_str()) < 0;
#else
        QByteArray lfile = linfo.fileName().toCaseFolded().toUtf8();
        QByteArray rfile = rinfo.fileName().toCaseFolded().toUtf8();
        return strverscmp(lfile.constData(), rfile.constData()) < 0;
#endif
    }

    template<SortField SortCol>
    static bool sortColumnPredicateLeftBeforeRight(const QSharedPointer<Image> &limg, const QFileInfo &linfo, const QSharedPointer<Image> &rimg, const QFileInfo &rinfo)
    {
        if constexpr(SortCol == SortField::FileName)
        {
            // nothing to do here, we use the fileName comparison below
        }
        else if constexpr(SortCol == SortField::FileSize)
        {
            return linfo.size() < rinfo.size();
        }
        else if constexpr(SortCol == SortField::FileType)
        {
            return linfo.suffix().toUpper() < rinfo.suffix().toUpper();
        }
        else if constexpr(SortCol == SortField::DateModified)
        {
            return linfo.lastModified() < rinfo.lastModified();
        }

        bool leftFileNameIsBeforeRight = compareFileName(linfo, rinfo);

        if constexpr(ImageSectionDataContainer::sortedColumnNeedsPreloadingMetadata(SortCol, SortCol))
        {
            // only evaluate exif() when sortedColumnNeedsPreloadingMetadata() is true!
            auto lexif = limg->exif();
            auto rexif = rimg->exif();

            if(lexif && rexif)
            {
                if constexpr(SortCol == SortField::DateRecorded)
                {
                    QDateTime ltime = lexif->dateRecorded();
                    QDateTime rtime = rexif->dateRecorded();

                    if(ltime.isValid() && rtime.isValid())
                    {
                        if(ltime != rtime)
                        {
                            return ltime < rtime;
                        }
                    }
                    else if(ltime.isValid())
                    {
                        return true;
                    }
                    else if(rtime.isValid())
                    {
                        return false;
                    }
                }
                else if constexpr(SortCol == SortField::Resolution)
                {
                    QSize lsize = limg->size();
                    QSize rsize = rimg->size();

                    if(lsize.isValid() && rsize.isValid())
                    {
                        if(lsize.width() != rsize.width() && lsize.height() != rsize.height())
                        {
                            return static_cast<size_t>(lsize.width()) * lsize.height() < static_cast<size_t>(rsize.width()) * rsize.height();
                        }
                    }
                    else if(lsize.isValid())
                    {
                        return true;
                    }
                    else if(rsize.isValid())
                    {
                        return false;
                    }
                }
                else if constexpr(SortCol == SortField::Aperture)
                {
                    double lap, rap;
                    lap = rap = std::numeric_limits<double>::max();

                    lexif->aperture(lap);
                    rexif->aperture(rap);

                    if(lap != rap)
                    {
                        return lap < rap;
                    }
                }
                else if constexpr(SortCol == SortField::Exposure)
                {
                    double lex, rex;
                    lex = rex = std::numeric_limits<double>::max();

                    lexif->exposureTime(lex);
                    rexif->exposureTime(rex);

                    if(lex != rex)
                    {
                        return lex < rex;
                    }
                }
                else if constexpr(SortCol == SortField::Iso)
                {
                    int64_t liso, riso;
                    liso = riso = std::numeric_limits<int64_t>::max();

                    lexif->iso(liso);
                    rexif->iso(riso);

                    if(liso != riso)
                    {
                        return liso < riso;
                    }
                }
                else if constexpr(SortCol == SortField::FocalLength)
                {
                    double ll, rl;
                    ll = rl = std::numeric_limits<double>::max();

                    lexif->focalLength(ll);
                    rexif->focalLength(rl);

                    if(ll != rl)
                    {
                        return ll < rl;
                    }
                }
                else if constexpr(SortCol == SortField::Lens)
                {
                    QString ll, rl;

                    ll = lexif->lens();
                    rl = rexif->lens();

                    if(!ll.isEmpty() && !rl.isEmpty())
                    {
                        return ll < rl;
                    }
                    else if(!ll.isEmpty())
                    {
                        return true;
                    }
                    else if(!rl.isEmpty())
                    {
                        return false;
                    }
                }
                else if constexpr(SortCol == SortField::CameraModel)
                {
                    throw std::logic_error("not yet implemented");
                }
                else
                {
                    //                 static_assert("Unknown SortField to sort for");
                }
            }
            else if(lexif && !rexif)
            {
                return true; // l before r
            }
            else if(!lexif && rexif)
            {
                return false; // l behind r
            }
        }

        return leftFileNameIsBeforeRight;
    }

    // This is the entry point for sorting. It sorts all Directories first.
    // Second criteria is to sort according to fileName
    // For regular files it dispatches the call to sortColumnPredicateLeftBeforeRight()
    //
    // |   L  \   R    | DIR  | SortCol | UNKNOWN |
    // |      DIR      |  1   |   1     |    1    |
    // |     SortCol   |  0   |   1     |    1    |
    // |    UNKNOWN    |  0   |   0     |    1    |
    //
    template<SortField SortCol>
    static bool topLevelSortFunction(Qt::SortOrder order, const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
    {
        const QFileInfo &linfo = l->fileInfo();
        const QFileInfo &rinfo = r->fileInfo();

        bool leftIsBeforeRight;

        switch(order)
        {
        default:
        case Qt::AscendingOrder:
            leftIsBeforeRight =
                (linfo.isDir() && (!rinfo.isDir() || compareFileName(linfo, rinfo))) ||
                (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(l, linfo, r, rinfo));
            break;

        case Qt::DescendingOrder:
            leftIsBeforeRight =
                (linfo.isDir() && (!rinfo.isDir() || compareFileName(linfo, rinfo))) ||
                (!rinfo.isDir() && sortColumnPredicateLeftBeforeRight<SortCol>(r, rinfo, l, linfo));
            break;
        }

        return leftIsBeforeRight;
    }

    std::function<bool(const QSharedPointer<Image>&, const QSharedPointer<Image>&)> getSortFunction(SortField field, Qt::SortOrder order)
    {
        switch(field)
        {
        case SortField::FileName:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::FileName>(order, l, r);
            };

        case SortField::FileSize:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::FileSize>(order, l, r);
            };

        case SortField::FileType:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::FileType>(order, l, r);
            };

        case SortField::DateModified:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::DateModified>(order, l, r);
            };

        case SortField::Resolution:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::Resolution>(order, l, r);
            };

        case SortField::DateRecorded:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::DateRecorded>(order, l, r);
            };

        case SortField::Aperture:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::Aperture>(order, l, r);
            };

        case SortField::Exposure:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::Exposure>(order, l, r);
            };

        case SortField::Iso:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::Iso>(order, l, r);
            };

        case SortField::FocalLength:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::FocalLength>(order, l, r);
            };

        case SortField::Lens:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::Lens>(order, l, r);
            };

        case SortField::CameraModel:
            return [ = ](const QSharedPointer<Image> &l, const QSharedPointer<Image> &r)
            {
                return topLevelSortFunction<SortField::CameraModel>(order, l, r);
            };

        default:
            throw std::logic_error(Formatter() << "No sorting function implemented for SortField " << (int)field);
        }
    }
};

SectionItem::~SectionItem() = default;

/* Constructs an section object of the image list model. */
SectionItem::SectionItem()
    : AbstractListItem(ListItemType::Section), d(std::make_unique<Impl>(this))
{
}

/* Constructs an section object of the image list model with the name (itemid) as the type of QVariant. */
SectionItem::SectionItem(const QVariant &itemid, SortField field, Qt::SortOrder order)
    : AbstractListItem(ListItemType::Section), d(std::make_unique<Impl>(this))
{
    this->setItemID(itemid);
    d->imageSortField = field;
    d->imageSortOrder = order;
}

/* Returns the name of the section item as a QString value. If no name is set, an empty value is returned. */
QString SectionItem::getName() const
{
    if(this->varId.isValid())
    {
        switch(this->varId.typeId())
        {
        case QMetaType::QDate:
            return this->varId.toDate().toString("yyyy-MM-dd (dddd)");

        default:
            return this->varId.toString();
        }
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
    d->imageSortField = field;
    d->imageSortOrder = order;
    auto sortFunction = d->getSortFunction(field, order);
    std::sort(/*std::execution::par_unseq, */this->d->data.begin(), this->d->data.end(), sortFunction);
}

SectionItem::ImageList::iterator SectionItem::findInsertPosition(const QSharedPointer<Image> &img)
{
    auto sortFunction = d->getSortFunction(d->imageSortField, d->imageSortOrder);
    auto upper = std::upper_bound(this->d->data.begin(), this->d->data.end(), img, sortFunction);
    return upper;
}

SectionItem::ImageList::iterator SectionItem::begin()
{
    return d->data.begin();
}

bool SectionItem::isEnd(const SectionItem::ImageList::iterator &it) const
{
    return it == this->d->data.end();
}

bool SectionItem::find(const AbstractListItem *item, int *externalIdx)
{
    auto it = std::find_if(this->d->data.begin(), this->d->data.end(),
                           [ = ](const QSharedPointer<Image> &entry)
    {
        return entry.data() == item;
    });

    *externalIdx += std::distance(this->d->data.begin(), it);
    return it != this->d->data.end();
}

bool SectionItem::find(QFileInfo item, int *externalIdx)
{
    auto it = std::find_if(this->d->data.begin(), this->d->data.end(),
                           [ = ](const QSharedPointer<Image> &entry)
    {
        return entry.data()->fileInfo() == item;
    });

    *externalIdx += std::distance(this->d->data.begin(), it);
    return it != this->d->data.end();
}


int SectionItem::find(const QFileInfo info, ImageList::iterator *itout)
{
    auto it = std::find_if(this->d->data.begin(), this->d->data.end(),
                           [ = ](const QSharedPointer<Image> &entry)
    {
        return entry->fileInfo() == info;
    });

    if(it == this->d->data.end())
    {
        return -1;
    }

    if(itout)
    {
        *itout = it;
    }

    return std::distance(this->d->data.begin(), it);
}

void SectionItem::insert(ImageList::iterator it, QSharedPointer<Image> &img)
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
