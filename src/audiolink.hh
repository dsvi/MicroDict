/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once


#include <QString>
#include <string>

std::string addAudioLink( const std::string & url, const std::string & dictionaryId );
std::string addAudioLink( const QString & url, const std::string & dictionaryId );
