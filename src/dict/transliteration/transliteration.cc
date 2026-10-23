/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "transliteration.hh"
#include "text.hh"
#include "folding.hh"

namespace Transliteration {


BaseTransliterationDictionary::BaseTransliterationDictionary( const string & id,
                                                              const string & name_,
                                                              QIcon icon_,
                                                              bool caseSensitive_ ):
  Dictionary::Class( id, vector< string >() ),
  name( name_ ),
  caseSensitive( caseSensitive_ )
{
  dictionaryIcon       = icon_;
  dictionaryIconLoaded = true;
}

string BaseTransliterationDictionary::getName() noexcept
{
  return name;
}

unsigned long BaseTransliterationDictionary::getArticleCount() noexcept
{
  return 0;
}

unsigned long BaseTransliterationDictionary::getWordCount() noexcept
{
  return 0;
}

sptr< Dictionary::WordSearchRequest > BaseTransliterationDictionary::prefixMatch( const QString &, unsigned long )
{
  return std::make_shared< Dictionary::WordSearchRequestInstant >();
}

sptr< Dictionary::DataRequest >
BaseTransliterationDictionary::getArticle( const QString &, const vector< QString > &, const QString &, bool )

{
  return std::make_shared< Dictionary::DataRequestInstant >( false );
}

sptr< Dictionary::WordSearchRequest > BaseTransliterationDictionary::findHeadwordsForSynonym( const QString & str )

{
  sptr< Dictionary::WordSearchRequestInstant > result = std::make_shared< Dictionary::WordSearchRequestInstant >();

  vector< QString > alts = getAlternateWritings( str );

  for ( const auto & alt : alts ) {
    result->getMatches().push_back( alt );
  }

  return result;
}


void Table::ins( const char * from, const char * to )
{
  QString fr = Text::toQString( std::string( from ) );

  insert( std::pair< QString, QString >( fr, Text::toQString( std::string( to ) ) ) );
}

void Table::ins( const std::string & from, const std::string & to )
{
  insert( std::pair< QString, QString >( Text::toQString( from ), Text::toQString( to ) ) );
}


TransliterationDictionary::TransliterationDictionary(
  const string & id, const string & name_, QIcon icon_, const Table & table_, bool caseSensitive_ ):
  BaseTransliterationDictionary( id, name_, icon_, caseSensitive_ ),
  table( table_ )
{
}

vector< QString > TransliterationDictionary::getAlternateWritings( const QString & str ) noexcept
{
  vector< QString > results;

  QString result, folded;
  const QString * target;

  if ( caseSensitive ) {
    target = &str;
  }
  else {
    folded = Folding::applySimpleCaseOnly( str );
    target = &folded;
  }

  const QString & tgt = *target;
  qsizetype pos       = 0;

  Table::const_iterator i;

  while ( pos < tgt.size() ) {
    qsizetype x;

    for ( x = (qsizetype)table.size(); x >= 1; --x ) {
      if ( tgt.size() - pos >= x ) {
        i = table.find( tgt.mid( pos, x ) );

        if ( i != table.end() ) {
          result.append( i->second );
          pos += x;
          break;
        }
      }
    }

    if ( !x ) {
      result.push_back( tgt[ pos ] );
      ++pos;
    }
  }

  if ( result != *target ) {
    results.push_back( result );
  }

  return results;
}

}
