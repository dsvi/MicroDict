/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "lsa.hh"
#include "dictfile.hh"
#include "iconv.hh"
#include "folding.hh"
#include "text.hh"
#include "btreeidx.hh"

#include "audiolink.hh"

#include <set>
#include <string>

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#include <QDir>
#include <QUrl>
#include <QFile>

#include "utils.hh"
#include "globalbroadcaster.hh"

namespace Lsa {

using std::string;
using std::map;
using std::multimap;
using std::set;
using BtreeIndexing::WordArticleLink;
using BtreeIndexing::IndexedWords;
using BtreeIndexing::IndexInfo;

namespace {

DEF_EX( exInvalidData, "Invalid data encountered", Dictionary::Ex )
DEF_EX( exFailedToOpenVorbisData, "Failed to open Vorbis data", Dictionary::Ex )
DEF_EX( exFailedToSeekInVorbisData, "Failed to seek in Vorbis data", Dictionary::Ex )
DEF_EX( exFailedToRetrieveVorbisInfo, "Failed to retrieve Vorbis info", Dictionary::Ex )

enum {
  Signature            = 0x5841534c,
  CurrentFormatVersion = 5
};

#pragma pack( push, 1 )

struct IdxHeader
{
  uint32_t signature;
  uint32_t formatVersion;
  uint32_t soundsCount;
  uint32_t vorbisOffset;
  uint32_t indexBtreeMaxElements;
  uint32_t indexRootOffset;
};
static_assert( alignof( IdxHeader ) == 1 );
#pragma pack( pop )

bool indexIsOldOrBad( string const & indexFile )
{
  File::Index idx( indexFile, QIODevice::ReadOnly );

  IdxHeader header;

  return idx.readRecords( &header, sizeof( header ), 1 ) != 1 || header.signature != Signature
    || header.formatVersion != CurrentFormatVersion;
}

string stripExtension( const string & str )
{
  if ( Utils::endsWithIgnoreCase( str, ".wav" ) ) {
    return string( str, 0, str.size() - 4 );
  }
  else {
    return str;
  }
}

struct Entry
{
  string name;

  uint32_t samplesLength;
  uint32_t samplesOffset;

public:

  Entry( File::Index & f );
};

Entry::Entry( File::Index & f )
{
  bool firstEntry = ( f.tell() == 13 );
  size_t read = 0;

  vector< uint16_t > filenameBuffer( 64 );

  for ( ;; ++read ) {
    if ( filenameBuffer.size() <= read ) {
      filenameBuffer.resize( read + 64 );
    }

    f.read( &filenameBuffer[ read ], 2 );

    if ( filenameBuffer[ read ] == 0xD ) {
      if ( f.read< uint16_t >() != 0xA ) {
        throw exInvalidData();
      }

      break;
    }
  }

  if ( auto x = f.read< uint8_t >() ) {
    if ( x != 0xFF ) {
      throw exInvalidData();
    }
  }
  else if ( f.read< uint8_t >() != 0xFF ) {
    throw exInvalidData();
  }


  if ( !firstEntry ) {
    samplesOffset = f.read< uint32_t >();

    if ( f.read< uint8_t >() != 0xFF ) {
      throw exInvalidData();
    }
  }
  else {
    samplesOffset = 0;
  }

  samplesLength = f.read< uint32_t >();

  name = Iconv::toUtf8( Text::utf16_le, &filenameBuffer.front(), read * sizeof( uint16_t ) );
}

class LsaDictionary: public BtreeIndexing::BtreeDictionary
{
  QMutex idxMutex;
  File::Index idx;
  IdxHeader idxHeader;

public:

  LsaDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles );

  string getName() noexcept override;

  unsigned long getArticleCount() noexcept override
  {
    return idxHeader.soundsCount;
  }

  unsigned long getWordCount() noexcept override
  {
    return getArticleCount();
  }

  sptr< Dictionary::DataRequest >
  getArticle( const QString &, const vector< QString > & alts, const QString &, bool ignoreDiacritics ) override;

