/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "hunspell.hh"
#include "text.hh"
#include "htmlescape.hh"
#include "iconv.hh"
#include "folding.hh"
#include "language.hh"
#include "langcoder.hh"
#include <QRegularExpression>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <set>
#include "utils.hh"
#include <QtConcurrentRun>
#include <hunspell/hunspell.hxx>

namespace HunspellMorpho {

using namespace Dictionary;


namespace {

class HunspellDictionary: public Dictionary::Class
{
  Hunspell hunspell;

public:

  HunspellDictionary( const string & id, const string & name_, const vector< string > & files ):
    Dictionary::Class( id, files ),
    hunspell( files[ 0 ].c_str(), files[ 1 ].c_str() )
  {
    dictionaryName = name_;
  }

  unsigned long getArticleCount() noexcept override
  {
    return 0;
  }

  unsigned long getWordCount() noexcept override
  {
    return 0;
  }

  sptr< WordSearchRequest > prefixMatch( const QString &, unsigned long maxResults ) override;

  sptr< WordSearchRequest > findHeadwordsForSynonym( const QString & ) override;

  sptr< DataRequest > getArticle( const QString &, const vector< QString > & alts, const QString &, bool ) override;

  bool isLocalDictionary() override
  {
    return true;
  }

  vector< QString > getAlternateWritings( const QString & word ) noexcept override;

protected:

  void loadIcon() noexcept override;

private:

  static QMutex & getHunspellMutex()
  {
    static QMutex mutex;
    return mutex;
  }
};

QString decodeFromHunspell( Hunspell &, const char * );

QList< QString > suggest( const QString & word, QMutex & hunspellMutex, Hunspell & hunspell );

void getSuggestionsForExpression( const QString & expression,
                                  vector< QString > & suggestions,
                                  QMutex & hunspellMutex,
                                  Hunspell & hunspell );

bool containsWhitespace( const QString & str )
{
  for ( const QChar ch : str ) {
    if ( Folding::isWhitespace( ch.unicode() ) ) {
      return true;
    }
  }

  return false;
}

void HunspellDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  if ( !loadIconFromFileName( fileName ) ) {
    dictionaryIcon = QIcon( ":/icons/icon32_hunspell.png" );
  }

  dictionaryIconLoaded = true;
}

vector< QString > HunspellDictionary::getAlternateWritings( const QString & word ) noexcept
{
  vector< QString > results;

  if ( containsWhitespace( word ) ) {
    getSuggestionsForExpression( word, results, getHunspellMutex(), hunspell );
  }

  return results;
}

class HunspellArticleRequest: public Dictionary::DataRequest
{

  QMutex & hunspellMutex;
  Hunspell & hunspell;
  QString word;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  HunspellArticleRequest( const QString & word_, QMutex & hunspellMutex_, Hunspell & hunspell_ ):
    hunspellMutex( hunspellMutex_ ),
    hunspell( hunspell_ ),
    word( word_ )
  {
    f = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }

  void run();

  void cancel() override
  {
    isCancelled.ref();
  }

  ~HunspellArticleRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void HunspellArticleRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  vector< string > suggestions;

  try {
    QString trimmedWord = Folding::trimWhitespaceOrPunct( word );

    if ( containsWhitespace( trimmedWord ) ) {
      finish();
      return;
    }

    QMutexLocker _( &hunspellMutex );

    string trimmedWord_utf8 = Text::toUtf8( trimmedWord );

    if ( hunspell.spell( trimmedWord_utf8 ) ) {
      finish();
      return;
    }

    suggestions = hunspell.suggest( trimmedWord_utf8 );
    if ( !suggestions.empty() ) {

      string result = "<div class=\"gdspellsuggestion\">"
        + Html::escape( QCoreApplication::translate( "Hunspell", "Spelling suggestions: " ).toUtf8().data() );

      QString lowercasedWord = Folding::applySimpleCaseOnly( word );

      for ( vector< string >::size_type x = 0; x < suggestions.size(); ++x ) {
        QString suggestion = decodeFromHunspell( hunspell, suggestions[ x ].c_str() );

        if ( Folding::applySimpleCaseOnly( suggestion ) == lowercasedWord ) {

          finish();

          return;
        }
        string suggestionUtf8 = Text::toUtf8( suggestion );

        result += "<a href=\"bword:";
        result += Html::escape( suggestionUtf8 ) + "\">";
        result += Html::escape( suggestionUtf8 ) + "</a>";

        if ( x != suggestions.size() - 1 ) {
          result += ", ";
        }
      }

      result += "</div>";

      appendString( result );

      hasAnyData = true;
    }
  }
  catch ( Iconv::Ex & e ) {
    qWarning( "Hunspell: charset conversion error, no processing's done: %s", e.what() );
  }
  catch ( std::exception & e ) {
    qWarning( "Hunspell: error: %s", e.what() );
  }

