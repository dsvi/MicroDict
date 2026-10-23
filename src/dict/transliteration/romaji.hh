/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "transliteration.hh"

namespace RomajiTranslit {

std::vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Romaji & );
}
