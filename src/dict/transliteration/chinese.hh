/* This file is (c) 2015 Zhe Wang <0x1997@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"

namespace ChineseTranslit {

std::vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Chinese & );
}
