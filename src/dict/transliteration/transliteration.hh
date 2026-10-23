/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dictionary.hh"
#include <map>

namespace Transliteration {

using std::map;
using std::string;
using std::vector;

class BaseTransliterationDictionary: public Dictionary::Class
{
  string name;

protected:
  bool caseSensitive;

public:

  BaseTransliterationDictionary( const string & id, const string & name, QIcon icon, bool caseSensitive = true );

  virtual string getName() noexcept;

  virtual unsigned long getArticleCount() noexcept;

  virtual unsigned long getWordCount() noexcept;

  virtual vector< QString > getAlternateWritings( const QString & ) noexcept = 0;

  virtual sptr< Dictionary::WordSearchRequest > findHeadwordsForSynonym( const QString & );

  virtual sptr< Dictionary::WordSearchRequest > prefixMatch( const QString &, unsigned long );

  virtual sptr< Dictionary::DataRequest >
  getArticle( const QString &, const vector< QString > &, const QString &, bool );
};


class Table: public map< QString, QString >
{

protected:

  void ins( const char * from, const char * to );

  void ins( const QString & from, const QString & to )
  {
    this->insert( { from, to } );
  }

  void ins( const std::string & from, const std::string & to );
};

class TransliterationDictionary: public BaseTransliterationDictionary
{
  const Table & table;

public:

  TransliterationDictionary(
    const string & id, const string & name, QIcon icon, const Table & table, bool caseSensitive = true );

  virtual vector< QString > getAlternateWritings( const QString & ) noexcept;
};

}
