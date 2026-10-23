/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "dsl.hh"
#include "dsl_details.hh"
#include "btreeidx.hh"
#include "folding.hh"
#include "text.hh"
#include "chunkedstorage.hh"
#include "dictzip.hh"
#include "htmlescape.hh"
#include "iconv.hh"
#include "filetype.hh"
#include "audiolink.hh"
#include "langcoder.hh"
#include "indexedzip.hh"
#include "tiff.hh"
#include "ftshelpers.hh"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <wctype.h>
#include <QThreadPool>
#include <QAtomicInt>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QByteArray>
#include <QtConcurrentRun>
#include "utils.hh"
#include "common/globalbroadcaster.hh"

namespace Dsl {

using namespace Details;

using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::list;
using Text::Encoding;

using BtreeIndexing::WordArticleLink;
using BtreeIndexing::IndexedWords;
using BtreeIndexing::IndexInfo;

namespace {

DEF_EX( exUserAbort, "User abort", Dictionary::Ex )
DEF_EX_STR( exDictzipError, "DICTZIP error", Dictionary::Ex )

enum {
  Signature                = 0x584c5344,
  CurrentFormatVersion     = 23 + BtreeIndexing::FormatVersion + Folding::Version + BtreeIndexing::ZipParseLogicVersion,
  CurrentZipSupportVersion = 2,
  CurrentFtsIndexVersion   = 7
};

#pragma pack( push, 1 )

struct IdxHeader
{
  uint32_t signature;
  uint32_t formatVersion;
  uint32_t zipSupportVersion;
  uint32_t dslEncoding;
  uint32_t chunksOffset;
  uint32_t hasAbrv;
  uint32_t abrvAddress;
  uint32_t indexBtreeMaxElements;
  uint32_t indexRootOffset;
  uint32_t articleCount;
  uint32_t wordCount;
  uint32_t langFrom;
  uint32_t langTo;
  uint32_t hasZipFile;
  uint32_t hasSoundDictionaryName;
  uint32_t zipIndexBtreeMaxElements;
  uint32_t zipIndexRootOffset;
};
static_assert( alignof( IdxHeader ) == 1 );
#pragma pack( pop )


struct InsidedCard
{
  uint32_t offset;
  uint32_t size;
  QList< QString > headwords;
  InsidedCard( uint32_t _offset, uint32_t _size, const QList< QString > & words ):
    offset( _offset ),
    size( _size ),
    headwords( words )
  {
  }
  InsidedCard() = default;
};

bool indexIsOldOrBad( const string & indexFile, bool hasZipFile )
{
  File::Index idx( indexFile, QIODevice::ReadOnly );

  IdxHeader header;

  return idx.readRecords( &header, sizeof( header ), 1 ) != 1 || header.signature != Signature
    || header.formatVersion != CurrentFormatVersion || (bool)header.hasZipFile != hasZipFile
    || ( hasZipFile && header.zipSupportVersion != CurrentZipSupportVersion );
}

class DslDictionary: public BtreeIndexing::BtreeDictionary
{
  QMutex idxMutex;
  File::Index idx;
  IdxHeader idxHeader;
  sptr< ChunkedStorage::Reader > chunks;
  string preferredSoundDictionary;
  map< string, string > abrv;
  QMutex dzMutex;
  dictData * dz;
  QMutex resourceZipMutex;
  IndexedZip resourceZip;
  BtreeIndex resourceZipIndex;

  QAtomicInt deferredInitDone;
  QMutex deferredInitMutex;
  bool deferredInitRunnableStarted;

  string initError;

  int optionalPartNom;
  quint8 articleNom;

  QString currentHeadword;
  string resourceDir1, resourceDir2;

public:

  DslDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles );

  void deferredInit() override;

  ~DslDictionary();


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

  inline virtual string getResourceDir1() const
  {
    return resourceDir1;
  }

  inline virtual string getResourceDir2() const
  {
    return resourceDir2;
  }


  sptr< Dictionary::DataRequest >
  getArticle( const QString &, const vector< QString > & alts, const QString &, bool ignoreDiacritics ) override;

  sptr< Dictionary::DataRequest > getResource( const string & name ) override;

  sptr< Dictionary::DataRequest >
  getSearchResults( const QString & searchString, int searchMode, bool matchCase, bool ignoreDiacritics ) override;
  const QString & getDescription() override;

  QString getMainFilename() override;

  void getArticleText( uint32_t articleAddress, QString & headword, QString & text ) override;

  void makeFTSIndex( QAtomicInt & isCancelled ) override;

  void setFTSParameters( const Config::FullTextSearch & fts ) override
  {
    if ( ensureInitDone().size() ) {
      return;
    }
    if ( metadata_enable_fts.has_value() ) {
      can_FTS = metadata_enable_fts.value();
    }
    else {
      can_FTS = fts.enabled && !fts.disabledTypes.contains( "DSL", Qt::CaseInsensitive )
        && ( fts.maxDictionarySize == 0 || getArticleCount() <= fts.maxDictionarySize );
    }
  }

  uint32_t getFtsIndexVersion() override
  {
    return CurrentFtsIndexVersion;
  }

protected:

  void loadIcon() noexcept override;

private:

  const string & ensureInitDone() override;
  void doDeferredInit();

  void loadArticle( uint32_t address,
                    const QString & requestedHeadwordFolded,
                    bool ignoreDiacritics,
                    QString & tildeValue,
                    QString & displayedHeadword,
                    unsigned & headwordIndex,
                    QString & articleText );

  string dslToHtml( const QString &, const QString & headword = QString() );

  string nodeToHtml( const ArticleDom::Node & );
  string processNodeChildren( const ArticleDom::Node & node );
  string getNodeLink( const ArticleDom::Node & node );

  bool hasHiddenZones()
  {
    return optionalPartNom != 0;
  }

  friend class DslArticleRequest;
  friend class DslResourceRequest;
};

DslDictionary::DslDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles ):
  BtreeDictionary( id, dictionaryFiles ),
  idx( indexFile, QIODevice::ReadOnly ),
  idxHeader( idx.read< IdxHeader >() ),
  dz( 0 ),
  deferredInitRunnableStarted( false ),
  optionalPartNom( 0 ),
  articleNom( 0 )
{

  ftsIdxName = indexFile + Dictionary::getFtsSuffix();

  idx.seek( sizeof( idxHeader ) );

  idx.readU32SizeAndData<>( dictionaryName );
  idx.readU32SizeAndData<>( preferredSoundDictionary );


  resourceDir1 = getDictionaryFilenames()[ 0 ] + ".files" + Utils::Fs::separator();
  QString s    = QString::fromStdString( getDictionaryFilenames()[ 0 ] );
  if ( s.endsWith( QString::fromLatin1( ".dz" ), Qt::CaseInsensitive ) ) {
    s.chop( 3 );
  }
  resourceDir2 = s.toStdString() + ".files" + Utils::Fs::separator();

}

DslDictionary::~DslDictionary()
{
  QMutexLocker _( &deferredInitMutex );

  if ( dz ) {
    dict_data_close( dz );
  }
}

