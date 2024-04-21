/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */

/* Modified by derselbst for ANPV */

#include "ListItemDelegate.hpp"

#include <QFutureWatcher>

#include "types.hpp"
#include "SortedImageModel.hpp"
#include "ANPV.hpp"
#include "ProgressIndicatorHelper.hpp"

/* Constructs a ItemDelegate object. */
ListItemDelegate::ListItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent), szSectionSize(40, 40)
{
}

/* reimpl. */
void ListItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    bool issection;

    if(!index.isValid())
    {
        return;
    }
    auto* model = index.model();

    issection = model->data(index, SortedImageModel::ItemIsSection).toBool();

    if(issection)
    {
        this->paintSection(painter, option, index);
    }
    else
    {
        auto task = qvariant_cast<QSharedPointer<QFutureWatcher<DecodingState>>>(model->data(index, SortedImageModel::ItemBackgroundTask));
        if (task && task->isRunning())
        {
            this->paintProgressIcon(painter, option, index, task.data());
        }
        else
        {
            this->QStyledItemDelegate::paint(painter, option, index);
        }
    }
}

void ListItemDelegate::paintProgressIcon(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const QFutureWatcher<DecodingState>* task) const
{
    // transform bounds, otherwise fills the whole cell
    auto bounds = option.rect;
    //bounds.setWidth(28);
    //bounds.moveTo(option.rect.center().x() - bounds.width() / 2, option.rect.center().y() - bounds.height() / 2);

    ANPV::globalInstance()->spinningIconHelper()->drawProgressIndicator(painter, bounds, *task);
}

/* Paints a section item with a given model index (index) and options (option) on a painter object (painter). */
void ListItemDelegate::paintSection(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QString fname;
    QFont paintfont;

    fname = index.model()->data(index, SortedImageModel::ItemName).toString();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(option.palette.window());
    painter->setPen(option.palette.color(QPalette::Text));

    paintfont = painter->font();
    paintfont.setPixelSize(30);
    paintfont.setBold(true);
    painter->setFont(paintfont);

    painter->fillRect(option.rect, option.palette.base());
    painter->drawText(option.rect - QMargins(10, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, fname);

    painter->restore();
}

/* reimpl. */
QSize ListItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option)

    bool issection = index.model()->data(index, SortedImageModel::ItemIsSection).toBool();

    if(issection)
    {
        return this->szSectionSize;
    }
    else
    {
        return this->QStyledItemDelegate::sizeHint(option, index);
    }
}

/* Sets a new size of the section item. */
void ListItemDelegate::resizeSectionSize(const QSize &newsize)
{
    this->szSectionSize.setWidth(newsize.width());
}
