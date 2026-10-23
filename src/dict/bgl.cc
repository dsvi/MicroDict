/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "bgl.hh"
#include "bgl_babylon.hh"
#include "btreeidx.hh"
#include "chunkedstorage.hh"
#include "dictfile.hh"
#include "folding.hh"
#include "ftshelpers.hh"
#include "htmlescape.hh"
#include "langcoder.hh"
#include "language.hh"
#include "text.hh"
#include "utils.hh"
#include <ctype.h>
#include <list>
#include <map>
#include <set>
#include <string.h>
#include <zlib.h>
#include <QAtomicInt>
#include <QCryptographicHash>
#include <QDir>
#include <QPainter>
#include <QRegularExpression>

namespace Bgl {

using std::map;
using std::multimap;
using std::set;
using std::list;
using std::pair;
using std::string;

using BtreeIndexing::WordArticleLink;
using BtreeIndexing::IndexedWords;
using BtreeIndexing::IndexInfo;

namespace {
enum {
  Signature            = 0x584c4742,
  CurrentFormatVersion = 19 + BtreeIndexing::FormatVersion
};

#pragma pack( push, 1 )

struct IdxHeader
{
  uint32_t signature;
  uint32_t formatVersion;
  uint32_t parserVersion;
  uint32_t foldingVersion;
  uint32_t articleCount;
  uint32_t wordCount;
  uint32_t chunksOffset;
  uint32_t indexBtreeMaxElements;
  uint32_t indexRootOffset;
  uint32_t resourceListOffset;
  uint32_t resourcesCount;
  uint32_t langFrom;
  uint32_t langTo;
  uint32_t iconAddress;
  uint32_t iconSize;
  uint32_t descriptionAddress;
  uint32_t descriptionSize;
};
static_assert( alignof( IdxHeader ) == 1 );
#pragma pack( pop )

bool indexIsOldOrBad( string const & indexFile )
{
  File::Index idx( indexFile, QIODevice::ReadOnly );

  IdxHeader header;

  return idx.readRecords( &header, sizeof( header ), 1 ) != 1 || header.signature != Signature
    || header.formatVersion != CurrentFormatVersion || header.parserVersion != Babylon::ParserVersion
    || header.foldingVersion != Folding::Version;
}

string removePostfix( const string & in )
{
  if ( in.size() && in[ in.size() - 1 ] == '$' ) {
    for ( long x = in.size() - 2; x >= 0; x-- ) {
      if ( in[ x ] == '$' ) {
        return in.substr( 0, x );
      }
      else if ( !isdigit( in[ x ] ) ) {
        break;
      }
    }
  }

  return in;
}

void trimWs( string & word )
{
  if ( word.size() ) {
    unsigned begin = 0;

    while ( begin < word.size() && Text::isspace( word[ begin ] ) ) {
      ++begin;
    }

    if ( begin == word.size() ) {
      word.clear();
    }
    else {
      unsigned end = word.size();

      while ( Text::isspace( word[ end - 1 ] ) ) {
        --end;
      }

      if ( end != word.size() || begin ) {
        word = string( word, begin, end - begin );
      }
    }
  }
}

void addEntryToIndex( string & word,
                      uint32_t articleOffset,
                      IndexedWords & indexedWords,
                      vector< char32_t > & wcharBuffer )
{
  trimWs( word );

  if ( word.size() && word[ 0 ] == '/' ) {
    return;
  }

  if ( word.size() && word[ word.size() - 1 ] == '$' ) {
    word = removePostfix( word );
    trimWs( word );
  }

  indexedWords.addWord( Text::toQString( word ), articleOffset );
}

class BglDictionary: public BtreeIndexing::BtreeDictionary
{
  QMutex idxMutex;
  File::Index idx;
  IdxHeader idxHeader;
  ChunkedStorage::Reader chunks;

public:

  BglDictionary( const string & id, const string & indexFile, const string & dictionaryFile );

