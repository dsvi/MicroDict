/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "zipsounds.hh"
#include "dictfile.hh"
#include "folding.hh"
#include "text.hh"
#include "btreeidx.hh"

#include "audiolink.hh"
#include "indexedzip.hh"
#include "filetype.hh"
#include "chunkedstorage.hh"
#include "htmlescape.hh"

#include <set>
#include <string>
#include <QFile>
#include <QDir>


#include "utils.hh"

namespace ZipSounds {

using std::string;
using std::map;
using std::multimap;
using std::set;
using BtreeIndexing::WordArticleLink;
using BtreeIndexing::IndexedWords;
using BtreeIndexing::IndexInfo;

namespace {

DEF_EX( exInvalidData, "Invalid data encountered", Dictionary::Ex )

enum {
  Signature            = 0x5350495a,
  CurrentFormatVersion = 6 + BtreeIndexing::FormatVersion + BtreeIndexing::ZipParseLogicVersion
};

#pragma pack( push, 1 )
struct IdxHeader
{
  uint32_t signature;
  uint32_t formatVersion;
  uint32_t soundsCount;
  uint32_t indexBtreeMaxElements;
  uint32_t indexRootOffset;
  uint32_t chunksOffset;
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

QString stripExtension( const string & str )
{
  QString name;
  try {
    name = Text::toQString( str );
  }
  catch ( Text::exCantDecode & ) {
    return name;
  }

  if ( Filetype::isNameOfSound( str ) ) {
    const qsizetype pos = name.lastIndexOf( u'.' );
    if ( pos >= 0 ) {
      name.truncate( pos );
    }

    qsizetype n = name.size();
    while ( n && name.at( n - 1 ) == u' ' ) {
      n--;
    }

    if ( n != name.size() ) {
      name.truncate( n );
    }
  }
  return name;
}

class ZipSoundsDictionary: public BtreeIndexing::BtreeDictionary
{
  QMutex idxMutex;
  File::Index idx;
  IdxHeader idxHeader;
  sptr< ChunkedStorage::Reader > chunks;
  IndexedZip zipsFile;

public:

  ZipSoundsDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles );

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

ZipSoundsDictionary::ZipSoundsDictionary( const string & id,
                                          const string & indexFile,
                                          const vector< string > & dictionaryFiles ):
  BtreeDictionary( id, dictionaryFiles ),
  idx( indexFile, QIODevice::ReadOnly ),
  idxHeader( idx.read< IdxHeader >() )
{
  chunks = std::shared_ptr< ChunkedStorage::Reader >( new ChunkedStorage::Reader( idx, idxHeader.chunksOffset ) );

  openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );

  QString zipName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  zipsFile.openZipFile( zipName );
  zipsFile.openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );
}

string ZipSoundsDictionary::getName() noexcept
{
  string result = Utils::Fs::basename( getDictionaryFilenames()[ 0 ] );

  result.erase( result.rfind( '.' ) );

  return result;
}

sptr< Dictionary::DataRequest > ZipSoundsDictionary::getArticle( const QString & word,
                                                                 const vector< QString > & alts,
                                                                 const QString &,
                                                                 bool ignoreDiacritics )

{
  vector< WordArticleLink > chain = findArticles( word, ignoreDiacritics );

  for ( const auto & alt : alts ) {

    vector< WordArticleLink > altChain = findArticles( alt, ignoreDiacritics );

    chain.insert( chain.end(), altChain.begin(), altChain.end() );
  }

  multimap< QString, uint32_t > mainArticles, alternateArticles;

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

    multimap< QString, uint32_t > & mapToUse =
      ( wordCaseFolded == headwordStripped ) ? mainArticles : alternateArticles;

    mapToUse.insert( std::pair( Folding::applySimpleCaseOnly( x.word ), x.articleOffset ) );

    articlesIncluded.insert( x.articleOffset );
  }

  if ( mainArticles.empty() && alternateArticles.empty() ) {
    return std::make_shared< Dictionary::DataRequestInstant >( false );
  }

  string result;

  multimap< QString, uint32_t >::const_iterator i;

  result += "<div class=\"audio-play\">";

  vector< char > chunk;
  char * nameBlock;

  for ( i = mainArticles.begin(); i != mainArticles.end(); ++i ) {
    try {
      QMutexLocker _( &idxMutex );
      nameBlock = chunks->getBlock( i->second, chunk );

      if ( nameBlock >= &chunk.front() + chunk.size() ) {
        throw ChunkedStorage::exAddressOutOfRange();
      }
    }
    catch ( ChunkedStorage::exAddressOutOfRange & ) {
      continue;
    }

    uint16_t sz;
    memcpy( &sz, nameBlock, sizeof( uint16_t ) );
    nameBlock += sizeof( uint16_t );

    string name( nameBlock, sz );
    nameBlock += sz;

    string displayedName =
      mainArticles.size() + alternateArticles.size() > 1 ? name : Text::toUtf8( stripExtension( name ) );

    result += "<div class=\"audio-play-item\">";

    QUrl url;
    url.setScheme( "gdau" );
    url.setHost( QString::fromUtf8( getId().c_str() ) );
    url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( name.c_str() ) ) );

    string ref = string( "\"" ) + url.toEncoded().data() + "\"";

    result += addAudioLink( url.toEncoded(), getId() );

    result += "<a href=" + ref + R"( class="audio-play-icon"></a>)";
    result += "<a href=" + ref + ">" + Html::escape( displayedName ) + "</a>";
    result += "</div>";
  }

  for ( i = alternateArticles.begin(); i != alternateArticles.end(); ++i ) {
    try {
      QMutexLocker _( &idxMutex );
      nameBlock = chunks->getBlock( i->second, chunk );

      if ( nameBlock >= &chunk.front() + chunk.size() ) {
        throw ChunkedStorage::exAddressOutOfRange();
      }
    }
    catch ( ChunkedStorage::exAddressOutOfRange & ) {
      continue;
    }

    uint16_t sz;
    memcpy( &sz, nameBlock, sizeof( uint16_t ) );
    nameBlock += sizeof( uint16_t );

    string name( nameBlock, sz );
    nameBlock += sz;

    string displayedName =
      mainArticles.size() + alternateArticles.size() > 1 ? name : Text::toUtf8( stripExtension( name ) );

    result += "<div class=\"audio-play-item\">";

    QUrl url;
    url.setScheme( "gdau" );
    url.setHost( QString::fromUtf8( getId().c_str() ) );
    url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( name.c_str() ) ) );

    string ref = string( "\"" ) + url.toEncoded().data() + "\"";

    addAudioLink( url.toEncoded(), getId() );

    result += "<a href=" + ref + R"( class="audio-play-icon"></a>)";
    result += "<a href=" + ref + ">" + Html::escape( displayedName ) + "</a>";
    result += "</div>";
  }

  result += "</div>";

  auto ret = std::make_shared< Dictionary::DataRequestInstant >( true );
  ret->appendString( result );
  return ret;
}

