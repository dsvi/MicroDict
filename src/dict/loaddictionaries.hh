/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "config.hh"
#include "dict/dictionary.hh"

#include <QThread>
#include <QStringList>

class LoadDictionaries: public QObject, public Dictionary::Initializing
{
  Q_OBJECT

  QStringList nameFilters;
  const Config::Paths & paths;
  const Config::SoundDirs & soundDirs;
  const Config::Hunspell & hunspell;
  const Config::Transliteration & transliteration;
  std::vector< sptr< Dictionary::Class > > dictionaries;
  QStringList exceptionTexts;
  unsigned int maxHeadwordSize;
  unsigned int maxHeadwordToExpand;

public:

  LoadDictionaries( const Config::Class & cfg );

  void load();

  const std::vector< sptr< Dictionary::Class > > & getDictionaries() const
  {
    return dictionaries;
  }

  std::string getExceptionText() const
  {
    return exceptionTexts.join( "\n" ).toStdString();
  }


public:

  virtual void indexingDictionary( const std::string & dictionaryName ) noexcept;
  virtual void loadingDictionary( const std::string & dictionaryName ) noexcept;

private:

  void handlePath( const Config::Path & );

  void addDicts( const std::vector< sptr< Dictionary::Class > > & dicts );

signals:
  void indexingDictionarySignal( const QString & dictionaryName );
  void loadingDictionarySignal( const QString & dictionaryName );
  void finished();
};

void loadDictionaries( QWidget * parent,
                       const Config::Class & cfg,
                       std::vector< sptr< Dictionary::Class > > &,
                       bool doDeferredInit = true );

void doDeferredInit( std::vector< sptr< Dictionary::Class > > & );