  unsigned long getArticleCount() noexcept override
  {
    return idxHeader.articleCount;
  }

  unsigned long getWordCount() noexcept override
  {
    return idxHeader.wordCount;
  }

  inline quint32 getLangFrom() const override
  {
    return idxHeader.langFrom;
  }

  inline quint32 getLangTo() const override
  {
    return idxHeader.langTo;
  }

  sptr< Dictionary::WordSearchRequest > findHeadwordsForSynonym( const QString & ) override;

  sptr< Dictionary::DataRequest >
  getArticle( const QString &, const vector< QString > & alts, const QString &, bool ignoreDiacritics ) override;

  sptr< Dictionary::DataRequest > getResource( const string & name ) override;

  sptr< Dictionary::DataRequest >
  getSearchResults( const QString & searchString, int searchMode, bool matchCase, bool ignoreDiacritics ) override;
  const QString & getDescription() override;

  void getArticleText( uint32_t articleAddress, QString & headword, QString & text ) override;

  void makeFTSIndex( QAtomicInt & isCancelled ) override;

  void setFTSParameters( const Config::FullTextSearch & fts ) override
  {
    if ( metadata_enable_fts.has_value() ) {
      can_FTS = metadata_enable_fts.value();
    }
    else {
      can_FTS = fts.enabled && !fts.disabledTypes.contains( "BGL", Qt::CaseInsensitive )
        && ( fts.maxDictionarySize == 0 || getArticleCount() <= fts.maxDictionarySize );
    }
  }

protected:

  void loadIcon() noexcept override;

private:

  void loadArticle( uint32_t offset, string & headword, string & displayedHeadword, string & articleText );

  static void replaceCharsetEntities( string & );

  friend class BglHeadwordsRequest;
  friend class BglArticleRequest;
  friend class BglResourceRequest;
};

BglDictionary::BglDictionary( const string & id, const string & indexFile, const string & dictionaryFile ):
  BtreeDictionary( id, vector< string >( 1, dictionaryFile ) ),
  idx( indexFile, QIODevice::ReadOnly ),
  idxHeader( idx.read< IdxHeader >() ),
  chunks( idx, idxHeader.chunksOffset )
{
  idx.seek( sizeof( idxHeader ) );

  idx.readU32SizeAndData<>( dictionaryName );

  openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );

  ftsIdxName = indexFile + Dictionary::getFtsSuffix();
}

void BglDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( QString::fromStdString( getDictionaryFilenames()[ 0 ] ) );


  if ( !loadIconFromFileName( fileName ) ) {
    if ( idxHeader.iconSize ) {

      vector< char > chunk;

      QMutexLocker _( &idxMutex );

      char * iconData = chunks.getBlock( idxHeader.iconAddress, chunk );

      QImage img;

      if ( img.loadFromData( (unsigned char *)iconData, idxHeader.iconSize ) ) {

        int max = img.width() > img.height() ? img.width() : img.height();

        QImage result( max, max, QImage::Format_ARGB32 );
        result.fill( 0 );

        QPainter painter( &result );
        painter.setRenderHint( QPainter::RenderHint::Antialiasing );

        painter.drawImage( QPoint( img.width() == max ? 0 : ( max - img.width() ) / 2,
                                   img.height() == max ? 0 : ( max - img.height() ) / 2 ),
                           img );

        painter.end();

        dictionaryIcon = QIcon( QPixmap::fromImage( result ) );
      }
    }

    if ( dictionaryIcon.isNull() ) {
      dictionaryIcon = QIcon( ":/icons/icon32_bgl.png" );
    }
  }

  dictionaryIconLoaded = true;
}

void BglDictionary::loadArticle( uint32_t offset, string & headword, string & displayedHeadword, string & articleText )
{
  vector< char > chunk;

  QMutexLocker _( &idxMutex );

  char * articleData = chunks.getBlock( offset, chunk );

  headword = articleData;

  displayedHeadword = articleData + headword.size() + 1;

  articleText = string( articleData + headword.size() + displayedHeadword.size() + 2 );
}

