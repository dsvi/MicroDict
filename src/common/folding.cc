/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "folding.hh"

#include "text.hh"
#include "globalregex.hh"
#include "inc_case_folding.hh"

#include <string>

namespace Folding {

namespace {

QString foldFull( const QString & in )
{
  const std::u32string codePoints = in.toStdU32String();

  std::u32string caseFolded;
  caseFolded.reserve( codePoints.size() * foldCaseMaxOut );

  char32_t buf[ foldCaseMaxOut ];
  for ( const char32_t ch : codePoints ) {
    caseFolded.append( buf, foldCase( ch, buf ) );
  }

  return QString::fromStdU32String( caseFolded );
}

}

bool isCombiningMark( char32_t ch )
{
  return QChar::isMark( ch );
}

QString apply( const QString & in, bool preserveWildcards )
{
  auto temp = in.normalized( QString::NormalizationForm_KD )
                .remove( RX::markSpace )
                .removeIf( [ preserveWildcards ]( const QChar & ch ) -> bool {
                  return ch.isPunct()
                    && !( preserveWildcards && ( ch == '\\' || ch == '?' || ch == '*' || ch == '[' || ch == ']' ) );
                } );
  return foldFull( temp );
}

std::string applyForIndex( const QString & in )
{
  auto temp = in.normalized( QString::NormalizationForm_KD ).remove( RX::accentPunc );
  return Text::toUtf8( foldFull( temp ) );
}

QString applySimpleCaseOnly( const QString & in )
{
  return in.toCaseFolded();
}

QString applySimpleCaseOnly( const std::string & in )
{
  return applySimpleCaseOnly( Text::toQString( in ) );
}

QString applyFullCaseOnly( const QString & in )
{
  return foldFull( in );
}

QString applyDiacriticsOnly( const QString & in )
{
  return in.normalized( QString::NormalizationForm_KD ).remove( RX::accentMark );
}

QString applyPunctOnly( const QString & in )
{
  QString out;
  out.reserve( in.size() );

  for ( const QChar & ch : in ) {
    if ( !isPunct( ch.unicode() ) ) {
      out.push_back( ch );
    }
  }

  return out;
}

QString applyWhitespaceOnly( const QString & in )
{
  QString out;
  out.reserve( in.size() );

  for ( const QChar & ch : in ) {
    if ( !isWhitespace( ch.unicode() ) ) {
      out.push_back( ch );
    }
  }

  return out;
}

QString applyWhitespaceAndPunctOnly( const QString & in )
{
  QString out;
  out.reserve( in.size() );

  for ( const QChar & ch : in ) {
    if ( !isWhitespaceOrPunct( ch.unicode() ) ) {
      out.push_back( ch );
    }
  }

  return out;
}

bool isWhitespace( char32_t ch )
{
  return QChar::isSpace( ch );
}

bool isWhitespaceOrPunct( char32_t ch )
{
  return isWhitespace( ch ) || QChar::isPunct( ch );
}

bool isPunct( char32_t ch )
{
  return QChar::isPunct( ch );
}

QString trimWhitespaceOrPunct( const QString & in )
{
  int wordBegin = 0;
  int wordEnd   = in.size();

  while ( wordBegin < wordEnd && Folding::isWhitespaceOrPunct( in[ wordBegin ].unicode() ) ) {
    ++wordBegin;
  }

  while ( wordEnd > wordBegin && Folding::isWhitespaceOrPunct( in[ wordEnd - 1 ].unicode() ) ) {
    --wordEnd;
  }

  return wordBegin == 0 && wordEnd == in.size() ? in : in.mid( wordBegin, wordEnd - wordBegin );
}

QString trimWhitespace( const QString & in )
{
  return in.trimmed();
}

QString escapeWildcardSymbols( const QString & str )
{
  QString escaped( str );
  escaped.replace( QRegularExpression( R"(([\[\]\?\*]))" ), R"(\\1)" );

  return escaped;
}

QString unescapeWildcardSymbols( const QString & str )
{
  QString unescaped( str );
  unescaped.replace( QRegularExpression( R"(\\([\[\]\?\*]))" ), R"(\1)" );

  return unescaped;
}
}
