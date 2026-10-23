/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <map>
#include <string>
#include <vector>
#include <QMutex>
#include <QString>
#include <QWaitCondition>
#include <QGuiApplication>

#include "config.hh"
#include "ex.hh"
#include "langcoder.hh"
#include "sptr.hh"
#include "utils.hh"
#include <QtGlobal>

namespace Dictionary {

using std::vector;
using std::string;
using std::map;

DEF_EX( Ex, "Dictionary error", std::exception )
DEF_EX( exIndexOutOfRange, "The supplied index is out of range", Ex )
DEF_EX( exRequestUnfinished, "The request hasn't yet finished", Ex )

DEF_EX_STR( exCantReadFile, "Can't read file", Dictionary::Ex )

class Request: public QObject
{
  Q_OBJECT

public:
  bool isFinished();

  QString getErrorString();

  virtual void cancel() = 0;

  virtual ~Request() {}

signals:

  void updated();

  void finished();

  void matchCount( int );

protected:
  void update();

  void finish();

  void setErrorString( const QString & );
  QWaitCondition cond;
  QMutex dataMutex;

private:

  QAtomicInt isFinishedFlag;

  QMutex errorStringMutex;
  QString errorString;
};

struct WordMatch
{
  QString word;
  int weight;

  WordMatch():
    weight( 0 )
  {
  }
  WordMatch( const QString & word_ ):
    word( word_ ),
    weight( 0 )
  {
  }
  WordMatch( const QString & word_, int weight_ ):
    word( word_ ),
    weight( weight_ )
  {
  }

  bool operator==( const WordMatch & other ) const
  {
    return word == other.word;
  }
};

class WordSearchRequest: public Request
{
  Q_OBJECT

public:

  WordSearchRequest():
    uncertain( false )
  {
  }

  size_t matchesCount();

  WordMatch operator[]( size_t index );

  vector< WordMatch > & getAllMatches();

  bool isUncertain() const
  {
    return uncertain;
  }

  void addMatch( const WordMatch & match );

protected:

  vector< WordMatch > matches;
  bool uncertain;
};

class DataRequest: public Request
{
  Q_OBJECT

public:
  long dataSize();

  void appendDataSlice( const void * buffer, size_t size );
  void appendString( std::string_view str );

  vector< char > & getFullData();

protected:
  bool hasAnyData = false;
  vector< char > data;
};

class WordSearchRequestInstant: public WordSearchRequest
{
  Q_OBJECT

public:

  WordSearchRequestInstant()
  {
    finish();
  }

  void cancel() override {}

  vector< WordMatch > & getMatches()
  {
    return matches;
  }

  void setUncertain( bool value )
  {
    uncertain = value;
  }
};

class DataRequestInstant: public DataRequest
{
public:

  DataRequestInstant( bool succeeded )
  {
    hasAnyData = succeeded;
    finish();
  }

  DataRequestInstant( const QString & errorString )
  {
    setErrorString( errorString );
    finish();
  }

  virtual void cancel() {}

  vector< char > & getData()
  {
    return data;
  }
};

enum Feature {
  NoFeatures = 0,
  SuitableForCompoundSearching = 1,
};

Q_DECLARE_FLAGS( Features, Feature )
Q_DECLARE_OPERATORS_FOR_FLAGS( Features )

class Class: public QObject
{
  Q_OBJECT

  string id;
  vector< string > dictionaryFiles;
  long indexedFtsDoc;

  long lastProgress = 0;

protected:
  QString dictionaryDescription;
  QIcon dictionaryIcon;
  bool dictionaryIconLoaded;
  bool can_FTS;
  QAtomicInt FTS_index_completed;
  bool synonymSearchEnabled;
  string dictionaryName;
  string originalDictionaryName;
  std::optional< bool > metadata_enable_fts = std::nullopt;
  virtual void loadIcon() noexcept;

  static int getOptimalIconSize();

  [[nodiscard]] bool loadIconFromFileName( const QString & mainDictFileName );
  bool loadIconFromFilePath( const QString & filename );
  bool loadIconFromText( const QString & iconUrl, const QString & text );

  static QString getAbbrName( const QString & text, const QString & key = {} );
  static QColor intToFixedColor( int index );

public:
  void isolateCSS( QString & css, const QString & wrapperSelector = QString() );

  Class( const string & id, const vector< string > & dictionaryFiles );

  virtual void deferredInit();