void DslDictionary::deferredInit()
{
  if ( !Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
    QMutexLocker _( &deferredInitMutex );

    if ( Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
      return;
    }

    if ( !deferredInitRunnableStarted ) {
      QThreadPool::globalInstance()->start(
        [ this ]() {
          this->doDeferredInit();
        },
        -1000 );
      deferredInitRunnableStarted = true;
    }
  }
}


const string & DslDictionary::ensureInitDone()
{
  doDeferredInit();

  return initError;
}

void DslDictionary::doDeferredInit()
{
  if ( !Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
    QMutexLocker _( &deferredInitMutex );

    if ( Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
      return;
    }

    try {

      chunks = std::shared_ptr< ChunkedStorage::Reader >( new ChunkedStorage::Reader( idx, idxHeader.chunksOffset ) );

      DZ_ERRORS error;
      dz = dict_data_open( getDictionaryFilenames()[ 0 ].c_str(), &error, 0 );

      if ( !dz ) {
        throw exDictzipError( string( dz_error_str( error ) ) + "(" + getDictionaryFilenames()[ 0 ] + ")" );
      }

      if ( idxHeader.hasAbrv ) {
        vector< char > chunk;

        char * abrvBlock = chunks->getBlock( idxHeader.abrvAddress, chunk );

        uint32_t total;
        memcpy( &total, abrvBlock, sizeof( uint32_t ) );
        abrvBlock += sizeof( uint32_t );

        qDebug( "DSL: %s loading %u abbrv", getName().c_str(), total );

        while ( total-- ) {
          uint32_t keySz;
          memcpy( &keySz, abrvBlock, sizeof( uint32_t ) );
          abrvBlock += sizeof( uint32_t );

          char * key = abrvBlock;

          abrvBlock += keySz;

          uint32_t valueSz;
          memcpy( &valueSz, abrvBlock, sizeof( uint32_t ) );
          abrvBlock += sizeof( uint32_t );

          abrv[ string( key, keySz ) ] = string( abrvBlock, valueSz );

          abrvBlock += valueSz;
        }
      }

      openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );

      if ( idxHeader.hasZipFile && ( idxHeader.zipIndexBtreeMaxElements || idxHeader.zipIndexRootOffset ) ) {
        resourceZip.openIndex( IndexInfo( idxHeader.zipIndexBtreeMaxElements, idxHeader.zipIndexRootOffset ),
                               idx,
                               idxMutex );

        QString zipName = QDir::fromNativeSeparators( getDictionaryFilenames().back().c_str() );

        if ( zipName.endsWith( ".zip", Qt::CaseInsensitive ) ) {
          resourceZip.openZipFile( zipName );
        }
      }
    }
    catch ( std::exception & e ) {
      initError = e.what();
    }
    catch ( ... ) {
      initError = "Unknown error";
    }

    deferredInitDone.ref();
  }
}


void DslDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  if ( fileName.endsWith( ".dsl.dz", Qt::CaseInsensitive ) ) {
    fileName.chop( 6 );
  }
  else {
    fileName.chop( 3 );
  }

  if ( !loadIconFromFileName( fileName ) ) {
    dictionaryIcon = QIcon( ":/icons/icon32_dsl.png" );
  }

  dictionaryIconLoaded = true;
}

bool isDslWs( QChar ch )
{
  switch ( ch.unicode() ) {
    case ' ':
    case '\t':
      return true;
    default:
      return false;
  }
}

qsizetype dslFindFirstOf( const QString & haystack, std::u16string_view chars, qsizetype from )
{
  qsizetype best = -1;

  for ( const char16_t c : chars ) {
    const qsizetype i = haystack.indexOf( QChar( c ), from );
    if ( i >= 0 && ( best < 0 || i < best ) ) {
      best = i;
    }
  }

  return best;
}

qsizetype dslFindFirstNotOf( const QString & haystack, std::u16string_view chars, qsizetype from = 0 )
{
  qsizetype i = from;
  while ( i < haystack.size() && chars.find( haystack[ i ].unicode() ) != std::u16string_view::npos ) {
    ++i;
  }
  return i;
}

void DslDictionary::loadArticle( uint32_t address,
                                 const QString & requestedHeadwordFolded,
                                 bool ignoreDiacritics,
                                 QString & tildeValue,
                                 QString & displayedHeadword,
                                 unsigned & headwordIndex,
                                 QString & articleText )
{
  QString articleData;

  {
    vector< char > chunk;

    char * articleProps;

    {
      QMutexLocker _( &idxMutex );

      articleProps = chunks->getBlock( address, chunk );
    }

    uint32_t articleOffset, articleSize;

    memcpy( &articleOffset, articleProps, sizeof( articleOffset ) );
    memcpy( &articleSize, articleProps + sizeof( articleOffset ), sizeof( articleSize ) );

    qDebug( "offset = %x", articleOffset );


    char * articleBody;

    {
      QMutexLocker _( &dzMutex );

      articleBody = dict_data_read_( dz, articleOffset, articleSize, 0, 0 );
    }

    if ( !articleBody ) {
      articleData = QString( "\n\r\tDICTZIP error: " ) + QString( dict_error_str( dz ) );
    }
    else {
      try {
        articleData =
          Iconv::toQString( Text::getEncodingNameFor( Encoding( idxHeader.dslEncoding ) ), articleBody, articleSize );
        free( articleBody );

        bool b = false;
        stripComments( articleData, b );
      }
      catch ( ... ) {
        free( articleBody );
        throw;
      }
    }
  }

  qsizetype pos               = 0;
  bool hadFirstHeadword       = false;
  bool foundDisplayedHeadword = false;

  bool insidedCard = isDslWs( articleData.at( 0 ) );

  QString tildeValueWithUnsorted;
  for ( headwordIndex = 0;; ) {
    qsizetype begin = pos;

    pos = dslFindFirstOf( articleData, u"\n\r", begin );

    if ( pos < 0 ) {
      pos = articleData.size();
    }

    if ( !foundDisplayedHeadword ) {

      QString rawHeadword = articleData.mid( begin, pos - begin );

      if ( insidedCard && !rawHeadword.isEmpty() && isDslWs( rawHeadword[ 0 ] ) ) {
        qsizetype hpos = rawHeadword.indexOf( u'@' );
        if ( hpos >= 0 ) {
          QString head = Folding::trimWhitespace( rawHeadword.mid( hpos + 1 ) );
          hpos         = head.indexOf( u'~' );
          while ( hpos >= 0 ) {
            if ( hpos == 0 || head[ hpos ] != u'\\' ) {
              break;
            }
            hpos = head.indexOf( u'~', hpos + 1 );
          }
          if ( hpos < 0 ) {
            rawHeadword = head;
          }
          else {
            rawHeadword.clear();
          }
        }
      }

      if ( !rawHeadword.isEmpty() ) {
        if ( !hadFirstHeadword ) {
          tildeValue = rawHeadword;

          list< QString > lst;

          expandOptionalParts( tildeValue, &lst );

          if ( lst.size() ) {
            tildeValue = lst.front();
          }

          tildeValueWithUnsorted = tildeValue;

          processUnsortedParts( tildeValue, false );
        }
        QString str = rawHeadword;

        if ( hadFirstHeadword ) {
          expandTildes( str, tildeValueWithUnsorted );
        }

        processUnsortedParts( str, true );

        str = Folding::applySimpleCaseOnly( str );

        list< QString > lst;
        expandOptionalParts( str, &lst );

        for ( auto & i : lst ) {
          unescapeDsl( i );
          normalizeHeadword( i );

          bool found;
          if ( ignoreDiacritics ) {
            found = Folding::applyDiacriticsOnly( Folding::trimWhitespace( i ) )
              == Folding::applyDiacriticsOnly( requestedHeadwordFolded );
          }
          else {
            found = Folding::trimWhitespace( i ) == requestedHeadwordFolded;
          }

          if ( found ) {
            if ( hadFirstHeadword ) {
              expandTildes( rawHeadword, tildeValueWithUnsorted );
            }

            processUnsortedParts( rawHeadword, false );

            displayedHeadword = rawHeadword;

            foundDisplayedHeadword = true;
            break;
          }
        }

        if ( !foundDisplayedHeadword ) {
          ++headwordIndex;
          hadFirstHeadword = true;
        }
      }
    }


    if ( pos == articleData.size() ) {
      break;
    }

    if ( articleData[ pos ] == u'\r' ) {
      ++pos;
    }

    if ( pos != articleData.size() ) {
      if ( articleData[ pos ] == u'\n' ) {
        ++pos;
      }
    }

    if ( pos == articleData.size() ) {
      break;
    }
    if ( isDslWs( articleData[ pos ] ) ) {
      if ( insidedCard ) {
        qsizetype hpos = dslFindFirstOf( articleData, u"\n\r", pos );
        if ( hpos < 0 ) {
          hpos = articleData.size();
        }

        QString str = articleData.mid( pos, hpos - pos );

        hpos = str.indexOf( u'@' );
        if ( hpos < 0 || str[ hpos - 1 ] == u'\\' || !isAtSignFirst( str ) ) {
          break;
        }
      }
      else {
        break;
      }
    }
  }

  if ( !foundDisplayedHeadword ) {
    if ( insidedCard ) {
      displayedHeadword = requestedHeadwordFolded;
    }
    else {
      displayedHeadword = tildeValue;
    }
  }

  if ( pos != articleData.size() ) {
    articleText = articleData.mid( pos );
  }
  else {
    articleText.clear();
  }
}

