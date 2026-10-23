/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QObject>
#include <QList>
#include <QString>

constexpr unsigned DEFAULT_MAX_HISTORY_ITEM_LENGTH = 256;

class History: public QObject
{
  Q_OBJECT

public:

  struct Item
  {
    QString word;

    bool operator==( const Item & other ) const
    {
      return QString::compare( word, other.word, Qt::CaseInsensitive ) == 0;
    }
  };

  explicit History( unsigned size = 256, unsigned maxItemLength = DEFAULT_MAX_HISTORY_ITEM_LENGTH );

  void addItem( const Item & );

  Item getItem( int index );

  void removeItem( int index )
  {
    if ( index >= 0 && index < items.size() ) {
      saveTemp( items.at( index ), '-' );
    }

    items.removeAt( index );
    dirty = true;
    emit itemsChanged();
  }

  bool save();

  void clear();

  int size() const;

  const QList< Item > & getItems() const
  {
    return items;
  }

  void enableAdd( bool enable )
  {
    addingEnabled = enable;
  }
  bool enabled()
  {
    return addingEnabled;
  }

  void setMaxSize( unsigned maxSize_ );

  unsigned getMaxSize()
  {
    return maxSize;
  }

  unsigned getMaxItemLength() const
  {
    return maxItemLength;
  }

signals:

  void itemsChanged();

private:

  bool ensureSizeConstraints();

  void saveTemp( const Item & item, QChar operation );

  void loadTemp();

  void removeTemp();

  QList< Item > items;
  unsigned maxSize;
  unsigned maxItemLength;
  bool addingEnabled;
  bool dirty;

protected:
};