  finish();
}

sptr< DataRequest >
HunspellDictionary::getArticle( const QString & word, const vector< QString > &, const QString &, bool )

{
  return std::make_shared< HunspellArticleRequest >( word, getHunspellMutex(), hunspell );
}

class HunspellHeadwordsRequest: public Dictionary::WordSearchRequest
{

  QMutex & hunspellMutex;
  Hunspell & hunspell;
  QString word;

  QAtomicInt isCancelled;
  QFuture< void > f;


public:

  HunspellHeadwordsRequest( const QString & word_, QMutex & hunspellMutex_, Hunspell & hunspell_ ):
    hunspellMutex( hunspellMutex_ ),
    hunspell( hunspell_ ),
    word( word_ )
  {
    f = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }

  void run();

  void cancel() override
  {
    isCancelled.ref();
  }

  ~HunspellHeadwordsRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};


void HunspellHeadwordsRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  QString trimmedWord = Folding::trimWhitespaceOrPunct( word );

  if ( trimmedWord.size() > 80 ) {
    finish();
    return;
  }

  if ( containsWhitespace( trimmedWord ) ) {
    vector< QString > results;

    getSuggestionsForExpression( trimmedWord, results, hunspellMutex, hunspell );

    QMutexLocker _( &dataMutex );
    for ( const auto & result : results ) {
      matches.push_back( result );
    }
  }
  else {
    QList< QString > suggestions = suggest( trimmedWord, hunspellMutex, hunspell );

    if ( !suggestions.empty() ) {
      QMutexLocker _( &dataMutex );

      for ( const auto & suggestion : std::as_const( suggestions ) ) {
        matches.push_back( suggestion );
      }
    }
  }

  finish();
}

QList< QString > suggest( const QString & word, QMutex & hunspellMutex, Hunspell & hunspell )
{
  QList< QString > result;

  try {
    QMutexLocker _( &hunspellMutex );

    auto suggestions = hunspell.analyze( Text::toUtf8( word ) );
    if ( !suggestions.empty() ) {

      QString lowercasedWord = Folding::applySimpleCaseOnly( word );

      static QRegularExpression cutStem( R"(^\s*st:(((\s+(?!\w{2}:)(?!-)(?!\+))|\S+)+))" );

      for ( const auto & x : suggestions ) {
        QString suggestion = decodeFromHunspell( hunspell, x.c_str() );

        int n = suggestion.indexOf( '#' );
        if ( n >= 0 ) {
          suggestion.chop( suggestion.length() - n );
        }

        qDebug( ">>>Sugg: %s", suggestion.toLocal8Bit().data() );

        auto match = cutStem.match( suggestion.trimmed() );
        if ( match.hasMatch() ) {
          QString alt = match.captured( 1 );

          if ( Folding::applySimpleCaseOnly( alt ) != lowercasedWord ) {
            result.append( alt );
          }
        }
      }
    }
  }
  catch ( Iconv::Ex & e ) {
    qWarning( "Hunspell: charset conversion error, no processing's done: %s", e.what() );
  }

  return result;
}


sptr< WordSearchRequest > HunspellDictionary::findHeadwordsForSynonym( const QString & word )

{
  return std::make_shared< HunspellHeadwordsRequest >( word, getHunspellMutex(), hunspell );
}

class HunspellPrefixMatchRequest: public Dictionary::WordSearchRequest
{

  QMutex & hunspellMutex;
  Hunspell & hunspell;
  QString word;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  HunspellPrefixMatchRequest( const QString & word_, QMutex & hunspellMutex_, Hunspell & hunspell_ ):
    hunspellMutex( hunspellMutex_ ),
    hunspell( hunspell_ ),
    word( word_ )
  {
    f = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }

  void run();

  void cancel() override
  {
    isCancelled.ref();
  }

  ~HunspellPrefixMatchRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};


void HunspellPrefixMatchRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  try {
    QString trimmedWord = Folding::trimWhitespaceOrPunct( word );

    if ( trimmedWord.isEmpty() || containsWhitespace( trimmedWord ) ) {
      finish();
      return;
    }

    QMutexLocker _( &hunspellMutex );

    if ( hunspell.spell( Text::toUtf8( trimmedWord ) ) ) {

      QMutexLocker _( &dataMutex );

      matches.push_back( WordMatch( trimmedWord, 1 ) );
    }
  }
  catch ( Iconv::Ex & e ) {
    qWarning( "Hunspell: charset conversion error, no processing's done: %s", e.what() );
  }

  finish();
}

sptr< WordSearchRequest > HunspellDictionary::prefixMatch( const QString & word, unsigned long )

{
  return std::make_shared< HunspellPrefixMatchRequest >( word, getHunspellMutex(), hunspell );
}