sptr< Dictionary::DataRequest > ZipSoundsDictionary::getResource( const string & name )

{

  QString strippedName = stripExtension( name );

  vector< WordArticleLink > chain = findArticles( strippedName );

  if ( chain.empty() ) {
    return std::make_shared< Dictionary::DataRequestInstant >( false );
  }

  uint32_t dataOffset = 0;
  for ( int x = chain.size() - 1; x >= 0; x-- ) {
    vector< char > chunk;
    char * nameBlock = chunks->getBlock( chain[ x ].articleOffset, chunk );

    uint16_t sz;
    memcpy( &sz, nameBlock, sizeof( uint16_t ) );
    nameBlock += sizeof( uint16_t );

    string fileName( nameBlock, sz );
    nameBlock += sz;

    memcpy( &dataOffset, nameBlock, sizeof( uint32_t ) );

    if ( name.compare( fileName ) == 0 ) {
      break;
    }
  }

  sptr< Dictionary::DataRequestInstant > dr = std::make_shared< Dictionary::DataRequestInstant >( true );

  if ( zipsFile.loadFile( dataOffset, dr->getData() ) ) {
    return dr;
  }

  return std::make_shared< Dictionary::DataRequestInstant >( false );
}

void ZipSoundsDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  if ( !loadIconFromFileName( fileName ) ) {
    dictionaryIcon = QIcon( ":/icons/zipsound.svg" );
  }

  dictionaryIconLoaded = true;
}

}

vector< sptr< Dictionary::Class > > makeDictionaries( const vector< string > & fileNames,
                                                      const string & indicesDir,
                                                      Dictionary::Initializing & initializing )

{
  (void)initializing;
  vector< sptr< Dictionary::Class > > dictionaries;

  for ( const auto & fileName : fileNames ) {
    if ( !Utils::endsWithIgnoreCase( fileName, ".zips" ) ) {
      continue;
    }

    try {
      vector< string > dictFiles( 1, fileName );
      string dictId    = Dictionary::makeDictionaryId( dictFiles );
      string indexFile = indicesDir + dictId;

      if ( Dictionary::needToRebuildIndex( dictFiles, indexFile ) || indexIsOldOrBad( indexFile ) ) {
        qDebug( "Zips: Building the index for dictionary: %s", fileName.c_str() );

        File::Index idx( indexFile, QIODevice::WriteOnly );
        IdxHeader idxHeader;

        memset( &idxHeader, 0, sizeof( idxHeader ) );

        idx.write( idxHeader );

        IndexedWords names, zipFileNames;
        ChunkedStorage::Writer chunks( idx );
        quint32 namesCount = 0;

        IndexedZip zipFile;
        if ( zipFile.openZipFile( QDir::fromNativeSeparators( fileName.c_str() ) ) ) {
          zipFile.indexFile( zipFileNames, &namesCount );
        }

        if ( !zipFileNames.empty() ) {
          for ( auto & zipFileName : zipFileNames ) {
            vector< WordArticleLink > links = zipFileName.second;
            for ( auto & link : links ) {

              uint32_t offset = chunks.startNewBlock();
              uint16_t sz     = link.word.size();
              chunks.addToBlock( &sz, sizeof( uint16_t ) );
              chunks.addToBlock( link.word.c_str(), sz );
              chunks.addToBlock( &link.articleOffset, sizeof( uint32_t ) );

              QString word = stripExtension( link.word );
              if ( !word.isEmpty() ) {
                names.addWord( word, offset );
              }
            }
          }

          idxHeader.chunksOffset = chunks.finish();

          IndexInfo idxInfo = BtreeIndexing::buildIndex( names, idx );

          idxHeader.indexBtreeMaxElements = idxInfo.btreeMaxElements;
          idxHeader.indexRootOffset       = idxInfo.rootOffset;

          idxHeader.signature     = Signature;
          idxHeader.formatVersion = CurrentFormatVersion;

          idxHeader.soundsCount = namesCount;

          idx.rewind();

          idx.write( &idxHeader, sizeof( idxHeader ) );
        }
        else {
          idx.close();
          QFile::remove( QDir::fromNativeSeparators( indexFile.c_str() ) );
          throw exInvalidData();
        }
      }

      dictionaries.push_back( std::make_shared< ZipSoundsDictionary >( dictId, indexFile, dictFiles ) );
    }
    catch ( std::exception & e ) {
      qWarning( "Zipped sounds pack reading failed: %s, error: %s", fileName.c_str(), e.what() );
    }
  }

  return dictionaries;
}

}
