/* This file is (c) 2008-2009 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"

namespace Xdxf {

using std::vector;
using std::string;

quint32 getLanguageId( const QString & lang );

vector< sptr< Dictionary::Class > >
makeDictionaries( const vector< string > & fileNames, const string & indicesDir, Dictionary::Initializing & );

}
