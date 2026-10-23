/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"
#include "config.hh"

namespace SoundDir {

using std::vector;
using std::string;

vector< sptr< Dictionary::Class > >
makeDictionaries( const Config::SoundDirs &, const string & indicesDir, Dictionary::Initializing & );

}
