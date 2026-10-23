/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"
#include "config.hh"

namespace HunspellMorpho {

using std::vector;
using std::string;

struct DataFiles
{
  QString affFileName, dicFileName;
  QString dictId;
  QString dictName;

  DataFiles( const QString & affFileName_,
             const QString & dicFileName_,
             const QString & dictId_,
             const QString & dictName_ ):
    affFileName( affFileName_ ),
    dicFileName( dicFileName_ ),
    dictId( dictId_ ),
    dictName( dictName_ )
  {
  }
};

vector< DataFiles > findDataFiles( const QString & path );

vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Hunspell & );

}