  sptr< Dictionary::DataRequest > getResource( const string & name ) override;

protected:

  void loadIcon() noexcept override;
};

string LsaDictionary::getName() noexcept
{
  string result = Utils::Fs::basename( getDictionaryFilenames()[ 0 ] );

  result.erase( result.rfind( '.' ) );

  return result;
}

LsaDictionary::LsaDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles ):
  BtreeDictionary( id, dictionaryFiles ),
  idx( indexFile, QIODevice::ReadOnly ),
  idxHeader( idx.read< IdxHeader >() )
{

  openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );
}

sptr< Dictionary::DataRequest > LsaDictionary::getArticle( const QString & word,
                                                           const vector< QString > & alts,
                                                           const QString &,
                                                           bool ignoreDiacritics )

{
  vector< WordArticleLink > chain = findArticles( word, ignoreDiacritics );

  for ( const auto & alt : alts ) {

    vector< WordArticleLink > altChain = findArticles( alt, ignoreDiacritics );

    chain.insert( chain.end(), altChain.begin(), altChain.end() );
  }

  multimap< QString, string > mainArticles, alternateArticles;

  set< uint32_t > articlesIncluded;

  QString wordCaseFolded = Folding::applySimpleCaseOnly( word );
  if ( ignoreDiacritics ) {
    wordCaseFolded = Folding::applyDiacriticsOnly( wordCaseFolded );
  }

  for ( auto & x : chain ) {
    if ( articlesIncluded.find( x.articleOffset ) != articlesIncluded.end() ) {
      continue;
    }

    QString headwordStripped = Folding::applySimpleCaseOnly( x.word );
    if ( ignoreDiacritics ) {
      headwordStripped = Folding::applyDiacriticsOnly( headwordStripped );
    }

    multimap< QString, string > & mapToUse = ( wordCaseFolded == headwordStripped ) ? mainArticles : alternateArticles;

    mapToUse.insert( std::pair( Folding::applySimpleCaseOnly( x.word ), x.word ) );

    articlesIncluded.insert( x.articleOffset );
  }

  if ( mainArticles.empty() && alternateArticles.empty() ) {
    return std::make_shared< Dictionary::DataRequestInstant >( false );
  }

  string result;

  multimap< QString, string >::const_iterator i;

  result += "<div class=\"audio-play\">";
  for ( i = mainArticles.begin(); i != mainArticles.end(); ++i ) {
    result += "<div class=\"audio-play-item\">";

    QUrl url;
    url.setScheme( "gdau" );
    url.setHost( QString::fromUtf8( getId().c_str() ) );
    url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( i->second.c_str() ) ) );

    string ref = string( "\"" ) + url.toEncoded().data() + "\"";

    addAudioLink( url.toEncoded(), getId() );

    result += "<a href=" + ref + R"( class="audio-play-icon"></a>)";
    result += "<a href=" + ref + ">" + i->second + "</a>";
    result += "</div>";
  }

  for ( i = alternateArticles.begin(); i != alternateArticles.end(); ++i ) {
    result += "<div class=\"audio-play-item\">";

    QUrl url;
    url.setScheme( "gdau" );
    url.setHost( QString::fromUtf8( getId().c_str() ) );
    url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( i->second.c_str() ) ) );

    string ref = string( "\"" ) + url.toEncoded().data() + "\"";

    result += addAudioLink( url.toEncoded(), getId() );

    result += "<a href=" + ref + R"( class="audio-play-icon"></a>)";
    result += "<a href=" + ref + ">" + i->second + "</a>";
    result += "</div>";
  }

  result += "</div>";

  auto * ret = new Dictionary::DataRequestInstant( true );

  ret->getData().resize( result.size() );

  memcpy( &( ret->getData().front() ), result.data(), result.size() );

  return std::shared_ptr< Dictionary::DataRequestInstant >( ret );
}

struct ShiftedVorbis
{
  QFile & f;
  size_t shift;

  ShiftedVorbis( QFile & f_, size_t shift_ ):
    f( f_ ),
    shift( shift_ )
  {
  }

