#include "CenteredBoxProxyStyle.hpp"
#include "SortedImageModel.hpp"

#include <QRect>
#include <QStyleOptionViewItem>


// credits to: https://wiki.qt.io/Center_a_QCheckBox_or_Decoration_in_an_Itemview
QRect CenteredBoxProxyStyle::subElementRect(QStyle::SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    const QRect baseRes = QProxyStyle::subElementRect(element, option, widget);

    switch(element)
    {
    case SE_ItemViewItemCheckIndicator:
    {
        const QStyleOptionViewItem *const itemOpt = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        const QVariant alignData = itemOpt ? itemOpt->index.data(SortedImageModel::CheckAlignmentRole) : QVariant();

        if(itemOpt && !alignData.isNull())
        {
            return QStyle::alignedRect(itemOpt->direction, alignData.value<Qt::Alignment>(), baseRes.size() * 1.5, itemOpt->rect);
        }

        break;
    }

    case SE_ItemViewItemDecoration:
    {
        const QStyleOptionViewItem *const itemOpt = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        const QVariant alignData = itemOpt ? itemOpt->index.data(SortedImageModel::DecorationAlignmentRole) : QVariant();

        if(itemOpt && !alignData.isNull())
        {
            return QStyle::alignedRect(itemOpt->direction, alignData.value<Qt::Alignment>(), baseRes.size(), itemOpt->rect);
        }

        break;
    }

    case SE_ItemViewItemFocusRect:
    {
        const QStyleOptionViewItem *const itemOpt = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        const QVariant checkAlignData = itemOpt ? itemOpt->index.data(SortedImageModel::CheckAlignmentRole) : QVariant();
        const QVariant decorationAlignData = itemOpt ? itemOpt->index.data(SortedImageModel::DecorationAlignmentRole) : QVariant();

        if(!checkAlignData.isNull() || !decorationAlignData.isNull())  // when it is not null, then the focus rect should be drawn over the complete cell
        {
            return option->rect;
        }

        break;
    }

    default:
        break;
    }

    return baseRes;
}
