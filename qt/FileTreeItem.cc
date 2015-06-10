/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <algorithm>
#include <cassert>

#include <QApplication>
#include <QStyle>

#include <libtransmission/transmission.h> // priorities

#include "FileTreeItem.h"
#include "FileTreeModel.h"
#include "Formatter.h"
#include "Utils.h" // mime icons

const QHash<QString,int>&
FileTreeItem::getMyChildRows ()
{
  const size_t n = childCount();

  // ensure that all the rows are hashed
  while (myFirstUnhashedRow < n)
    {
      myChildRows.insert (myChildren[myFirstUnhashedRow]->name(),
                          myFirstUnhashedRow);
      ++myFirstUnhashedRow;
    }

  return myChildRows;
}


FileTreeItem::~FileTreeItem ()
{
  assert(myChildren.isEmpty());

  if (myParent != 0)
    {
      const int pos = row();
      assert ((pos>=0) && "couldn't find child in parent's lookup");
      myParent->myChildren.removeAt(pos);
      myParent->myChildRows.remove(name());
      myParent->myFirstUnhashedRow = pos;
    }
}

void
FileTreeItem::appendChild (FileTreeItem * child)
{
  const size_t n = childCount();
  child->myParent = this;
  myChildren.append (child);
  myFirstUnhashedRow = n;
}

FileTreeItem *
FileTreeItem::child (const QString& filename)
{
  FileTreeItem * item(0);

  const int row = getMyChildRows().value (filename, -1);
  if (row != -1)
    {
      item = child (row);
      assert (filename == item->name());
    }

  return item;
}

int
FileTreeItem::row () const
{
  int i(-1);

  if(myParent)
    {
      i = myParent->getMyChildRows().value (name(), -1);
      assert (this == myParent->myChildren[i]);
    }

  return i;
}

QVariant
FileTreeItem::data (int column, int role) const
{
  QVariant value;

  if (column == FileTreeModel::COL_FILE_INDEX)
    {
      value.setValue (myFileIndex);
    }
  else if (role == Qt::EditRole)
    {
      if (column == 0)
        value.setValue (name());
    }
  else if ((role == Qt::TextAlignmentRole) && column == FileTreeModel::COL_SIZE)
    {
      value = Qt::AlignRight + Qt::AlignVCenter;
    }
  else if (role == Qt::DisplayRole)
    {
      switch(column)
       {
         case FileTreeModel::COL_NAME:
           value.setValue (name());
           break;

         case FileTreeModel::COL_SIZE:
           value.setValue (sizeString() + QLatin1String ("  "));
           break;

         case FileTreeModel::COL_PROGRESS:
           value.setValue (progress());
           break;

         case FileTreeModel::COL_WANTED:
           value.setValue (isSubtreeWanted());
           break;

         case FileTreeModel::COL_PRIORITY:
           value.setValue (priorityString());
           break;
        }
    }
  else if (role == Qt::DecorationRole && column == FileTreeModel::COL_NAME)
    {
      if (childCount () > 0)
        value = qApp->style ()->standardIcon (QStyle::SP_DirOpenIcon);
      else
        value = Utils::guessMimeIcon (name ());
    }

  return value;
}

void
FileTreeItem::getSubtreeWantedSize (uint64_t& have, uint64_t& total) const
{
  if (myIsWanted)
    {
      have += myHaveSize;
      total += myTotalSize;
    }

  for (const FileTreeItem * const i: myChildren)
    i->getSubtreeWantedSize(have, total);
}

double
FileTreeItem::progress () const
{
  double d(0);
  uint64_t have(0), total(0);

  getSubtreeWantedSize (have, total);
  if (total)
    d = have / (double)total;

  return d;
}

QString
FileTreeItem::sizeString () const
{
  QString str;

  if (myChildren.isEmpty())
    {
      str = Formatter::sizeToString (myTotalSize);
    }
  else
    {
      uint64_t have = 0;
      uint64_t total = 0;
      getSubtreeWantedSize (have, total);
      str = Formatter::sizeToString (total);
    }

  return str;
}