  static size_t read( void * ptr, size_t size, size_t nmemb, void * datasource );
  static int seek( void * datasource, ogg_int64_t offset, int whence );
  static long tell( void * datasource );

  static ov_callbacks callbacks;
};

size_t ShiftedVorbis::read( void * ptr, size_t size, size_t nmemb, void * datasource )
{
  auto * sv = (ShiftedVorbis *)datasource;

  return sv->f.read( reinterpret_cast< char * >( ptr ), size * nmemb );
}

int ShiftedVorbis::seek( void * datasource, ogg_int64_t offset, int whence )
{
  auto * sv = (ShiftedVorbis *)datasource;

  if ( whence == SEEK_SET ) {
    offset += sv->shift;
  }

  if ( whence == SEEK_CUR ) {
    offset += sv->f.pos();
  }

  if ( whence == SEEK_END ) {
    offset += sv->f.size();
  }

  return sv->f.seek( offset );
}

long ShiftedVorbis::tell( void * datasource )
{
  auto * sv   = (ShiftedVorbis *)datasource;
  long result = sv->f.pos();

  if ( result != -1 ) {
    result -= sv->shift;
  }

  return result;
}

ov_callbacks ShiftedVorbis::callbacks = { ShiftedVorbis::read, ShiftedVorbis::seek, nullptr, ShiftedVorbis::tell };

struct WavHeader
{
  char riff[ 4 ];
  uint32_t riffLength;
  char waveAndFmt[ 8 ];
  uint32_t fmtLength;
  uint16_t formatTag;
  uint16_t channels;
  uint32_t samplesPerSec;
  uint32_t bytesPerSec;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[ 4 ];
  uint32_t dataLength;
}
__attribute__( ( packed ) )
;

sptr< Dictionary::DataRequest > LsaDictionary::getResource( const string & name )

{

  string strippedName = Utils::endsWithIgnoreCase( name, ".wav" ) ? string( name, 0, name.size() - 4 ) : name;

  vector< WordArticleLink > chain = findArticles( Text::toQString( strippedName ) );

  if ( chain.empty() ) {
    return std::make_shared< Dictionary::DataRequestInstant >( false );
  }

  File::Index f( getDictionaryFilenames()[ 0 ], QIODevice::ReadOnly );

  f.seek( chain[ 0 ].articleOffset );
  Entry e( f );

  f.seek( idxHeader.vorbisOffset );

  ShiftedVorbis sv( f.file(), idxHeader.vorbisOffset );

  OggVorbis_File vf;

  int result = ov_open_callbacks( &sv, &vf, 0, 0, ShiftedVorbis::callbacks );

  if ( result ) {
    throw exFailedToOpenVorbisData();
  }

  if ( ov_pcm_seek( &vf, e.samplesOffset ) ) {
    throw exFailedToSeekInVorbisData();
  }

  vorbis_info * vi = ov_info( &vf, -1 );

  if ( !vi ) {
    ov_clear( &vf );

    throw exFailedToRetrieveVorbisInfo();
  }

  sptr< Dictionary::DataRequestInstant > dr = std::make_shared< Dictionary::DataRequestInstant >( true );

  vector< char > & data = dr->getData();

  data.resize( sizeof( WavHeader ) + e.samplesLength * 2 );

  auto * wh = (WavHeader *)&data.front();

  memset( wh, 0, sizeof( *wh ) );

  memcpy( wh->riff, "RIFF", 4 );
  wh->riffLength = data.size() - 8;

  memcpy( wh->waveAndFmt, "WAVEfmt ", 8 );
  wh->fmtLength     = 16;
  wh->formatTag     = 1;
  wh->channels      = vi->channels;
  wh->samplesPerSec = vi->rate;
  wh->bytesPerSec   = vi->channels * vi->rate * 2;
  wh->blockAlign    = vi->channels * 2;
  wh->bitsPerSample = 16;
  memcpy( wh->data, "data", 4 );
  wh->dataLength = data.size() - sizeof( *wh );

  char * ptr    = &data.front() + sizeof( *wh );
  int left      = data.size() - sizeof( *wh );
  int bitstream = 0;

  while ( left ) {
    long result = ov_read( &vf, ptr, left, 0, 2, 1, &bitstream );

    if ( result <= 0 ) {
      qWarning( "Failed to read Vorbis data (code = %ld)", result );
      memset( ptr, 0, left );
      break;
    }

    if ( result > left ) {
      qWarning( "Warning: Vorbis decode returned more data than requested." );

      result = left;
    }

    ptr += result;
    left -= result;
  }

  ov_clear( &vf );

  return dr;
}

void LsaDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  if ( !loadIconFromFileName( fileName ) ) {
    dictionaryIcon = QIcon( ":/icons/lsasound.png" );
  }

  dictionaryIconLoaded = true;
}

}

vector< sptr< Dictionary::Class > > makeDictionaries( const vector< string > & fileNames,
                                                      const string & indicesDir,
                                                      Dictionary::Initializing & initializing )
{
  vector< sptr< Dictionary::Class > > dictionaries;

  for ( auto i = fileNames.begin(); i != fileNames.end(); ++i ) {
    if ( !Utils::endsWithIgnoreCase( *i, ".dat" ) && !Utils::endsWithIgnoreCase( *i, ".lsa" ) ) {
      continue;
    }

    try {
      File::Index f( *i, QIODevice::ReadOnly );

      char buf[ 9 ];

      if ( f.readRecords( buf, 9, 1 ) != 1 || memcmp( buf, "L\09\0S\0A\0\xff", 9 ) != 0 ) {
        continue;
      }

      vector< string > dictFiles( 1, *i );

      string dictId = Dictionary::makeDictionaryId( dictFiles );

      GlobalBroadcaster::instance()->addLsaDictMapping( QString::fromStdString( dictId ),
                                                        QString::fromStdString( *i ) );

      string indexFile = indicesDir + dictId;

      if ( Dictionary::needToRebuildIndex( dictFiles, indexFile ) || indexIsOldOrBad( indexFile ) ) {

        qDebug( "Lsa: Building the index for dictionary: %s", i->c_str() );

        initializing.indexingDictionary( Utils::Fs::basename( *i ) );

        File::Index idx( indexFile, QIODevice::WriteOnly );

        IdxHeader idxHeader;

        memset( &idxHeader, 0, sizeof( idxHeader ) );

        idx.write( idxHeader );

        IndexedWords indexedWords;

        auto entriesCount = f.read< uint32_t >();

        qDebug( "%s: %u entries", i->c_str(), entriesCount );

        idxHeader.soundsCount = entriesCount;

        vector< uint16_t > filenameBuffer;

        while ( entriesCount-- ) {
          uint32_t offset = f.tell();

          Entry e( f );

          e.name = stripExtension( e.name );

          qDebug( "Read filename %s (%u at %u)<", e.name.c_str(), e.samplesLength, e.samplesOffset );

          indexedWords.addWord( Text::toQString( e.name ), offset );
        }

        idxHeader.vorbisOffset = f.tell();

        char buf[ 4 ];

        f.read( buf, sizeof( buf ) );

        if ( strncmp( buf, "OggS", 4 ) != 0 ) {
          throw exInvalidData();
        }

        IndexInfo idxInfo = BtreeIndexing::buildIndex( indexedWords, idx );

        idxHeader.indexBtreeMaxElements = idxInfo.btreeMaxElements;
        idxHeader.indexRootOffset       = idxInfo.rootOffset;

        idxHeader.signature     = Signature;
        idxHeader.formatVersion = CurrentFormatVersion;

        idx.rewind();

        idx.write( &idxHeader, sizeof( idxHeader ) );
      }

      dictionaries.push_back( std::make_shared< LsaDictionary >( dictId, indexFile, dictFiles ) );
    }
    catch ( std::exception & e ) {
      qWarning( "Lingvo's LSA reading failed: %s, error: %s", i->c_str(), e.what() );
    }
  }

  return dictionaries;
}

}
