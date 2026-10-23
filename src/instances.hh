/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "config.hh"
#include "dict/dictionary.hh"
#include <QIcon>

namespace Instances {

using std::vector;

struct Group
{
  unsigned id;
  QString name, icon, favoritesFolder;
  QIcon iconData;
  QKeySequence shortcut;
  vector< sptr< Dictionary::Class > > dictionaries;

  Group( const Config::Group & cfgGroup,
         const std::vector< sptr< Dictionary::Class > > & allDictionaries,
         const Config::Group & inactiveGroup );

  explicit Group();

  Group( unsigned id, QString name_ );

  Config::Group makeConfigGroup();

  QIcon makeIcon() const;

  void checkMutedDictionaries( QSet< QString > * mutedDictionaries ) const;
};

struct Groups: vector< Group >
{
  const Group * findGroup( unsigned id ) const;
};

void complementDictionaryOrder( Group & dictionaryOrder,
                                const Group & inactiveDictionaries,
                                const vector< sptr< Dictionary::Class > > & allDictionaries );

void updateNames( Config::Group &, const vector< sptr< Dictionary::Class > > & allDictionaries );

void updateNames( Config::Groups &, const vector< sptr< Dictionary::Class > > & allDictionaries );

void updateNames( Config::Class &, const vector< sptr< Dictionary::Class > > & allDictionaries );

QIcon iconFromData( const QByteArray & );

}
