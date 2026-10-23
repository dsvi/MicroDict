/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <string>

namespace Filetype {

using std::string;

string simplifyString( const string & str, bool lowercase = true );
bool isNameOfSound( const string & );
bool isNameOfVideo( const string & );
bool isNameOfPicture( const string & );
bool isNameOfTiff( const string & );
bool isNameOfCSS( const string & );
bool isNameOfSvg( const string & name );

}