const QString & BglDictionary::getDescription()
{
  if ( !dictionaryDescription.isEmpty() ) {
    return dictionaryDescription;
  }

  if ( idxHeader.descriptionSize == 0 ) {
    dictionaryDescription = "NONE";
  }
  else {
    QMutexLocker _( &idxMutex );
    vector< char > chunk;
    char * dictDescription = chunks.getBlock( idxHeader.descriptionAddress, chunk );
    string str( dictDescription );
    if ( !str.empty() ) {
      dictionaryDescription += QObject::tr( "Copyright: %1%2" )
                                 .arg( Html::unescape( QString::fromUtf8( str.data(), str.size() ) ) )
                                 .arg( "\n\n" );
    }
    dictDescription += str.size() + 1;

    str = string( dictDescription );
    if ( !str.empty() ) {
      dictionaryDescription +=
        QObject::tr( "Author: %1%2" ).arg( QString::fromUtf8( str.data(), str.size() ) ).arg( "\n\n" );
    }
    dictDescription += str.size() + 1;

    str = string( dictDescription );
    if ( !str.empty() ) {
      dictionaryDescription +=
        QObject::tr( "E-mail: %1%2" ).arg( QString::fromUtf8( str.data(), str.size() ) ).arg( "\n\n" );
    }
    dictDescription += str.size() + 1;

    str = string( dictDescription );
    if ( !str.empty() ) {
      dictionaryDescription += Html::unescape( QString::fromUtf8( str.data(), str.size() ) );
    }
  }

  return dictionaryDescription;
}

void BglDictionary::getArticleText( uint32_t articleAddress, QString & headword, QString & text )
{
  try {
    string headwordStr, displayedHeadwordStr, articleStr;
    loadArticle( articleAddress, headwordStr, displayedHeadwordStr, articleStr );

    trimWs( headwordStr );

    if ( headwordStr.size() && headwordStr[ 0 ] == '/' ) {
      headwordStr.erase();
    }

    if ( headwordStr.size() && headwordStr[ headwordStr.size() - 1 ] == '$' ) {
      headwordStr = removePostfix( headwordStr );
      trimWs( headwordStr );
    }

    headword = QString::fromUtf8( headwordStr.data(), headwordStr.size() );

    QString wstr = Text::toQString( articleStr );

    if ( getLangTo() == LangCoder::code2toInt( "he" ) ) {
      for ( QChar & c : wstr ) {
        if ( ( c.unicode() >= 224 && c.unicode() <= 250 ) || ( c.unicode() >= 192 && c.unicode() <= 210 ) ) {
          c = QChar( c.unicode() + 1488 - 224 );
        }
      }
    }

    text = Html::unescape( wstr );
  }
  catch ( std::exception & ex ) {
    qWarning( "BGL: Failed retrieving article from \"%s\", reason: %s", getName().c_str(), ex.what() );
  }
}

void BglDictionary::makeFTSIndex( QAtomicInt & isCancelled )
{
  if ( !( Dictionary::needToRebuildIndex( getDictionaryFilenames(), ftsIdxName )
          || FtsHelpers::ftsIndexIsOldOrBad( this ) ) ) {
    FTS_index_completed.ref();
  }

  if ( haveFTSIndex() ) {
    return;
  }


  qDebug( "Bgl: Building the full-text index for dictionary: %s", getName().c_str() );

  try {
    FtsHelpers::makeFTSIndex( this, isCancelled );
    FTS_index_completed.ref();
  }
  catch ( std::exception & ex ) {
    qWarning( "Bgl: Failed building full-text search index for \"%s\", reason: %s", getName().c_str(), ex.what() );
    QFile::remove( QString::fromStdString( ftsIdxName ) );
  }
}

class BglHeadwordsRequest: public Dictionary::WordSearchRequest
{
  QString str;
  BglDictionary & dict;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  BglHeadwordsRequest( const QString & word_, BglDictionary & dict_ ):
    str( word_ ),
    dict( dict_ )
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

