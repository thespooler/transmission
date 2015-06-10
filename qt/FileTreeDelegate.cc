/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QApplication>
#include <QPainter>

#include "FileTreeDelegate.h"
#include "FileTreeModel.h"

QSize
FileTreeDelegate::sizeHint(const QStyleOptionViewItem& item, const QModelIndex& index) const
{
  QSize size;

  switch(index.column())
    {
      case FileTreeModel::COL_PROGRESS:
      case FileTreeModel::COL_WANTED:
        size = QSize(20, 1);
        break;

      default:
        size = QItemDelegate::sizeHint (item, index);
    }

  size.rheight() += 8; // make the spacing a little nicer
  return size;
}

void
FileTreeDelegate::paint (QPainter                    * painter,
                         const QStyleOptionViewItem  & option,
                         const QModelIndex           & index) const
{
  const int column(index.column());

  if ((column != FileTreeModel::COL_PROGRESS) && (column != FileTreeModel::COL_WANTED))
    {
      QItemDelegate::paint(painter, option, index);
      return;
    }

  QStyle * style (qApp->style ());

  painter->save();
  QItemDelegate::drawBackground (painter, option, index);

  if(column == FileTreeModel::COL_PROGRESS)
    {
      QStyleOptionProgressBar p;
      p.state = option.state | QStyle::State_Small;
      p.direction = qApp->layoutDirection();
      p.rect = option.rect;
      p.rect.setSize (QSize(option.rect.width()-2, option.rect.height()-8));
      p.rect.moveCenter (option.rect.center());
      p.fontMetrics = qApp->fontMetrics();
      p.minimum = 0;
      p.maximum = 100;
      p.textAlignment = Qt::AlignCenter;
      p.textVisible = true;
      p.progress = (int)(100.0*index.data().toDouble());
      p.text = QString::fromLatin1 ("%1%").arg (p.progress);
      style->drawControl(QStyle::CE_ProgressBar, &p, painter);
    }
  else if(column == FileTreeModel::COL_WANTED)
    {
      QStyleOptionButton o;
      o.state = option.state;
      o.direction = qApp->layoutDirection();
      o.rect.setSize (QSize(20, option.rect.height()));
      o.rect.moveCenter (option.rect.center());
      o.fontMetrics = qApp->fontMetrics();
      switch (index.data().toInt())
        {
          case Qt::Unchecked: o.state |= QStyle::State_Off; break;
          case Qt::Checked:   o.state |= QStyle::State_On; break;
          default:            o.state |= QStyle::State_NoChange;break;
        }
      style->drawControl (QStyle::CE_CheckBox, &o, painter);
    }

  QItemDelegate::drawFocus (painter, option, option.rect);
  painter->restore();
}
