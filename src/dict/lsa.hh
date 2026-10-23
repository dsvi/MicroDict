/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"

namespace Lsa {

using std::vector;
using std::string;

vector< sptr< Dictionary::Class > >
makeDictionaries( const vector< string > & fileNames, const string & indicesDir, Dictionary::Initializing & );

}
