/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */

#ifndef H_IBIMAGEITEMDELEGATE
#define H_IBIMAGEITEMDELEGATE

#include <QStyledItemDelegate>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSize>

class ListItemDelegate : public QStyledItemDelegate
{
public:
    ListItemDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void resizeSectionSize(const QSize &newsize);

protected:
    void paintSection(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    /* size of a section item */
    QSize szSectionSize;
};

#endif /*H_IBIMAGEITEMDELEGATE*/
