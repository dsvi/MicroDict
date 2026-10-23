/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <string>
#include <map>
#include "dictionary.hh"

namespace Xdxf2Html {

enum DICT_TYPE {
  STARDICT,
  XDXF
};

using std::string;
using std::map;

string convert( const string &,
                DICT_TYPE type,
                const map< string, string > * pAbrv,
                Dictionary::Class * dictPtr,
                bool isLogicalFormat    = false,
                unsigned revisionNumber = 0,
                QString * headword      = 0 );

}