string DslDictionary::dslToHtml( const QString & str, const QString & headword )
{
  QString normalizedStr = Text::normalize( str );
  currentHeadword       = headword;

  ArticleDom dom( normalizedStr, getName(), headword );

  optionalPartNom = 0;

  string html = processNodeChildren( dom.root );

  return html;
}

string DslDictionary::processNodeChildren( const ArticleDom::Node & node )
{
  string result;

  for ( const auto & i : node ) {
    result += nodeToHtml( i );
  }

  return result;
}

string DslDictionary::getNodeLink( const ArticleDom::Node & node )
{
  string link;
  if ( !node.tagAttrs.isEmpty() ) {
    QString attrs = node.tagAttrs;
    int n         = attrs.indexOf( "target=\"" );
    if ( n >= 0 ) {
      int n_end      = attrs.indexOf( '\"', n + 8 );
      QString target = attrs.mid( n + 8, n_end > n + 8 ? n_end - ( n + 8 ) : -1 );
      link           = Html::escape( Filetype::simplifyString( string( target.toUtf8().data() ), false ) );
    }
  }
  if ( link.empty() ) {
    link = Html::escape( Filetype::simplifyString( Text::toUtf8( node.renderAsText() ), false ) );
  }

  return link;
}

string DslDictionary::nodeToHtml( const ArticleDom::Node & node )
{
  string result;

  if ( !node.isTag ) {
    result = Html::escape( Text::toUtf8( node.text ) );

    string::size_type n;

    while ( ( n = result.find( '\r' ) ) != string::npos ) {
      result.erase( n, 1 );
    }

    while ( ( n = result.find( '\n' ) ) != string::npos ) {
      result.replace( n, 1, "<p></p>" );
    }

    return result;
  }

  if ( node.tagName == u"b" ) {
    result += "<b class=\"dsl_b\">" + processNodeChildren( node ) + "</b>";
  }
  else if ( node.tagName == u"i" ) {
    result += "<i class=\"dsl_i\">" + processNodeChildren( node ) + "</i>";
  }
  else if ( node.tagName == u"u" ) {
    string nodeText = processNodeChildren( node );

    if ( nodeText.size() && isDslWs( nodeText[ 0 ] ) ) {
      result.push_back( ' ' );
    }

    result += "<span class=\"dsl_u\">" + nodeText + "</span>";
  }
  else if ( node.tagName == u"c" ) {
    if ( node.tagAttrs.isEmpty() ) {
      result += "<span class=\"c_default_color\">" + processNodeChildren( node ) + "</span>";
    }
    else {
      result += "<font color=\"" + Html::escape( Text::toUtf8( node.tagAttrs ) ) + "\">" + processNodeChildren( node )
        + "</font>";
    }
  }
  else if ( node.tagName == u"*" ) {
    string id = "O" + getId().substr( 0, 7 ) + "_" + QString::number( articleNom ).toStdString() + "_opt_"
      + QString::number( optionalPartNom++ ).toStdString();
    result += R"(<span class="dsl_opt" id=")" + id + "\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"m" ) {
    result += "<div class=\"dsl_m\">" + processNodeChildren( node ) + "</div>";
  }
  else if ( node.tagName.size() == 2 && node.tagName[ 0 ] == u'm' && node.tagName[ 1 ] >= u'0'
            && node.tagName[ 1 ] <= u'9' ) {
    result += "<div class=\"dsl_" + Text::toUtf8( node.tagName ) + "\">" + processNodeChildren( node ) + "</div>";
  }
  else if ( node.tagName == u"trn" ) {
    result += "<span class=\"dsl_trn\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"ex" ) {
    result += "<span class=\"dsl_ex\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"com" ) {
    result += "<span class=\"dsl_com\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"s" || node.tagName == u"video" ) {
    string filename = Filetype::simplifyString( Text::toUtf8( node.renderAsText() ), false );
    string n        = resourceDir1 + filename;

    if ( Filetype::isNameOfSound( filename ) ) {
      QUrl url;
      url.setScheme( "gdau" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );
      if ( idxHeader.hasSoundDictionaryName ) {
        Utils::Url::setFragment( url, QString::fromUtf8( preferredSoundDictionary.c_str() ) );
      }

      string ref = string( "\"" ) + url.toEncoded().data() + "\"";

      result += addAudioLink( url.toEncoded(), getId() );

      result += "<span class=\"dsl_s_wav\"><a href=" + ref
        + R"(><img src="qrc:///icons/playsound.svg" border="0" align="absmiddle" alt="Play"/></a></span>)";
    }
    else if ( Filetype::isNameOfPicture( filename ) ) {
      QUrl url;
      url.setScheme( "bres" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      string maxWidthStyle = " style=\"max-width:100%;\" ";

      result += string( "<img src=\"" ) + url.toEncoded().data() + "\" " + maxWidthStyle + " alt=\""
        + Html::escape( filename ) + "\"/>";
    }
    else if ( Filetype::isNameOfVideo( filename ) ) {
      QUrl url;
      url.setScheme( "gdvideo" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      result += string( R"(<a class="dsl_s dsl_video" href=")" ) + url.toEncoded().data() + "\">"
        + "<span class=\"img\"></span>" + "<span class=\"filename\">" + processNodeChildren( node ) + "</span>"
        + "</a>";
    }
    else {

      QUrl url;
      url.setScheme( "bres" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      result +=
        string( R"(<a class="dsl_s" href=")" ) + url.toEncoded().data() + "\">" + processNodeChildren( node ) + "</a>";
    }
  }
  else if ( node.tagName == u"url" ) {
    string link = getNodeLink( node );
    if ( QUrl::fromEncoded( link.c_str() ).scheme().isEmpty() ) {
      link = "http://" + link;
    }

    QUrl url( QString::fromUtf8( link.c_str() ) );
    if ( url.isLocalFile() && url.host().isEmpty() ) {
      QString name = QFileInfo( getMainFilename() ).absolutePath();
      name += url.toLocalFile();
      QFileInfo info( name );
      if ( info.isFile() ) {
        name = info.canonicalFilePath();
        url.setPath( Utils::Url::ensureLeadingSlash( QUrl::fromLocalFile( name ).path() ) );
        link = string( url.toEncoded().data() );
      }
    }

    result += R"(<a class="dsl_url" href=")" + link + "\">" + processNodeChildren( node ) + "</a>";
  }
  else if ( node.tagName == u"!trs" ) {
    result += "<span class=\"dsl_trs\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"p" ) {
    result += "<span class=\"dsl_p\"";

    string val = Text::toUtf8( node.renderAsText() );

    auto i = abrv.find( val );

    if ( i != abrv.end() ) {
      string title = i->second;

      result += " title=\"" + Html::escape( title ) + "\"";
    }

    result += ">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"'" ) {
    string data = processNodeChildren( node );
    result += R"(<span class="dsl_stress"><span class="dsl_stress_without_accent">)" + data + "</span>"
      + "<span class=\"dsl_stress_with_accent\">" + data + Text::toUtf8( QString( QChar( 0x301 ) ) ) + "</span></span>";
  }
  else if ( node.tagName == u"lang" ) {
    result += "<span class=\"dsl_lang\"";
    if ( !node.tagAttrs.isEmpty() ) {
      string langcode;
      QString attr = node.tagAttrs;
      int n        = attr.indexOf( "id=" );
      if ( n >= 0 ) {
        int id = attr.mid( n + 3 ).toInt();
        if ( id ) {
          langcode = findCodeForDslId( id );
        }
      }
      else {
        n = attr.indexOf( "name=\"" );
        if ( n >= 0 ) {
          int n2 = attr.indexOf( '\"', n + 6 );
          if ( n2 > 0 ) {
            quint32 id = dslLanguageToId( attr.mid( n + 6, n2 - n - 6 ) );
            langcode   = LangCoder::intToCode2( id ).toStdString();
          }
        }
      }
      if ( !langcode.empty() ) {
        result += " lang=\"" + langcode + "\"";
      }
    }
    result += ">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"ref" ) {
    QUrl url;

    url.setScheme( "gdlookup" );
    url.setHost( "localhost" );
    auto nodeStr = Text::toQString( getNodeLink( node ) );

    normalizeHeadword( nodeStr );
    url.setPath( Utils::Url::ensureLeadingSlash( nodeStr ) );
    if ( !node.tagAttrs.isEmpty() ) {
      QString attr = node.tagAttrs;
      attr.remove( '\"' );
      int n = attr.indexOf( '=' );
      if ( n > 0 ) {
        QList< std::pair< QString, QString > > query;
        query.append( std::pair< QString, QString >( attr.left( n ), attr.mid( n + 1 ) ) );
        Utils::Url::setQueryItems( url, query );
      }
    }

    result +=
      string( R"(<a class="dsl_ref" href=")" ) + url.toEncoded().data() + "\">" + processNodeChildren( node ) + "</a>";
  }
  else if ( node.tagName == u"@" ) {

    QUrl url;

    url.setScheme( "gdlookup" );
    url.setHost( "localhost" );
    QString nodeStr = node.renderAsText();
    normalizeHeadword( nodeStr );
    url.setPath( Utils::Url::ensureLeadingSlash( nodeStr ) );

    result +=
      string( R"(<a class="dsl_ref" href=")" ) + url.toEncoded().data() + "\">" + processNodeChildren( node ) + "</a>";
  }
  else if ( node.tagName == u"sub" ) {
    result += "<sub>" + processNodeChildren( node ) + "</sub>";
  }
  else if ( node.tagName == u"sup" ) {
    result += "<sup>" + processNodeChildren( node ) + "</sup>";
  }
  else if ( node.tagName == u"t" ) {
    result += "<span class=\"dsl_t\">" + processNodeChildren( node ) + "</span>";
  }
  else if ( node.tagName == u"br" ) {
    result += "<br />";
  }
  else {
    qWarning( R"(DSL: Unknown tag "%s" with attributes "%s" found in "%s", article "%s".)",
              node.tagName.toUtf8().data(),
              node.tagAttrs.toUtf8().data(),
              getName().c_str(),
              currentHeadword.toUtf8().data() );

    result += "<span class=\"dsl_unknown\">[" + string( node.tagName.toUtf8().data() );
    if ( !node.tagAttrs.isEmpty() ) {
      result += " " + string( node.tagAttrs.toUtf8().data() );
    }
    result += "]" + processNodeChildren( node ) + "</span>";
  }

  return result;
}

const QString & DslDictionary::getDescription()
{
  if ( !dictionaryDescription.isEmpty() ) {
    return dictionaryDescription;
  }

  QString none = QStringLiteral( "NONE" );

  dictionaryDescription = none;

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  if ( fileName.endsWith( ".dsl.dz", Qt::CaseInsensitive ) ) {
    fileName.chop( 6 );
  }
  else {
    fileName.chop( 3 );
  }

  fileName += "ann";
  QFileInfo info( fileName );

  if ( info.exists() ) {
    QFile annFile( fileName );
    if ( !annFile.open( QFile::ReadOnly | QFile::Text ) ) {
      return dictionaryDescription;
    }

    QTextStream annStream( &annFile );
    QString data, str;

    str = annStream.readLine();

    if ( str.left( 10 ).compare( "#LANGUAGE " ) != 0 ) {
      annStream.seek( 0 );
      dictionaryDescription = annStream.readAll();
    }
    else {

      qint32 gdLang, annLang;
      QString langStr;
      gdLang = LangCoder::code2toInt( QLocale::system().name().left( 2 ).toLatin1().data() );
      for ( ;; ) {
        data.clear();
        langStr = str.mid( 10 ).replace( '\"', ' ' ).trimmed();
        annLang = LangCoder::findIdForLanguage( langStr );
        do {
          str = annStream.readLine();
          if ( str.left( 10 ).compare( "#LANGUAGE " ) == 0 ) {
            break;
          }
          if ( !str.endsWith( '\n' ) ) {
            str.append( '\n' );
          }
          data += str;
        } while ( !annStream.atEnd() );
        if ( dictionaryDescription.compare( "NONE " ) == 0 || langStr.compare( "English", Qt::CaseInsensitive ) == 0
             || gdLang == annLang ) {
          dictionaryDescription = data.trimmed();
        }
        if ( gdLang == annLang || annStream.atEnd() ) {
          break;
        }
      }
    }
  }
  if ( dictionaryDescription != none ) {
    dictionaryDescription.replace( QRegularExpression( R"(\R)" ), R"(<br>)" );
  }
  return dictionaryDescription;
}

QString DslDictionary::getMainFilename()
{
  return getDictionaryFilenames()[ 0 ].c_str();
}

void DslDictionary::makeFTSIndex( QAtomicInt & isCancelled )
{
  if ( !( Dictionary::needToRebuildIndex( getDictionaryFilenames(), ftsIdxName )
          || FtsHelpers::ftsIndexIsOldOrBad( this ) ) ) {
    FTS_index_completed.ref();
  }


  if ( haveFTSIndex() ) {
    return;
  }

  if ( !ensureInitDone().empty() ) {
    return;
  }


  qDebug( "Dsl: Building the full-text index for dictionary: %s", getName().c_str() );

  try {
    FtsHelpers::makeFTSIndex( this, isCancelled );
    FTS_index_completed.ref();
  }
  catch ( std::exception & ex ) {
    qWarning( "DSL: Failed building full-text search index for \"%s\", reason: %s", getName().c_str(), ex.what() );
    QFile::remove( ftsIdxName.c_str() );
  }
}

void DslDictionary::getArticleText( uint32_t articleAddress, QString & headword, QString & text )
{
  headword.clear();
  text.clear();

  vector< char > chunk;

  char * articleProps;
  QString articleData;

  {
    QMutexLocker _( &idxMutex );
    articleProps = chunks->getBlock( articleAddress, chunk );
  }

  uint32_t articleOffset, articleSize;

  memcpy( &articleOffset, articleProps, sizeof( articleOffset ) );
  memcpy( &articleSize, articleProps + sizeof( articleOffset ), sizeof( articleSize ) );

  char * articleBody;

  {
    QMutexLocker _( &dzMutex );
    articleBody = dict_data_read_( dz, articleOffset, articleSize, 0, 0 );
  }

  if ( !articleBody ) {
    return;
  }
  else {
    try {
      articleData = Iconv::toQString( getEncodingNameFor( static_cast< Encoding >( idxHeader.dslEncoding ) ),
                                      articleBody,
                                      articleSize );
      free( articleBody );

      bool b = false;
      stripComments( articleData, b );
    }
    catch ( ... ) {
      free( articleBody );
      return;
    }
  }

  qsizetype pos = 0;
  QString articleHeadword, tildeValue;

  bool insidedCard = isDslWs( articleData.at( 0 ) );

  for ( ;; ) {
    qsizetype begin = pos;

    pos = dslFindFirstOf( articleData, u"\n\r", begin );
    if ( pos < 0 ) {
      pos = articleData.size();
    }

    if ( articleHeadword.isEmpty() ) {
      articleHeadword = articleData.mid( begin, pos - begin );

      if ( insidedCard && !articleHeadword.isEmpty() && isDslWs( articleHeadword[ 0 ] ) ) {
        qsizetype hpos = articleHeadword.indexOf( u'@' );
        if ( hpos >= 0 ) {
          QString head = Folding::trimWhitespace( articleHeadword.mid( hpos + 1 ) );
          hpos         = head.indexOf( u'~' );
          while ( hpos >= 0 ) {
            if ( hpos == 0 || head[ hpos ] != u'\\' ) {
              break;
            }
            hpos = head.indexOf( u'~', hpos + 1 );
          }
          if ( hpos < 0 ) {
            articleHeadword = head;
          }
          else {
            articleHeadword.clear();
          }
        }
      }

      if ( !articleHeadword.isEmpty() ) {
        list< QString > lst;

        tildeValue = articleHeadword;

        processUnsortedParts( articleHeadword, true );
        expandOptionalParts( articleHeadword, &lst );

        if ( lst.size() ) {
          articleHeadword = lst.front();
        }
      }
    }

    if ( pos == articleData.size() ) {
      break;
    }

    if ( articleData[ pos ] == u'\r' ) {
      ++pos;
    }

    if ( pos < articleData.size() ) {
      if ( articleData[ pos ] == u'\n' ) {
        ++pos;
      }
    }

    if ( pos >= articleData.size() ) {
      break;
    }
    if ( isDslWs( articleData[ pos ] ) ) {
      if ( insidedCard ) {
        qsizetype hpos = dslFindFirstOf( articleData, u"\n\r", pos );
        if ( hpos < 0 ) {
          hpos = articleData.size();
        }

        QString str = articleData.mid( pos, hpos - pos );

        hpos = str.indexOf( u'@' );
        if ( hpos < 0 || str[ hpos - 1 ] == u'\\' || !isAtSignFirst( str ) ) {
          break;
        }
      }
      else {
        break;
      }
    }
  }

  if ( !articleHeadword.isEmpty() ) {
    unescapeDsl( articleHeadword );
    normalizeHeadword( articleHeadword );
    headword = articleHeadword;
  }

  QString articleText;

  if ( pos != articleData.size() ) {
    articleText = articleData.mid( pos );
  }
  else {
    articleText.clear();
  }

  if ( !tildeValue.isEmpty() ) {
    list< QString > lst;

    processUnsortedParts( tildeValue, false );
    expandOptionalParts( tildeValue, &lst );

    if ( lst.size() ) {
      expandTildes( articleText, lst.front() );
    }
  }

  if ( !articleText.isEmpty() ) {
    text = articleText.normalized( QString::NormalizationForm_C );

    articleText.clear();

    const int stripTagsNumber                      = 5;
    static QString stripTags[ stripTagsNumber ]    = { "s", "url", "!trs", "video", "preview" };
    static QString stripEndTags[ stripTagsNumber ] = { "[/s]", "[/url]", "[/!trs]", "[/video]", "[/preview]" };

    int pos = 0;
    while ( pos >= 0 ) {
      pos = text.indexOf( '[', pos, Qt::CaseInsensitive );
      if ( pos >= 0 ) {
        if ( ( pos > 0 && text[ pos - 1 ] == '\\' && ( pos < 2 || text[ pos - 2 ] != '\\' ) )
             || ( pos > text.size() - 2 || text[ pos + 1 ] == '/' ) ) {
          pos += 1;
          continue;
        }

        int pos2 = text.indexOf( ']', pos + 1, Qt::CaseInsensitive );
        if ( pos2 < 0 ) {
          break;
        }

        QString tag = text.mid( pos + 1, pos2 - pos - 1 );

        int n;
        for ( n = 0; n < stripTagsNumber; n++ ) {
          if ( tag.compare( stripTags[ n ], Qt::CaseInsensitive ) == 0 ) {
            pos2 = text.indexOf( stripEndTags[ n ], pos + stripTags[ n ].size() + 2, Qt::CaseInsensitive );
            text.replace( pos, pos2 > 0 ? pos2 - pos + stripEndTags[ n ].length() : text.length() - pos, " " );
            break;
          }
        }

        if ( n >= stripTagsNumber ) {
          pos += 1;
        }
      }
    }

    text.replace( QRegularExpression( R"(\[(|/)(p|trn|ex|com|\*|t|br|m[0-9]?)\])" ), " " );
    text.replace( QRegularExpression( R"(\[(|/)lang(\s[^\]]*)?\])" ), " " );
    text.remove( QRegularExpression( R"(\[[^\\\[\]]+\])" ) );

    text.remove( QString::fromLatin1( "<<" ) );
    text.remove( QString::fromLatin1( ">>" ) );

    bool haveInsidedCards = false;
    pos                   = 0;
    while ( pos >= 0 ) {
      pos = text.indexOf( "@", pos );
      if ( pos > 0 && text.at( pos - 1 ) != '\\' ) {
        haveInsidedCards = true;
        break;
      }

      if ( pos >= 0 ) {
        pos += 1;
      }
    }

    if ( haveInsidedCards ) {
      ArticleDom dom( text, getName(), articleHeadword );
      text = dom.root.renderAsText( true );
    }
    else {
      pos = 0;
      while ( pos >= 0 ) {
        pos = text.indexOf( '\\', pos );
        if ( pos >= 0 ) {
          if ( text[ pos + 1 ] == '\\' ) {
            pos += 1;
          }

          text.remove( pos, 1 );
        }
      }
    }
  }
}

class DslArticleRequest: public Dictionary::DataRequest
{
  QString word;
  vector< QString > alts;
  DslDictionary & dict;
  bool ignoreDiacritics;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  DslArticleRequest( const QString & word_,
                     const vector< QString > & alts_,
                     DslDictionary & dict_,
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

  ~DslArticleRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void DslArticleRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  if ( dict.ensureInitDone().size() ) {
    setErrorString( QString::fromUtf8( dict.ensureInitDone().c_str() ) );
    finish();
    return;
  }

  vector< WordArticleLink > chain = dict.findArticles( word, ignoreDiacritics );

  for ( const auto & alt : alts ) {

    vector< WordArticleLink > altChain = dict.findArticles( alt, ignoreDiacritics );

    chain.insert( chain.end(), altChain.begin(), altChain.end() );
  }

  set< pair< uint32_t, unsigned > > articlesIncluded;

  QString wordCaseFolded = Folding::applySimpleCaseOnly( word );

  for ( auto & x : chain ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      finish();
      return;
    }

    QString tildeValue;
    QString displayedHeadword;
    QString articleBody;
    unsigned headwordIndex;

    string articleText, articleAfter;

    try {
      dict.loadArticle( x.articleOffset,
                        wordCaseFolded,
                        ignoreDiacritics,
                        tildeValue,
                        displayedHeadword,
                        headwordIndex,
                        articleBody );

      if ( !articlesIncluded.insert( std::make_pair( x.articleOffset, headwordIndex ) ).second ) {
        continue;
      }

      dict.articleNom += 1;

      if ( displayedHeadword.isEmpty() || isDslWs( displayedHeadword[ 0 ] ) ) {
        displayedHeadword = word;
      }

      articleText += "<div class=\"dsl_article\">";
      articleText += "<div class=\"dsl_headwords\"";
      if ( dict.isFromLanguageRTL() ) {
        articleText += " dir=\"rtl\"";
      }
      articleText += "><p>";

      if ( displayedHeadword.size() == 1 && displayedHeadword[ 0 ] == '<' ) {
        articleText += "<";
      }
      else {
        articleText += dict.dslToHtml( displayedHeadword, displayedHeadword );
      }

      articleAfter += "</p></div>";

      expandTildes( articleBody, tildeValue );

      articleAfter += "<div class=\"dsl_definition\"";
      if ( dict.isToLanguageRTL() ) {
        articleAfter += " dir=\"rtl\"";
      }
      articleAfter += ">";

      articleAfter += dict.dslToHtml( articleBody, displayedHeadword );
      articleAfter += "</div>";
      articleAfter += "</div>";

      if ( dict.hasHiddenZones() ) {
        string prefix = "O" + dict.getId().substr( 0, 7 ) + "_" + QString::number( dict.articleNom ).toStdString();
        string id1    = prefix + "_expand";
        string id2    = "gd-" + dict.getId();
        string button = R"( <img src="qrc:///icons/expand_opt.svg" class="hidden_expand_opt" id=")" + id1
          + "\" onclick=\"gdExpandOptPart('" + id1 + "','" + id2 + "')\" alt=\"[+]\"/>";
        if ( articleText.compare( articleText.size() - 4, 4, "</p>" ) == 0 ) {
          articleText.insert( articleText.size() - 4, " " + button );
        }
        else {
          articleText += button;
        }
      }

      articleText += articleAfter;
    }
    catch ( std::exception & ex ) {
      qWarning( "DSL: Failed loading article from \"%s\", reason: %s", dict.getName().c_str(), ex.what() );
      articleText =
        string( "<span class=\"dsl_article\">" ) + QObject::tr( "Article loading error" ).toStdString() + "</span>";
    }

    appendString( articleText );

    hasAnyData = true;
  }

  finish();
}

sptr< Dictionary::DataRequest > DslDictionary::getArticle( const QString & word,
                                                           const vector< QString > & alts,
                                                           const QString &,
                                                           bool ignoreDiacritics )

{
  return std::make_shared< DslArticleRequest >( word, alts, *this, ignoreDiacritics );
}

class DslResourceRequest: public Dictionary::DataRequest
{
  DslDictionary & dict;

  string resourceName;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  DslResourceRequest( DslDictionary & dict_, const string & resourceName_ ):
    dict( dict_ ),
    resourceName( resourceName_ )
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

  ~DslResourceRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

void DslResourceRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  if ( dict.ensureInitDone().size() ) {
    setErrorString( QString::fromUtf8( dict.ensureInitDone().c_str() ) );
    finish();
    return;
  }

  string n = dict.getContainingFolder().toStdString() + Utils::Fs::separator() + resourceName;

  auto fp = Utils::Fs::findFirstExistingFile(
    { n, dict.getResourceDir1() + resourceName, dict.getResourceDir2() + resourceName } );
  qDebug( "found dsl resource name is %s", fp.c_str() );
  try {
    QMutexLocker _( &dataMutex );

    if ( !fp.empty() ) {
      File::loadFromFile( fp, data );
    }
    else if ( dict.resourceZip.isOpen() ) {
      if ( !dict.resourceZip.loadFile( Text::toQString( resourceName ), data ) ) {
        throw std::runtime_error( "Failed to load file from resource zip" );
      }
    }
    else {
      QString containingFolder = dict.getContainingFolder();
      if ( !containingFolder.isEmpty() ) {
        QDir dir( containingFolder );
        QStringList lsaFiles = dir.entryList( QStringList() << "*.lsa", QDir::Files );

        if ( !lsaFiles.isEmpty() ) {
          QString lsaFilePath = dir.absoluteFilePath( lsaFiles.first() );
          QString lsaDictId   = GlobalBroadcaster::instance()->getLsaIdFromPath( lsaFilePath );

          if ( !lsaDictId.isEmpty() ) {
            sptr< Dictionary::Class > lsaDict = GlobalBroadcaster::instance()->getDictionaryById( lsaDictId );

            if ( lsaDict ) {
              sptr< Dictionary::DataRequest > lsaReq = lsaDict->getResource( resourceName );

              if ( lsaReq->isFinished() && lsaReq->dataSize() > 0 ) {
                data       = lsaReq->getFullData();
                hasAnyData = true;
              }
            }
          }
        }
      }
      if ( !hasAnyData ) {
        throw std::runtime_error( "Resource not found." );
      }
    }

    if ( Filetype::isNameOfTiff( resourceName ) ) {
      GdTiff::tiff2img( data );
    }

    hasAnyData = true;
  }
  catch ( std::exception & ex ) {
    qWarning() << "DSL: Failed loading resource" << QString::fromStdString( resourceName ) << "for"
               << QString::fromStdString( dict.getName() ) << ", reason:" << ex.what();

  }

  finish();
}

sptr< Dictionary::DataRequest > DslDictionary::getResource( const string & name )

{
  return std::make_shared< DslResourceRequest >( *this, name );
}


sptr< Dictionary::DataRequest > DslDictionary::getSearchResults( const QString & searchString,
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

}

vector< sptr< Dictionary::Class > > makeDictionaries( const vector< string > & fileNames,
                                                      const string & indicesDir,
                                                      Dictionary::Initializing & initializing,
                                                      unsigned int maxHeadwordSize )

{
  vector< sptr< Dictionary::Class > > dictionaries;

  for ( const auto & fileName : fileNames ) {

    bool uncompressedDsl = Utils::endsWithIgnoreCase( fileName, ".dsl" );
    if ( !uncompressedDsl && !Utils::endsWithIgnoreCase( fileName, ".dsl.dz" ) ) {
      continue;
    }

    if ( size_t extSize = ( uncompressedDsl ? 4 : 7 ); ( fileName.size() >= ( 5 + extSize ) )
         && ( QByteArrayView( fileName ).chopped( extSize ).last( 5 ).compare( "_abrv", Qt::CaseInsensitive ) == 0 ) ) {
      continue;
    }

    unsigned atLine = 0;

    try {
      vector< string > dictFiles( 1, fileName );

      string baseName = ( fileName[ fileName.size() - 4 ] == '.' ) ? string( fileName, 0, fileName.size() - 4 ) :
                                                                     string( fileName, 0, fileName.size() - 7 );

      string abrvFileName = Utils::Fs::findFirstExistingFile( { baseName + "_abrv.dsl",
                                                                baseName + "_abrv.dsl.dz",
                                                                baseName + "_ABRV.DSL",
                                                                baseName + "_ABRV.DSL.DZ",
                                                                baseName + "_ABRV.DSL.dz" } );

      if ( abrvFileName.size() ) {
        dictFiles.push_back( abrvFileName );
      }

      initializing.loadingDictionary( fileName );

      string dictId = Dictionary::makeDictionaryId( dictFiles );

      string zipFileName = Utils::Fs::findFirstExistingFile( { baseName + ".dsl.files.zip",
                                                               baseName + ".dsl.dz.files.zip",
                                                               baseName + ".DSL.FILES.ZIP",
                                                               baseName + ".DSL.DZ.FILES.ZIP" } );

      if ( !zipFileName.empty() ) {
        dictFiles.push_back( zipFileName );
      }

      string indexFile = indicesDir + dictId;

      if ( Dictionary::needToRebuildIndex( dictFiles, indexFile )
           || indexIsOldOrBad( indexFile, zipFileName.size() ) ) {
        DslScanner scanner( fileName );

        try {

          if ( scanner.getDictionaryName() == u"Abbrev" ) {
            continue;
          }

          initializing.indexingDictionary( Text::toUtf8( scanner.getDictionaryName() ) );

          qDebug( "Dsl: Building the index for dictionary: %s", scanner.getDictionaryName().toUtf8().data() );

          File::Index idx( indexFile, QIODevice::WriteOnly );

          IdxHeader idxHeader;

          memset( &idxHeader, 0, sizeof( idxHeader ) );

          idx.write( idxHeader );

          string dictionaryName = Text::toUtf8( scanner.getDictionaryName() );

          idx.write( (uint32_t)dictionaryName.size() );
          idx.write( dictionaryName.data(), dictionaryName.size() );

          string soundDictName = Text::toUtf8( scanner.getSoundDictionaryName() );
          if ( !soundDictName.empty() ) {
            idxHeader.hasSoundDictionaryName = 1;
            idx.write( (uint32_t)soundDictName.size() );
            idx.write( soundDictName.data(), soundDictName.size() );
          }

          idxHeader.dslEncoding = static_cast< uint32_t >( scanner.getEncoding() );

          IndexedWords indexedWords;

          ChunkedStorage::Writer chunks( idx );

          if ( abrvFileName.size() ) {
            try {
              DslScanner abrvScanner( abrvFileName );

              map< string, string > abrv;

              QString curString;
              size_t curOffset;

              for ( ;; ) {
                if ( !abrvScanner.readNextLineWithoutComments( curString, curOffset, true ) ) {
                  break;
                }
                if ( curString.isEmpty() || isDslWs( curString[ 0 ] ) ) {
                  continue;
                }

                list< QString > keys;

                bool eof = false;

                for ( ;; ) {
                  processUnsortedParts( curString, true );

                  if ( keys.size() ) {
                    expandTildes( curString, keys.front() );
                  }

                  expandOptionalParts( curString, &keys );

                  if ( !abrvScanner.readNextLineWithoutComments( curString, curOffset ) || curString.isEmpty() ) {
                    qWarning( "Premature end of file %s", abrvFileName.c_str() );
                    eof = true;
                    break;
                  }

                  if ( isDslWs( curString[ 0 ] ) ) {
                    break;
                  }
                }

                if ( eof ) {
                  break;
                }

                curString.remove( 0, dslFindFirstNotOf( curString, u" \t" ) );

                if ( keys.size() ) {
                  expandTildes( curString, keys.front() );
                }

                string value = Text::toUtf8( ArticleDom( curString ).root.renderAsText() );

                for ( auto & key : keys ) {
                  unescapeDsl( key );
                  normalizeHeadword( key );

                  abrv[ Text::toUtf8( Folding::trimWhitespace( key ) ) ] = value;
                }
              }

              idxHeader.hasAbrv     = 1;
              idxHeader.abrvAddress = chunks.startNewBlock();

              uint32_t sz = abrv.size();

              chunks.addToBlock( &sz, sizeof( uint32_t ) );

              for ( const auto & i : abrv ) {

                sz = i.first.size();
                chunks.addToBlock( &sz, sizeof( uint32_t ) );
                chunks.addToBlock( i.first.data(), sz );
                sz = i.second.size();
                chunks.addToBlock( &sz, sizeof( uint32_t ) );
                chunks.addToBlock( i.second.data(), sz );
              }
            }
            catch ( std::exception & e ) {
              qWarning( "Error reading abrv file \"%s\", error: %s. Skipping it.", abrvFileName.c_str(), e.what() );
            }
          }

          bool hasString = false;
          QString curString;
          size_t curOffset;

          uint32_t articleCount = 0, wordCount = 0;

          for ( ;; ) {

            if ( !hasString && !scanner.readNextLineWithoutComments( curString, curOffset, true ) ) {
              break;
            }

            hasString = false;

            if ( curString.isEmpty() ) {
              continue;
            }

            if ( isDslWs( curString[ 0 ] ) ) {
              for ( qsizetype x = 1; x < curString.size(); ++x ) {
                if ( !isDslWs( curString[ x ] ) ) {
                  qWarning( "Garbage string in %s at offset 0x%lX", fileName.c_str(), curOffset );
                  break;
                }
              }
              continue;
            }

            list< QString > allEntryWords;

            processUnsortedParts( curString, true );
            expandOptionalParts( curString, &allEntryWords );

            uint32_t articleOffset = curOffset;

            for ( ;; ) {
              if ( !( hasString = scanner.readNextLineWithoutComments( curString, curOffset ) ) ) {
                qWarning( "Premature end of file %s", fileName.c_str() );
                break;
              }

              if ( curString.isEmpty() ) {
                continue;
              }

              if ( isDslWs( curString[ 0 ] ) ) {
                break;
              }

              qDebug() << "dsl Alt headword" << curString;

              processUnsortedParts( curString, true );
              expandTildes( curString, allEntryWords.front() );
              expandOptionalParts( curString, &allEntryWords );
            }

            if ( !hasString ) {
              break;
            }

            uint32_t descOffset = chunks.startNewBlock();

            chunks.addToBlock( &articleOffset, sizeof( articleOffset ) );

            for ( auto & allEntryWord : allEntryWords ) {
              unescapeDsl( allEntryWord );
              normalizeHeadword( allEntryWord );
              indexedWords.addWord( allEntryWord, descOffset, maxHeadwordSize );
            }

            ++articleCount;
            wordCount += allEntryWords.size();

            int insideInsided = 0;
            QString headword;
            QList< InsidedCard > insidedCards;
            uint32_t offset = curOffset;
            QList< QString > insidedHeadwords;
            unsigned linesInsideCard = 0;
            int dogLine              = 0;
            bool wasEmptyLine        = false;
            int headwordLine         = scanner.getLinesRead() - 2;
            bool noSignificantLines  = Folding::applyWhitespaceOnly( curString ).isEmpty();
            bool haveLine            = !noSignificantLines;

            for ( ;; ) {
              hasString = haveLine ? true : scanner.readNextLineWithoutComments( curString, curOffset );
              haveLine  = false;

              if ( !hasString || ( curString.size() && !isDslWs( curString[ 0 ] ) ) ) {
                if ( insideInsided ) {
                  qWarning( "Unclosed tag '@' at line %i", dogLine );
                  insidedCards.append( InsidedCard( offset, curOffset - offset, insidedHeadwords ) );
                }
                if ( noSignificantLines ) {
                  qWarning( "Orphan headword at line %i", headwordLine );
                }

                break;
              }

              if ( curString.isEmpty() ) {
                wasEmptyLine = true;
                continue;
              }
              else {
                if ( wasEmptyLine && !Folding::applyWhitespaceOnly( curString ).isEmpty() ) {
                  qWarning( "Orphan string at line %i", scanner.getLinesRead() - 1 );
                }
              }

              if ( noSignificantLines ) {
                noSignificantLines = Folding::applyWhitespaceOnly( curString ).isEmpty();
              }

              qsizetype n = curString.indexOf( u'@' );
              if ( n < 0 || curString[ n - 1 ] == u'\\' ) {
                if ( insideInsided ) {
                  linesInsideCard++;
                }

                continue;
              }
              else {
                if ( !isAtSignFirst( curString ) ) {
                  qWarning( "Unescaped '@' symbol at line %i", scanner.getLinesRead() - 1 );

                  if ( insideInsided ) {
                    linesInsideCard++;
                  }

                  continue;
                }
              }

              dogLine = scanner.getLinesRead() - 1;

              if ( insideInsided ) {
                if ( linesInsideCard ) {
                  insidedCards.append( InsidedCard( offset, curOffset - offset, insidedHeadwords ) );

                  insidedHeadwords.clear();
                  linesInsideCard = 0;
                  offset          = curOffset;
                }
              }
              else {
                offset          = curOffset;
                linesInsideCard = 0;
              }

              headword = Folding::trimWhitespace( curString.mid( n + 1 ) );

              if ( !headword.isEmpty() ) {
                processUnsortedParts( headword, true );
                expandTildes( headword, allEntryWords.front() );
                insidedHeadwords.append( headword );
                insideInsided = true;
              }
              else {
                insideInsided = false;
              }
            }

            uint32_t articleSize = ( curOffset - articleOffset );

            chunks.addToBlock( &articleSize, sizeof( articleSize ) );

            for ( auto & insidedCard : insidedCards ) {
              uint32_t desc_offset = chunks.startNewBlock();
              chunks.addToBlock( &insidedCard.offset, sizeof( insidedCard.offset ) );
              chunks.addToBlock( &insidedCard.size, sizeof( insidedCard.size ) );

              for ( auto & hw : insidedCard.headwords ) {
                allEntryWords.clear();
                expandOptionalParts( hw, &allEntryWords );

                for ( auto & allEntryWord : allEntryWords ) {
                  unescapeDsl( allEntryWord );
                  normalizeHeadword( allEntryWord );
                  indexedWords.addWord( allEntryWord, desc_offset, maxHeadwordSize );
                }

                wordCount += allEntryWords.size();
              }
              ++articleCount;
            }

            if ( !hasString ) {
              break;
            }
          }

          idxHeader.chunksOffset = chunks.finish();

          IndexInfo idxInfo = BtreeIndexing::buildIndex( indexedWords, idx );

          idxHeader.indexBtreeMaxElements = idxInfo.btreeMaxElements;
          idxHeader.indexRootOffset       = idxInfo.rootOffset;

          indexedWords.clear();

          if ( zipFileName.size() ) {
            qDebug( "Indexing zip file" );

            idxHeader.hasZipFile = 1;

            IndexedWords zipFileNames;
            IndexedZip zipFile;
            if ( zipFile.openZipFile( QDir::fromNativeSeparators( zipFileName.c_str() ) ) ) {
              zipFile.indexFile( zipFileNames );
            }

            if ( !zipFileNames.empty() ) {

              IndexInfo idxInfo = BtreeIndexing::buildIndex( zipFileNames, idx );

              idxHeader.zipIndexBtreeMaxElements = idxInfo.btreeMaxElements;
              idxHeader.zipIndexRootOffset       = idxInfo.rootOffset;
            }
            else {
              idxHeader.zipIndexBtreeMaxElements = 0;
              idxHeader.zipIndexRootOffset       = 0;
            }
          }
          else {
            idxHeader.hasZipFile = 0;
          }

          idxHeader.signature         = Signature;
          idxHeader.formatVersion     = CurrentFormatVersion;
          idxHeader.zipSupportVersion = CurrentZipSupportVersion;

          idxHeader.articleCount = articleCount;
          idxHeader.wordCount    = wordCount;

          idxHeader.langFrom = dslLanguageToId( scanner.getLangFrom() );
          idxHeader.langTo   = dslLanguageToId( scanner.getLangTo() );

          idx.rewind();

          idx.write( &idxHeader, sizeof( idxHeader ) );
        }
        catch ( ... ) {
          atLine = scanner.getLinesRead();
          throw;
        }
      }

      dictionaries.push_back( std::make_shared< DslDictionary >( dictId, indexFile, dictFiles ) );
    }
    catch ( std::exception & e ) {
      qWarning( "DSL dictionary reading failed: %s:%u, error: %s", fileName.c_str(), atLine, e.what() );
    }
  }

  return dictionaries;
}

}