  ~BglHeadwordsRequest() override
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void BglHeadwordsRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  vector< WordArticleLink > chain = dict.findArticles( str );

  QString caseFolded = Folding::applySimpleCaseOnly( str );

  for ( auto & x : chain ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      finish();
      return;
    }

    string headword, displayedHeadword, articleText;

    dict.loadArticle( x.articleOffset, headword, displayedHeadword, articleText );

    QString headwordDecoded;
    try {
      headwordDecoded = Text::toQString( removePostfix( headword ) );
    }
    catch ( Text::exCantDecode & ) {
    }

    if ( caseFolded != Folding::applySimpleCaseOnly( headwordDecoded ) && !headwordDecoded.isEmpty() ) {
      QMutexLocker _( &dataMutex );

      matches.push_back( headwordDecoded );
    }
  }

  finish();
}

sptr< Dictionary::WordSearchRequest > BglDictionary::findHeadwordsForSynonym( const QString & word )

{
  return synonymSearchEnabled ? std::make_shared< BglHeadwordsRequest >( word, *this ) :
                                Class::findHeadwordsForSynonym( word );
}

string postfixToSuperscript( const string & in )
{
  if ( !in.size() || in[ in.size() - 1 ] != '$' ) {
    return in;
  }

  for ( long x = in.size() - 2; x >= 0; x-- ) {
    if ( in[ x ] == '$' ) {
      if ( in.size() - x - 2 > 2 ) {
        return in.substr( 0, x );
      }
      else {
        return in.substr( 0, x ) + "<sup>" + in.substr( x + 1, in.size() - x - 2 ) + "</sup>";
      }
    }
    else if ( !isdigit( in[ x ] ) ) {
      break;
    }
  }

  return in;
}

class BglArticleRequest: public Dictionary::DataRequest
{
  QString word;
  vector< QString > alts;
  BglDictionary & dict;

  QAtomicInt isCancelled;
  bool ignoreDiacritics;
  QFuture< void > f;

public:

  BglArticleRequest( const QString & word_,
                     const vector< QString > & alts_,
                     BglDictionary & dict_,
                     bool ignoreDiacritics_ ):
    word( word_ ),
    alts( alts_ ),
    dict( dict_ ),
    ignoreDiacritics( ignoreDiacritics_ )
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

  void fixHebString( string & hebStr );
  void fixHebArticle( string & hebArticle );

  ~BglArticleRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void BglArticleRequest::fixHebString( string & hebStr )
{
  QString hebWStr;
  try {
    hebWStr = Text::toQString( hebStr );
  }
  catch ( Text::exCantDecode & ) {
    hebStr = "Utf-8 decoding error";
    return;
  }

  for ( QChar & c : hebWStr ) {
    if ( ( c.unicode() >= 224 && c.unicode() <= 250 ) || ( c.unicode() >= 192 && c.unicode() <= 210 ) ) {
      c = QChar( c.unicode() + 1488 - 224 );
    }
  }
  hebStr = Text::toUtf8( hebWStr );
}

void BglArticleRequest::fixHebArticle( string & hebArticle )
{
  unsigned nulls;

  for ( nulls = hebArticle.size(); nulls > 0
        && ( ( hebArticle[ nulls - 1 ] <= 32 && hebArticle[ nulls - 1 ] >= 0 )
             || ( hebArticle[ nulls - 1 ] >= 65 && hebArticle[ nulls - 1 ] <= 90 ) );
        --nulls ) {
    ;
  }

  hebArticle.resize( nulls );
}

void BglArticleRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  vector< WordArticleLink > chain = dict.findArticles( word, ignoreDiacritics );

  static Language::Id hebrew = LangCoder::code2toInt( "he" );

  for ( const auto & alt : alts ) {

    vector< WordArticleLink > altChain = dict.findArticles( alt, ignoreDiacritics );

    chain.insert( chain.end(), altChain.begin(), altChain.end() );
  }

