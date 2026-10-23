/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "text.hh"
#include <QString>

namespace Folding {

enum {
  Version = 5
};

QString apply( const QString &, bool preserveWildcards = false );
std::string applyForIndex( const QString & in );
QString applySimpleCaseOnly( const QString & in );
QString applySimpleCaseOnly( const std::string & in );

QString applyFullCaseOnly( const QString & );

QString applyDiacriticsOnly( const QString & );

QString applyPunctOnly( const QString & );
QString applyWhitespaceOnly( const QString & );

QString applyWhitespaceAndPunctOnly( const QString & );

bool isWhitespace( char32_t ch );
bool isWhitespaceOrPunct( char32_t ch );

bool isPunct( char32_t ch );

QString trimWhitespaceOrPunct( const QString & in );

QString trimWhitespace( const QString & in );

QString unescapeWildcardSymbols( const QString & );

QString escapeWildcardSymbols( const QString & );

bool isCombiningMark( char32_t ch );

}