void getSuggestionsForExpression( const QString & expression,
                                  vector< QString > & suggestions,
                                  QMutex & hunspellMutex,
                                  Hunspell & hunspell )
{

  QString trimmedWord = Folding::trimWhitespaceOrPunct( expression );
  QString word, punct;
  QList< QString > words;

  suggestions.clear();

  for ( const QChar c : trimmedWord ) {
    if ( Folding::isPunct( c.unicode() ) ) {
      return;
    }
    if ( Folding::isWhitespace( c.unicode() ) ) {
      if ( !word.isEmpty() ) {
        words.push_back( word );
        word.clear();
      }
      continue;
    }
    word.push_back( c );
  }

  if ( !word.isEmpty() ) {
    words.push_back( word );
  }

  if ( words.size() > 4 ) {
    return;
  }

  QList< QString > results;

  for ( const auto & currentWord : words ) {
    QList< QString > sugg = suggest( currentWord, hunspellMutex, hunspell );
    int suggNum           = sugg.size() + 1;
    if ( suggNum > 3 ) {
      suggNum = 3;
    }
    int resNum = results.size();
    QString resultStr;

    if ( resNum == 0 ) {
      for ( int k = 0; k < suggNum; k++ ) {
        results.push_back( k == 0 ? currentWord : sugg.at( k - 1 ) );
      }
    }
    else {
      for ( int j = 0; j < resNum; j++ ) {
        resultStr = results.at( j );
        for ( int k = 0; k < suggNum; k++ ) {
          if ( k == 0 ) {
            results[ j ].append( currentWord );
          }
          else {
            results.push_back( resultStr + sugg.at( k - 1 ) );
          }
        }
      }
    }
  }

  for ( const auto & result : std::as_const( results ) ) {
    if ( result != trimmedWord ) {
      suggestions.push_back( result );
    }
  }
}

QString decodeFromHunspell( Hunspell & hunspell, const char * str )
{
  Iconv conv( hunspell.get_dic_encoding() );

  const void * in = str;
  size_t inLeft   = strlen( str );

  return conv.convert( in, inLeft );
}
}

vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Hunspell & cfg )

{
  vector< sptr< Dictionary::Class > > result;

  vector< DataFiles > dataFiles = findDataFiles( cfg.dictionariesPath );


  for ( const auto & enabledDictionarie : cfg.enabledDictionaries ) {
    for ( unsigned d = dataFiles.size(); d--; ) {
      if ( dataFiles[ d ].dictId == enabledDictionarie ) {

        vector< string > dictFiles;

        dictFiles.push_back( QDir::toNativeSeparators( dataFiles[ d ].affFileName ).toStdString() );
        dictFiles.push_back( QDir::toNativeSeparators( dataFiles[ d ].dicFileName ).toStdString() );

        result.push_back( std::make_shared< HunspellDictionary >( Dictionary::makeDictionaryId( dictFiles ),
                                                                  dataFiles[ d ].dictName.toUtf8().data(),
                                                                  dictFiles ) );
        break;
      }
    }
  }

  return result;
}

vector< DataFiles > findDataFiles( const QString & path )
{
  if ( path.isEmpty() ) {
    return vector< DataFiles >();
  }

  QDir dir( path );

  QFileInfoList affixFiles = dir.entryInfoList( ( QStringList() << "*.aff"
                                                                << "*.AFF" ),
                                                QDir::Files );

  vector< DataFiles > result;
  std::set< QString > presentNames;

  for ( QFileInfoList::const_iterator i = affixFiles.constBegin(); i != affixFiles.constEnd(); ++i ) {
    QString affFileName = i->absoluteFilePath();

    QString dicFileNameBase = affFileName.mid( 0, affFileName.size() - 3 );

    QString dicFileName = dicFileNameBase + "dic";

    if ( !QFile( dicFileName ).exists() ) {
      dicFileName = dicFileNameBase + "DIC";
      if ( !QFile( dicFileName ).exists() ) {
        continue;
      }
    }

    QString dictId = i->fileName();
    dictId.chop( 4 );

    QString dictBaseId =
      dictId.size() < 3 ? dictId : ( ( dictId[ 2 ] == '-' || dictId[ 2 ] == '_' ) ? dictId.mid( 0, 2 ) : QString() );

    dictBaseId = dictBaseId.toLower();

    QString localizedName;

    if ( dictBaseId.size() == 2 ) {
      localizedName = Language::localizedNameForId( LangCoder::code2toInt( dictBaseId.toLatin1().data() ) );
    }

    QString dictName = dictId;

    if ( localizedName.size() ) {
      dictName = localizedName;

      if ( dictId.size() > 2 && ( dictId[ 2 ] == '-' || dictId[ 2 ] == '_' )
           && dictId.mid( 3 ).toLower() != dictBaseId ) {
        dictName += " (" + dictId.mid( 3 ) + ")";
      }
    }

    dictName = QCoreApplication::translate( "Hunspell", "%1 Morphology" ).arg( dictName );

    if ( presentNames.insert( dictName ).second ) {

      result.push_back( DataFiles( affFileName, dicFileName, dictId, dictName ) );
    }
  }

  return result;
}

}