  multimap< QString, pair< string, string > > mainArticles, alternateArticles;

  set< uint32_t > articlesIncluded;
  set< QByteArray > articleBodiesIncluded;

  QString wordCaseFolded = Folding::applySimpleCaseOnly( word );
  if ( ignoreDiacritics ) {
    wordCaseFolded = Folding::applyDiacriticsOnly( wordCaseFolded );
  }

  for ( auto & x : chain ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      finish();
      return;
    }

    try {

      if ( articlesIncluded.find( x.articleOffset ) != articlesIncluded.end() ) {
        continue;
      }

      string headword, displayedHeadword, articleText;

      dict.loadArticle( x.articleOffset, headword, displayedHeadword, articleText );

      QString headwordStripped = Folding::applySimpleCaseOnly( removePostfix( headword ) );
      if ( ignoreDiacritics ) {
        headwordStripped = Folding::applyDiacriticsOnly( headwordStripped );
      }

      if ( dict.idxHeader.langFrom == hebrew ) {
        displayedHeadword = displayedHeadword.size() ? displayedHeadword : headword;
        fixHebString( articleText );
        fixHebArticle( articleText );
        fixHebString( displayedHeadword );
      }

      const string & targetHeadword = displayedHeadword.size() ? displayedHeadword : headword;

      QCryptographicHash hash( QCryptographicHash::Sha256 );
      hash.addData( { targetHeadword.data(), static_cast< qsizetype >( targetHeadword.size() + 1 ) } );
      hash.addData( { articleText.data(), static_cast< qsizetype >( articleText.size() ) } );

      if ( !articleBodiesIncluded.insert( hash.result() ).second ) {
        continue;
      }

      multimap< QString, pair< string, string > > & mapToUse =
        ( wordCaseFolded == headwordStripped ) ? mainArticles : alternateArticles;

      mapToUse.insert( pair( Folding::applySimpleCaseOnly( headword ), pair( targetHeadword, articleText ) ) );

      articlesIncluded.insert( x.articleOffset );
    }
    catch ( std::exception & ex ) {
      qWarning( "BGL: Failed loading article from \"%s\", reason: %s", dict.getName().c_str(), ex.what() );
    }
  }

  if ( mainArticles.empty() && alternateArticles.empty() ) {
    finish();
    return;
  }

  string result;

  multimap< QString, pair< string, string > >::const_iterator i;

  string cleaner = Utils::Html::getHtmlCleaner();
  for ( i = mainArticles.begin(); i != mainArticles.end(); ++i ) {
    if ( dict.isFromLanguageRTL() ) {
      result += "<h3 style=\"text-align:right;direction:rtl\">";
    }
    else {
      result += "<h3>";
    }
    result += postfixToSuperscript( i->second.first );
    result += "</h3>";
    if ( dict.isToLanguageRTL() ) {
      result += "<div class=\"bglrtl\">" + i->second.second + "</div>";
    }
    else {
      result += "<div>" + i->second.second + "</div>";
    }
    result += cleaner;
  }


  for ( i = alternateArticles.begin(); i != alternateArticles.end(); ++i ) {
    if ( dict.isFromLanguageRTL() ) {
      result += "<h3 style=\"text-align:right;direction:rtl\">";
    }
    else {
      result += "<h3>";
    }
    result += postfixToSuperscript( i->second.first );
    result += "</h3>";
    if ( dict.isToLanguageRTL() ) {
      result += "<div class=\"bglrtl\">" + i->second.second + "</div>";
    }
    else {
      result += "<div>" + i->second.second + "</div>";
    }
    result += cleaner;
  }

  BglDictionary::replaceCharsetEntities( result );

  result =
    QString::fromUtf8( result.c_str() )
      .replace( QRegularExpression(
                  R"(<([a-z0-9]+)\s+[^>]*onclick="[a-z.]*location(?:\.href)\s*=\s*'([^']+)[^>]*>([^<]+)</\1>)",
                  QRegularExpression::CaseInsensitiveOption ),
                R"(<a href="\2">\3</a>)" )
      .replace(
        QRegularExpression( R"((<\s*a\s+[^>]*href\s*=\s*["']\s*)bword://)", QRegularExpression::CaseInsensitiveOption ),
        "\\1bword:" )
      .replace( QRegularExpression( R"((width|height)\s*=\s*["']\d{7,}["''])" ), "" )
      .replace(
        QRegularExpression(
          R"(<br>(<div|<table|<tbody|<tr|<td|</div>|</table>|</tbody>|</tr>|</td>|function addScript|var scNode|scNode|var atag|while\(atag|atag=atag|document\.getElementsByTagName|addScript|src="bres|<a onmouseover="return overlib|onclick="return overlib))",
          QRegularExpression::CaseInsensitiveOption ),
        "\\1" )
      .replace(
        QRegularExpression(
          R"((AUTOSTATUS, WRAP\);" |</DIV>|addScript\('JS_FILE_PHONG_VT_45634'\);|appendChild\(scNode\);|atag\.firstChild;)<br>)",
          QRegularExpression::CaseInsensitiveOption ),
        " \\1 " )
      .toUtf8()
      .data();


  appendString( result );

  hasAnyData = true;

  finish();
}

