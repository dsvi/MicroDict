/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "history.hh"
#include "config.hh"
#include <QFile>
#include <QSaveFile>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

QString getTempHistoryFileName()
{
  return Config::getHomeDir().filePath( "history.tmp" );
}

History::History( unsigned size, unsigned maxItemLength_ ):
  maxSize( size ),
  maxItemLength( maxItemLength_ ),
  addingEnabled( true ),
  dirty( false )
{
  QFile file( Config::getHistoryFileName() );

  if ( file.open( QFile::ReadOnly | QIODevice::Text ) ) {
    QTextStream in( &file );
    while ( !in.atEnd() && items.size() <= maxSize ) {
      QString line = in.readLine( 4096 );

      auto firstSpace = line.indexOf( ' ' );

      if ( firstSpace < 0 || firstSpace == line.size() ) {
        break;
      }

      QString t = line.right( line.size() - firstSpace - 1 ).trimmed();

      if ( !t.isEmpty() ) {
        items.push_back( Item{ line.right( line.size() - firstSpace - 1 ).trimmed() } );
      }
    }
  }

  loadTemp();

  removeTemp();
}

History::Item History::getItem( int index )
{
  if ( index < 0 || index >= items.size() ) {
    return {};
  }
  return items.at( index );
}

void History::addItem( const Item & item )
{
  if ( !enabled() ) {
    return;
  }

  if ( item.word.isEmpty() || item.word.size() > maxItemLength ) {
    return;
  }

  if ( items.contains( item ) ) {
    items.removeOne( item );
  }

  items.push_front( item );

  Item & addedItem = items.first();

  addedItem.word.replace( QChar::LineFeed, QChar::Space );
  addedItem.word.replace( QChar::CarriageReturn, QChar::Space );

  ensureSizeConstraints();

  dirty = true;

  emit itemsChanged();

  saveTemp( addedItem, '+' );
}

bool History::ensureSizeConstraints()
{
  bool changed = false;
  while ( items.size() > (int)maxSize ) {
    items.pop_back();
    changed = true;
    dirty   = true;
  }

  return changed;
}

void History::setMaxSize( unsigned maxSize_ )
{
  maxSize = maxSize_;
  if ( ensureSizeConstraints() ) {
    emit itemsChanged();
  }
}

int History::size() const
{
  return items.size();
}

bool History::save()
{
  if ( !dirty ) {
    return true;
  }

  QSaveFile file( Config::getHistoryFileName() );
  if ( !file.open( QFile::WriteOnly | QIODevice::Text ) ) {
    return false;
  }

  QTextStream out( &file );
  for ( const auto & i : std::as_const( items ) ) {
    out << "0 " << i.word.trimmed() << '\n';
  }

  if ( file.commit() ) {
    dirty = false;
    removeTemp();
    return true;
  }

  qDebug() << "Failed to save history file";
  return false;
}

void History::clear()
{
  items.clear();
  dirty = true;

  emit itemsChanged();

  removeTemp();
}

void History::saveTemp( const Item & item, QChar operation )
{
  QFile file( getTempHistoryFileName() );
  if ( !file.open( QFile::Append | QFile::WriteOnly | QIODevice::Text ) ) {
    qDebug() << "Failed to open temporary history file for writing:" << file.errorString();
    return;
  }

  QTextStream out( &file );
  out << operation << " " << item.word.trimmed() << '\n';

  out.flush();
  file.flush();
}

void History::loadTemp()
{
  QFile file( getTempHistoryFileName() );

  if ( !file.open( QFile::ReadOnly | QIODevice::Text ) ) {
    return;
  }

  QTextStream in( &file );
  while ( !in.atEnd() && items.size() <= maxSize ) {
    QString line = in.readLine( 4096 );

    if ( line.isEmpty() ) {
      continue;
    }

    QChar operation = line[ 0 ];
    QString word;

    if ( line.size() > 2 ) {
      word = line.mid( 2 ).trimmed();
    }

    if ( word.isEmpty() ) {
      continue;
    }

    Item newItem{ word };

    if ( operation == '+' ) {
      if ( !items.contains( newItem ) ) {
        items.push_front( newItem );
      }
    }
    else if ( operation == '-' ) {
      items.removeOne( newItem );
    }
  }

  qDebug() << "Recovered items from temporary file";
}

void History::removeTemp()
{
  QFile::remove( getTempHistoryFileName() );
}