  string getId() noexcept
  {
    return id;
  }

  const vector< string > & getDictionaryFilenames() noexcept
  {
    return dictionaryFiles;
  }

  QString getContainingFolder() const;

  virtual string getName()
  {
    return dictionaryName;
  }

  virtual void setName( string _dictionaryName )
  {
    if ( _dictionaryName.empty() ) {
      if ( !originalDictionaryName.empty() ) {
        dictionaryName = originalDictionaryName;
      }
      return;
    }

    if ( originalDictionaryName.empty() ) {
      originalDictionaryName = dictionaryName;
    }
    dictionaryName = _dictionaryName;
  }

  virtual Features getFeatures() const noexcept
  {
    return NoFeatures;
  }

  virtual unsigned long getArticleCount() noexcept = 0;

  void setIndexedFtsDoc( long _indexedFtsDoc );

  int getIndexingFtsProgress()
  {
    if ( haveFTSIndex() ) {
      return 100;
    }
    auto total = getArticleCount();
    if ( total == 0 )
      return 0;
    int progress = (int)indexedFtsDoc * 100 / total;
    return qMin( progress, 100 );
  }

  virtual unsigned long getWordCount() noexcept = 0;

  virtual const QIcon & getIcon() noexcept;

  virtual quint32 getLangFrom() const
  {
    return 0;
  }

  virtual quint32 getLangTo() const
  {
    return 0;
  }

  virtual sptr< WordSearchRequest > prefixMatch( const QString &, unsigned long maxResults ) = 0;

  virtual sptr< WordSearchRequest >
  stemmedMatch( const QString &, unsigned minLength, unsigned maxSuffixVariation, unsigned long maxResults );

  virtual sptr< WordSearchRequest > findHeadwordsForSynonym( const QString & );

  virtual vector< QString > getAlternateWritings( const QString & ) noexcept;

  virtual sptr< DataRequest > getArticle( const QString &,
                                          const vector< QString > & alts,
                                          const QString & context = QString(),
                                          bool ignoreDiacritics   = false ) = 0;

  virtual sptr< DataRequest > getResource( const string & );

  virtual sptr< DataRequest >
  getSearchResults( const QString & searchString, int searchMode, bool matchCase, bool ignoreDiacritics );

  virtual const QString & getDescription();

  virtual QString getMainFilename();

  bool isFromLanguageRTL()
  {
    return LangCoder::isLanguageRTL( getLangFrom() );
  }
  bool isToLanguageRTL()
  {
    return LangCoder::isLanguageRTL( getLangTo() );
  }

  virtual bool isLocalDictionary()
  {
    return false;
  }

  bool canFTS()
  {
    return can_FTS;
  }

  void setFtsEnabled( bool enable )
  {
    metadata_enable_fts = enable;
    can_FTS             = enable;
  }

  bool haveFTSIndex()
  {
    return Utils::AtomicInt::loadAcquire( FTS_index_completed ) != 0;
  }

  void resetFTSIndexState()
  {
    FTS_index_completed.storeRelease( 0 );
  }

  virtual void makeFTSIndex( QAtomicInt & ) {}

  virtual sptr< Class > duplicate()
  {
    return nullptr;
  }

  virtual void setFTSParameters( const Config::FullTextSearch & ) {}

  virtual bool getHeadwords( QStringList & )
  {
    return false;
  }
  virtual void findHeadWordsWithLenth( int &, QSet< QString > *, uint32_t ) {}

  void setSynonymSearchEnabled( bool enabled )
  {
    synonymSearchEnabled = enabled;
  }

  virtual ~Class() = default;
};

class Initializing
{
public:

  virtual void indexingDictionary( const string & dictionaryName ) noexcept = 0;
  virtual void loadingDictionary( const string & dictionaryName ) noexcept  = 0;

  virtual ~Initializing() = default;
};

string makeDictionaryId( const vector< string > & dictionaryFiles ) noexcept;

bool needToRebuildIndex( const vector< string > & dictionaryFiles, const string & indexFile ) noexcept;

string makeLegacyDictionaryId( const vector< string > & dictionaryFiles ) noexcept;

string getFtsSuffix();

vector< string > getObsoleteFtsSuffixes();
QString generateRandomDictionaryId();

QMap< std::string, sptr< Dictionary::Class > > dictToMap( const std::vector< sptr< Dictionary::Class > > & dicts );

}