sptr< Dictionary::DataRequest > BglDictionary::getArticle( const QString & word,
                                                           const vector< QString > & alts,
                                                           const QString &,
                                                           bool ignoreDiacritics )

{
  return std::make_shared< BglArticleRequest >( word, alts, *this, ignoreDiacritics );
}

class BglResourceRequest: public Dictionary::DataRequest
{

  QMutex & idxMutex;
  File::Index & idx;
  uint32_t resourceListOffset, resourcesCount;
  string name;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  BglResourceRequest( QMutex & idxMutex_,
                      File::Index & idx_,
                      uint32_t resourceListOffset_,
                      uint32_t resourcesCount_,
                      const string & name_ ):
    idxMutex( idxMutex_ ),
    idx( idx_ ),
    resourceListOffset( resourceListOffset_ ),
    resourcesCount( resourcesCount_ ),
    name( name_ )
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

  ~BglResourceRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void BglResourceRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  string nameLowercased = name;

  for ( char & i : nameLowercased ) {
    i = tolower( i );
  }

  QMutexLocker _( &idxMutex );

  idx.seek( resourceListOffset );

  for ( size_t count = resourcesCount; count--; ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      break;
    }

    vector< char > nameData;
    idx.readU32SizeAndData<>( nameData );

    for ( size_t x = nameData.size(); x--; ) {
      nameData[ x ] = tolower( nameData[ x ] );
    }

    uint32_t offset = idx.read< uint32_t >();

    if ( string( &nameData.front(), nameData.size() ) == nameLowercased ) {

      idx.seek( offset );

      QMutexLocker _( &dataMutex );

      data.resize( idx.read< uint32_t >() );

      vector< unsigned char > compressedData;

      idx.readU32SizeAndData<>( compressedData );

      unsigned long decompressedLength = data.size();

      if ( uncompress( (unsigned char *)&data.front(),
                       &decompressedLength,
                       &compressedData.front(),
                       compressedData.size() )
             != Z_OK
           || decompressedLength != data.size() ) {
        qWarning( "Failed to decompress resource \"%s\", ignoring it.", name.c_str() );
      }
      else {
        hasAnyData = true;
      }

      break;
    }
  }

  finish();
}

sptr< Dictionary::DataRequest > BglDictionary::getResource( const string & name )

{
  return std::shared_ptr< BglResourceRequest >(
    new BglResourceRequest( idxMutex, idx, idxHeader.resourceListOffset, idxHeader.resourcesCount, name ) );
}