std::pair<int,int>
FileTreeItem::update (const QString& name,
                      bool           wanted,
                      int            priority,
                      uint64_t       haveSize,
                      bool           updateFields)
{
  int changed_count = 0;
  int changed_columns[4];

  if (myName != name)
    {
      if (myParent)
        myParent->myFirstUnhashedRow = row();

      myName = name;
      changed_columns[changed_count++] = FileTreeModel::COL_NAME;
    }

  if (fileIndex () != -1)
    {
      if (myHaveSize != haveSize)
        {
          myHaveSize = haveSize;
          changed_columns[changed_count++] = FileTreeModel::COL_PROGRESS;
        }

      if (updateFields)
        {
          if (myIsWanted != wanted)
            {
              myIsWanted = wanted;
              changed_columns[changed_count++] = FileTreeModel::COL_WANTED;
            }

          if (myPriority != priority)
            {
              myPriority = priority;
              changed_columns[changed_count++] = FileTreeModel::COL_PRIORITY;
            }
        }
    }

  std::pair<int,int> changed (-1, -1);
  if (changed_count > 0)
    {
      std::sort (changed_columns, changed_columns+changed_count);
      changed.first = changed_columns[0];
      changed.second = changed_columns[changed_count-1];
    }
  return changed;
}

QString
FileTreeItem::priorityString () const
{
  const int i = priority();

  switch (i)
    {
      case LOW:    return tr("Low");
      case HIGH:   return tr("High");
      case NORMAL: return tr("Normal");
      default:     return tr("Mixed");
    }
}

int
FileTreeItem::priority () const
{
  int i(0);

  if (myChildren.isEmpty())
    {
      switch (myPriority)
        {
          case TR_PRI_LOW:
            i |= LOW;
            break;

          case TR_PRI_HIGH:
            i |= HIGH;
            break;

          default:
            i |= NORMAL;
            break;
        }
    }

  for (const FileTreeItem * const child: myChildren)
    i |= child->priority();

  return i;
}

void
FileTreeItem::setSubtreePriority (int i, QSet<int>& ids)
{
  if (myPriority != i)
    {
      myPriority = i;

      if (myFileIndex >= 0)
        ids.insert (myFileIndex);
    }

  for (FileTreeItem * const child: myChildren)
    child->setSubtreePriority (i, ids);
}

void
FileTreeItem::twiddlePriority (QSet<int>& ids, int& p)
{
  const int old(priority());

  if (old & LOW)
    p = TR_PRI_NORMAL;
  else if (old & NORMAL)
    p = TR_PRI_HIGH;
  else
    p = TR_PRI_LOW;

  setSubtreePriority (p, ids);
}

int
FileTreeItem::isSubtreeWanted () const
{
  if(myChildren.isEmpty())
    return myIsWanted ? Qt::Checked : Qt::Unchecked;

  int wanted(-1);
  for (const FileTreeItem * const child: myChildren)
    {
      const int childWanted = child->isSubtreeWanted();

      if (wanted == -1)
        wanted = childWanted;

      if (wanted != childWanted)
        wanted = Qt::PartiallyChecked;

      if (wanted == Qt::PartiallyChecked)
        return wanted;
    }

  return wanted;
}

void
FileTreeItem::setSubtreeWanted (bool b, QSet<int>& ids)
{
  if (myIsWanted != b)
    {
      myIsWanted = b;

      if (myFileIndex >= 0)
        ids.insert(myFileIndex);
    }

  for (FileTreeItem * const child: myChildren)
    child->setSubtreeWanted (b, ids);
}

void
FileTreeItem::twiddleWanted (QSet<int>& ids, bool& wanted)
{
  wanted = isSubtreeWanted() != Qt::Checked;
  setSubtreeWanted (wanted, ids);
}

QString
FileTreeItem::path () const
{
  QString itemPath;
  const FileTreeItem * item = this;

  while (item != NULL && !item->name().isEmpty())
    {
      if (itemPath.isEmpty())
        itemPath = item->name();
      else
        itemPath = item->name() + QLatin1Char ('/') + itemPath;
      item = item->parent ();
    }

  return itemPath;
}

bool
FileTreeItem::isComplete () const
{
  return myHaveSize == totalSize ();
}
