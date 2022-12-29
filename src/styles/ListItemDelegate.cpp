/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */

#include "ListItemDelegate.hpp"

#include "types.hpp"

/* Constructs a ItemDelegate object. */
ListItemDelegate::ListItemDelegate(QObject *parent)
    : QAbstractItemDelegate(parent), szItemSize(240,180), szSectionSize(40,40)
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

   issection = index.model()->data(index, IBImageListModel::ItemIsSection).toBool();

   if(issection)
   {
      this->paintSection(painter, option, index);
   }
   else
   {
      this->paintItem(painter, option, index);
   }
}

/* Paints a section item with a given model index (index) and options (option) on a painter object (painter). */
void ListItemDelegate::paintSection(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QString fname;
   QFont paintfont;

   fname = index.model()->data(index, IBImageListModel::ItemName).toString();

   painter->save();
   painter->setRenderHint(QPainter::Antialiasing, true);
   painter->setBrush(option.palette.window());
   painter->setPen(option.palette.color(QPalette::Text));

   paintfont = painter->font();
   paintfont.setPixelSize(30);
   paintfont.setBold(true);
   painter->setFont(paintfont);

   painter->fillRect(option.rect, option.palette.base());
   painter->drawText(option.rect - QMargins(10,0,0,0), Qt::AlignLeft | Qt::AlignVCenter, fname);
    
   painter->restore();
}

/* Paints a normal item with a given model index (index) and options (option) on a painter object (painter). */
void ListItemDelegate::paintItem(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   QString ftype, fname;
   QRect hbufrect(0 ,0, option.rect.width(),  option.rect.height());
   QPixmap thumbnail, hbufpxmp(hbufrect.width(),  hbufrect.height());
   QPainter *hbufpainter;
   QSize imagesize;
   QFont paintfont;
   bool imageloaded;
   
   ftype = index.model()->data(index, IBImageListModel::ItemFileType).toString();
   fname = index.model()->data(index, IBImageListModel::ItemName).toString();
   thumbnail = index.model()->data(index, IBImageListModel::ItemThumbnail).value<QPixmap>();
   imagesize = index.model()->data(index, IBImageListModel::ItemImageSize).toSize();
   imageloaded = index.model()->data(index, IBImageListModel::ItemImageLoaded).toBool();

   /* start painting of item on a pixmap to prevent text flickering */
   hbufpainter = new QPainter(&hbufpxmp);
   hbufpainter->setRenderHint(QPainter::Antialiasing, true);

   if (option.state & QStyle::State_Selected)
   {
      hbufpainter->fillRect(hbufrect, option.palette.highlight());
   }
   else
   {
      hbufpainter->fillRect(hbufrect, option.palette.base());
   }

   hbufpainter->setBrush(Qt::NoBrush);
   if (option.state & QStyle::State_Selected)
   {
      hbufpainter->setPen(option.palette.color(QPalette::HighlightedText));
   }
   else
   {
      hbufpainter->setPen(option.palette.color(QPalette::Text));
   }

   hbufpainter->drawRect(4, 4, hbufrect.width() - 8, hbufrect.height() - 46);
   
   hbufpainter->drawPixmap(4 + ((hbufrect.width() - 8 - thumbnail.width()) / 2),
                           4 + ((hbufrect.height() - 46 - thumbnail.height()) / 2),
                           thumbnail);

   paintfont = hbufpainter->font();
   paintfont.setPixelSize(16);
   paintfont.setBold(true);
   hbufpainter->setFont(paintfont);

   if (option.state & QStyle::State_Selected)
   {
      hbufpainter->setBrush(option.palette.highlightedText());
   }
   else
   {
      hbufpainter->setBrush(option.palette.window());
   }

   hbufpainter->drawText(QRect(hbufrect.x() + 4, hbufrect.y() + hbufrect.height() - 43, 
                         hbufrect.width() - 8, hbufrect.height() - 28), 
                         Qt::AlignHCenter, fname);
    
   paintfont.setBold(false);
   paintfont.setPixelSize(14);
   hbufpainter->setFont(paintfont);
   hbufpainter->drawText(QRect(hbufrect.x() + 4, hbufrect.y() + hbufrect.height() - 23, 
                         hbufrect.width() / 2, hbufrect.height() - 10), 
                         Qt::AlignLeft,
                         imageloaded ? QString("Size: %1x%2").arg(imagesize.width()).arg(imagesize.height()) : QStringLiteral("Loading..."));
   

   hbufpainter->drawText(QRect(hbufrect.x() + 4 + (hbufrect.width() / 2), 
                           hbufrect.y() + hbufrect.height() - 23, 
                           (hbufrect.width() / 2) - 8, hbufrect.y() + hbufrect.height() - 10), 
                           Qt::AlignRight, QString("Type: %1").arg(ftype.toUpper()));

   delete hbufpainter;
   /* end painting of item on a pixmap */

   /* draw generated pixmap on view */
   painter->save();
   painter->drawPixmap(option.rect, hbufpxmp);
   painter->restore();
}

/* reimpl. */
QSize ListItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
   Q_UNUSED(option)

   bool issection = index.model()->data(index, IBImageListModel::ItemIsSection).toBool();

   if(issection)
   {
      return this->szSectionSize;
   }
   else
   {
      return this->szItemSize;
   }
}

/* Sets a new size of the section item. */
void ListItemDelegate::resizeSectionSize(const QSize &newsize)
{
   this->szSectionSize.setWidth(newsize.width());
}