void BglDictionary::replaceCharsetEntities( string & text )
{
  QString str = QString::fromUtf8( text.c_str() );

  QRegularExpression charsetExp(
    R"(<\s*charset\s+c\s*=\s*["']?t["']?\s*>((?:\s*[0-9a-fA-F]+\s*;\s*)*)<\s*/\s*charset\s*>)",
    QRegularExpression::CaseInsensitiveOption );

  QRegularExpression oneValueExp( "\\s*([0-9a-fA-F]+)\\s*;" );
  QString result;
  int pos = 0;

  QRegularExpressionMatchIterator it = charsetExp.globalMatch( str );
  while ( it.hasNext() ) {
    QRegularExpressionMatch match = it.next();
    result += str.mid( pos, match.capturedStart() - pos );
    pos = match.capturedEnd();

    QRegularExpressionMatchIterator itValue = oneValueExp.globalMatch( match.captured( 1 ) );
    while ( itValue.hasNext() ) {
      QRegularExpressionMatch matchValue = itValue.next();
      result += "&#x" + matchValue.captured( 1 ) + ";";
    }
  }

  if ( pos ) {
    result += str.mid( pos );
    str = result;
  }


  text = str.toUtf8().data();
}

class ResourceHandler: public Babylon::ResourceHandler
{
  File::Index & idxFile;
  list< pair< string, uint32_t > > resources;

public:

  ResourceHandler( File::Index & idxFile_ ):
    idxFile( idxFile_ )
  {
  }

  const list< pair< string, uint32_t > > & getResources() const
  {
    return resources;
  }

protected:
  void handleBabylonResource( const string & filename, const char * data, size_t size ) override;
};

void ResourceHandler::handleBabylonResource( const string & filename, const char * data, size_t size )
{

  vector< unsigned char > compressedData( compressBound( size ) );

  unsigned long compressedSize = compressedData.size();

  if ( compress( &compressedData.front(), &compressedSize, (const unsigned char *)data, size ) != Z_OK ) {
    qWarning( "Failed to compress the body of resource \"%s\", dropping it.", filename.c_str() );
    return;
  }

  resources.push_back( pair< string, uint32_t >( filename, idxFile.tell() ) );

  idxFile.write< uint32_t >( size );
  idxFile.write< uint32_t >( compressedSize );
  idxFile.write( &compressedData.front(), compressedSize );
}
}

sptr< Dictionary::DataRequest > BglDictionary::getSearchResults( const QString & searchString,
                                                                 int searchMode,
                                                                 bool matchCase,

                                                                 bool ignoreDiacritics )
{
  return std::make_shared< FtsHelpers::FTSResultsRequest >( *this,
                                                            searchString,
                                                            searchMode,
                                                            matchCase,
                                                            ignoreDiacritics );
}


vector< sptr< Dictionary::Class > > makeDictionaries( const vector< string > & fileNames,
                                                      const string & indicesDir,
                                                      Dictionary::Initializing & initializing )

{
  vector< sptr< Dictionary::Class > > dictionaries;

  for ( const auto & fileName : fileNames ) {
    if ( !Utils::endsWithIgnoreCase( fileName, ".bgl" ) ) {
      continue;
    }

    vector< string > dictFiles( 1, fileName );

    string dictId = Dictionary::makeDictionaryId( dictFiles );

    string indexFile = indicesDir + dictId;

    if ( Dictionary::needToRebuildIndex( dictFiles, indexFile ) || indexIsOldOrBad( indexFile ) ) {

      qDebug( "Bgl: Building the index for dictionary: %s", fileName.c_str() );

      try {
        Babylon b( fileName );

        if ( !b.open() ) {
          continue;
        }

        std::string sourceCharset, targetCharset;

        if ( !b.read( sourceCharset, targetCharset ) ) {
          qWarning( "Failed to start reading from %s, skipping it", fileName.c_str() );
          continue;
        }

        initializing.indexingDictionary( b.title() );

        File::Index idx( indexFile, QIODevice::WriteOnly );

        IdxHeader idxHeader;

        memset( &idxHeader, 0, sizeof( idxHeader ) );

        idx.write( idxHeader );

        idx.write< uint32_t >( b.title().size() );
        idx.write( b.title().data(), b.title().size() );

        IndexedWords indexedWords;

        vector< char32_t > wcharBuffer;

        ChunkedStorage::Writer chunks( idx );

        uint32_t articleCount = 0, wordCount = 0;

        ResourceHandler resourceHandler( idx );

        b.setResourcePrefix( string( "bres://" ) + dictId + "/" );

        if ( size_t sz = b.getIcon().size() ) {
          idxHeader.iconAddress = chunks.startNewBlock();
          chunks.addToBlock( &b.getIcon().front(), sz );
          idxHeader.iconSize = sz;
        }

        idxHeader.descriptionSize    = 0;
        idxHeader.descriptionAddress = chunks.startNewBlock();

        chunks.addToBlock( b.copyright().c_str(), b.copyright().size() + 1 );
        idxHeader.descriptionSize += b.copyright().size() + 1;

        chunks.addToBlock( b.author().c_str(), b.author().size() + 1 );
        idxHeader.descriptionSize += b.author().size() + 1;

        chunks.addToBlock( b.email().c_str(), b.email().size() + 1 );
        idxHeader.descriptionSize += b.email().size() + 1;

        chunks.addToBlock( b.description().c_str(), b.description().size() + 1 );
        idxHeader.descriptionSize += b.description().size() + 1;

        for ( ;; ) {
          bgl_entry e = b.readEntry( &resourceHandler );

          if ( e.headword.empty() ) {
            break;
          }

          uint32_t articleAddress = chunks.startNewBlock();

          chunks.addToBlock( e.headword.c_str(), e.headword.size() + 1 );
          chunks.addToBlock( e.displayedHeadword.c_str(), e.displayedHeadword.size() + 1 );
          chunks.addToBlock( e.definition.c_str(), e.definition.size() + 1 );

          addEntryToIndex( e.headword, articleAddress, indexedWords, wcharBuffer );

          for ( auto & alternate : e.alternates ) {
            addEntryToIndex( alternate, articleAddress, indexedWords, wcharBuffer );
          }

          wordCount += 1 + e.alternates.size();
          ++articleCount;
        }

        idxHeader.chunksOffset = chunks.finish();

        qDebug( "Writing index..." );

        IndexInfo idxInfo = BtreeIndexing::buildIndex( indexedWords, idx );

        idxHeader.indexBtreeMaxElements = idxInfo.btreeMaxElements;
        idxHeader.indexRootOffset       = idxInfo.rootOffset;

        idxHeader.resourceListOffset = idx.tell();
        idxHeader.resourcesCount     = resourceHandler.getResources().size();

        for ( const auto & j : resourceHandler.getResources() ) {
          idx.write< uint32_t >( j.first.size() );
          idx.write( j.first.data(), j.first.size() );
          idx.write< uint32_t >( j.second );
        }

        idxHeader.signature      = Signature;
        idxHeader.formatVersion  = CurrentFormatVersion;
        idxHeader.parserVersion  = Babylon::ParserVersion;
        idxHeader.foldingVersion = Folding::Version;
        idxHeader.articleCount   = articleCount;
        idxHeader.wordCount      = wordCount;
        idxHeader.langFrom       = b.sourceLang();
        idxHeader.langTo         = b.targetLang();

        idx.rewind();

        idx.write( &idxHeader, sizeof( idxHeader ) );
      }
      catch ( std::exception & e ) {
        qWarning( "BGL dictionary indexing failed: %s, error: %s", fileName.c_str(), e.what() );
      }
    }

    try {
      dictionaries.push_back( std::make_shared< BglDictionary >( dictId, indexFile, fileName ) );
    }
    catch ( std::exception & e ) {
      qWarning( "BGL dictionary initializing failed: %s, error: %s", fileName.c_str(), e.what() );
    }
  }

  return dictionaries;
}

}
