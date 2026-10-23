/* This file is (c) 2013 Timon Wong <timon86.wang AT gmail DOT com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "mdx.hh"
#include "btreeidx.hh"
#include "folding.hh"
#include "text.hh"
#include "dictfile.hh"
#include "chunkedstorage.hh"
#include "langcoder.hh"
#include "audiolink.hh"
#include "ex.hh"
#include "mdictparser.hh"
#include "filetype.hh"
#include "ftshelpers.hh"
#include "htmlescape.hh"
#include <algorithm>
#include <map>
#include <set>
#include <list>
#include "globalregex.hh"
#include "tiff.hh"
#include "utils.hh"
#include <QAtomicInt>
#include <QDir>
#include <QRegularExpression>
#include <QString>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <QStringBuilder>

namespace Mdx {

using std::map;
using std::multimap;
using std::set;
using std::list;
using std::pair;
using std::string;

using BtreeIndexing::WordArticleLink;
using BtreeIndexing::IndexedWords;
using BtreeIndexing::IndexInfo;

using namespace Mdict;

enum {
  kSignature            = 0x4349444d,
  kCurrentFormatVersion = 12 + BtreeIndexing::FormatVersion + Folding::Version
};

DEF_EX( exCorruptDictionary, "dictionary file was tampered or corrupted", std::exception )

#pragma pack( push, 1 )

struct IdxHeader
{
  uint32_t signature;
  uint32_t formatVersion;
  uint32_t parserVersion;
  uint32_t foldingVersion;

  uint32_t articleCount;
  uint32_t wordCount;

  uint32_t isRightToLeft;
  uint32_t chunksOffset;

  uint32_t descriptionAddress;
  uint32_t descriptionSize;

  uint32_t styleSheetAddress;
  uint32_t styleSheetCount;

  uint32_t indexBtreeMaxElements;
  uint32_t indexRootOffset;

  uint32_t langFrom;
  uint32_t langTo;

  uint32_t mddIndexInfosOffset;
  uint32_t mddIndexInfosCount;
  uint64_t sourceLastModified;
};
static_assert( alignof( IdxHeader ) == 1 );
#pragma pack( pop )

class IndexedMdd: public BtreeIndexing::BtreeIndex
{
  QMutex & idxMutex;
  QMutex fileMutex;
  ChunkedStorage::Reader & chunks;
  QFile mddFile;
  bool isFileOpen;

public:

  IndexedMdd( QMutex & idxMutex, ChunkedStorage::Reader & chunks ):
    idxMutex( idxMutex ),
    chunks( chunks ),
    isFileOpen( false )
  {
  }

  using BtreeIndexing::BtreeIndex::openIndex;

  bool open( const char * fileName )
  {
    mddFile.setFileName( QString::fromUtf8( fileName ) );
    isFileOpen = mddFile.open( QFile::ReadOnly );
    return isFileOpen;
  }

  inline bool isOpen() const
  {
    return isFileOpen;
  }

  bool hasFile( const QString & name )
  {
    if ( !isFileOpen ) {
      return false;
    }
    vector< WordArticleLink > links = findArticles( name );
    return !links.empty();
  }

  bool loadFile( const QString & name, std::vector< char > & result )
  {
    if ( !isFileOpen ) {
      return false;
    }

    vector< WordArticleLink > links = findArticles( name );
    if ( links.empty() ) {
      return false;
    }

    MdictParser::RecordInfo indexEntry{};
    vector< char > chunk;
    const char * indexEntryPtr = chunks.getBlock( links[ 0 ].articleOffset, chunk );
    memcpy( &indexEntry, indexEntryPtr, sizeof( indexEntry ) );

    if ( indexEntry.decompressedBlockSize < indexEntry.recordOffset + indexEntry.recordSize ) {
      return false;
    }

    QByteArray decompressed;

    {
      QMutexLocker _( &idxMutex );
      ScopedMemMap compressed( mddFile, indexEntry.compressedBlockPos, indexEntry.compressedBlockSize );
      if ( !compressed.startAddress() ) {
        return false;
      }

      if ( !MdictParser::parseCompressedBlock( indexEntry.compressedBlockSize,
                                               (char *)compressed.startAddress(),
                                               indexEntry.decompressedBlockSize,
                                               decompressed ) ) {
        return false;
      }
    }

    result.resize( indexEntry.recordSize );
    memcpy( &result.front(), decompressed.constData() + indexEntry.recordOffset, indexEntry.recordSize );
    return true;
  }
};

class MdxDictionary: public BtreeIndexing::BtreeDictionary
{
  QMutex idxMutex;
  File::Index idx;
  string idxFileName;
  IdxHeader idxHeader;
  string encoding;
  ChunkedStorage::Reader chunks;
  QFile dictFile;
  vector< sptr< IndexedMdd > > mddResources;
  MdictParser::StyleSheets styleSheets;

  QAtomicInt deferredInitDone;
  QMutex deferredInitMutex;
  bool deferredInitRunnableStarted = false;

  string initError;
  QString cacheDirName;

public:

  MdxDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles );

  ~MdxDictionary() override;

  void deferredInit() override;

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

  sptr< Dictionary::DataRequest >
  getArticle( const QString & word, const vector< QString > & alts, const QString &, bool ignoreDiacritics ) override;
  sptr< Dictionary::DataRequest > getResource( const string & name ) override;
  const QString & getDescription() override;

  sptr< Dictionary::DataRequest >
  getSearchResults( const QString & searchString, int searchMode, bool matchCase, bool ignoreDiacritics ) override;
  void getArticleText( uint32_t articleAddress, QString & headword, QString & text ) override;

  void makeFTSIndex( QAtomicInt & isCancelled ) override;

  sptr< Dictionary::Class > duplicate() override;

  void setFTSParameters( const Config::FullTextSearch & fts ) override
  {
    if ( !ensureInitDone().empty() ) {
      return;
    }
    if ( metadata_enable_fts.has_value() ) {
      can_FTS = metadata_enable_fts.value();
    }
    else {
      can_FTS = fts.enabled && !fts.disabledTypes.contains( "MDICT", Qt::CaseInsensitive )
        && ( fts.maxDictionarySize == 0 || getArticleCount() <= fts.maxDictionarySize );
    }
  }

  QString getCachedFileName( QString name );

protected:

  void loadIcon() noexcept override;

private:

  const string & ensureInitDone() override;
  void doDeferredInit();

  void loadArticle( uint32_t offset, string & articleText, bool noFilter = false );

  QString & filterResource( QString & article );

  void replaceLinks( QString & id, QString & article );
  QString isolateStyleCssInHtml( const QString & description );
  void replaceStyleInHtml( QString & id, QString & article );
  void replaceFontLinks( QString & id, QString & article );

  friend class MdxArticleRequest;
  friend class MddResourceRequest;
  void loadResourceFile( const QString & resourceName, vector< char > & data );
};

MdxDictionary::MdxDictionary( const string & id, const string & indexFile, const vector< string > & dictionaryFiles ):
  BtreeDictionary( id, dictionaryFiles ),
  idx( indexFile, QIODevice::ReadOnly ),
  idxFileName( indexFile ),
  idxHeader( idx.read< IdxHeader >() ),
  chunks( idx, idxHeader.chunksOffset )
{
  idx.seek( sizeof( idxHeader ) );
  idx.readU32SizeAndData<>( dictionaryName );

  if ( dictionaryName.empty() ) {
    QFileInfo f( QString::fromUtf8( dictionaryFiles[ 0 ].c_str() ) );
    dictionaryName = f.completeBaseName().toStdString();
  }

  idx.readU32SizeAndData<>( encoding );

  dictFile.setFileName( QString::fromUtf8( dictionaryFiles[ 0 ].c_str() ) );
  if ( !dictFile.open( QIODevice::ReadOnly ) ) {
    throw std::runtime_error( "mdx: failed to open main file" );
  };

  ftsIdxName = indexFile + Dictionary::getFtsSuffix();

  cacheDirName = QDir::tempPath() + QDir::separator() + QString::fromUtf8( getId().c_str() ) + ".cache";
}

MdxDictionary::~MdxDictionary()
{
  QMutexLocker _( &deferredInitMutex );

  dictFile.close();

  Utils::Fs::removeDirectory( cacheDirName );
}

void MdxDictionary::deferredInit()
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

const string & MdxDictionary::ensureInitDone()
{
  doDeferredInit();
  return initError;
}

void MdxDictionary::doDeferredInit()
{
  if ( !Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
    QMutexLocker _( &deferredInitMutex );

    if ( Utils::AtomicInt::loadAcquire( deferredInitDone ) ) {
      return;
    }

    try {
      idx.seek( idxHeader.styleSheetAddress );
      for ( uint32_t i = 0; i < idxHeader.styleSheetCount; i++ ) {
        qint32 key = idx.read< qint32 >();
        vector< char > buf;
        quint32 sz;

        sz = idx.read< quint32 >();
        buf.resize( sz );
        idx.read( &buf.front(), sz );
        QString styleBegin = QString::fromUtf8( buf.data() );

        sz = idx.read< quint32 >();
        buf.resize( sz );
        idx.read( &buf.front(), sz );
        QString styleEnd = QString::fromUtf8( buf.data() );

        styleSheets[ key ] = pair< QString, QString >( styleBegin, styleEnd );
      }

      openIndex( IndexInfo( idxHeader.indexBtreeMaxElements, idxHeader.indexRootOffset ), idx, idxMutex );

      vector< string > mddFileNames;
      vector< IndexInfo > mddIndexInfos;
      idx.seek( idxHeader.mddIndexInfosOffset );
      for ( uint32_t i = 0; i < idxHeader.mddIndexInfosCount; i++ ) {
        quint32 sz = idx.read< quint32 >();
        vector< char > buf( sz );
        idx.read( &buf.front(), sz );
        uint32_t btreeMaxElements = idx.read< uint32_t >();
        uint32_t rootOffset       = idx.read< uint32_t >();
        mddFileNames.emplace_back( &buf.front() );
        mddIndexInfos.emplace_back( btreeMaxElements, rootOffset );
      }

      const vector< string > dictFiles = getDictionaryFilenames();
      for ( uint32_t i = 1; i < dictFiles.size() && i < mddFileNames.size() + 1; i++ ) {
        QFileInfo fi( QString::fromUtf8( dictFiles[ i ].c_str() ) );
        QString mddFileName = QString::fromUtf8( mddFileNames[ i - 1 ].c_str() );

        if ( fi.fileName() != mddFileName || !fi.exists() ) {
          continue;
        }

        sptr< IndexedMdd > mdd = std::make_shared< IndexedMdd >( idxMutex, chunks );
        mdd->openIndex( mddIndexInfos[ i - 1 ], idx, idxMutex );
        mdd->open( dictFiles[ i ].c_str() );
        mddResources.push_back( mdd );
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

void MdxDictionary::makeFTSIndex( QAtomicInt & isCancelled )
{
  if ( !( Dictionary::needToRebuildIndex( getDictionaryFilenames(), ftsIdxName )
          || FtsHelpers::ftsIndexIsOldOrBad( this ) ) ) {
    FTS_index_completed.ref();
  }

  if ( haveFTSIndex() ) {
    return;
  }

  qDebug( "MDict: Building the full-text index for dictionary: %s", getName().c_str() );

  try {
    auto _dict = std::static_pointer_cast< MdxDictionary >( duplicate() );
    if ( !_dict || !_dict->ensureInitDone().empty() ) {
      return;
    }
    FtsHelpers::makeFTSIndex( _dict.get(), isCancelled );
    FTS_index_completed.ref();
  }
  catch ( std::exception & ex ) {
    qWarning( "MDict: Failed building full-text search index for \"%s\", reason: %s", getName().c_str(), ex.what() );
    QFile::remove( ftsIdxName.c_str() );
  }
}

sptr< Dictionary::Class > MdxDictionary::duplicate()
{
  auto instance = std::make_shared< MdxDictionary >( getId(), idxFileName, getDictionaryFilenames() );
  instance->metadata_enable_fts = this->metadata_enable_fts;
  instance->can_FTS             = this->can_FTS;
  return instance;
}

void MdxDictionary::getArticleText( uint32_t articleAddress, QString & headword, QString & text )
{
  try {
    headword.clear();
    string articleText;

    loadArticle( articleAddress, articleText, true );
    text = Html::unescape( QString::fromUtf8( articleText.data(), articleText.size() ) );
  }
  catch ( std::exception & ex ) {
    qWarning( "MDict: Failed retrieving article from \"%s\", reason: %s", getName().c_str(), ex.what() );
  }
}

sptr< Dictionary::DataRequest > MdxDictionary::getSearchResults( const QString & searchString,
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

class MdxArticleRequest: public Dictionary::DataRequest
{
  QString word;
  vector< QString > alts;
  MdxDictionary & dict;
  bool ignoreDiacritics;

  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  MdxArticleRequest( const QString & word_,
                     const vector< QString > & alts_,
                     MdxDictionary & dict_,
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

  ~MdxArticleRequest() override
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};


void MdxArticleRequest::run()
{
  if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
    finish();
    return;
  }

  if ( !dict.ensureInitDone().empty() ) {
    setErrorString( QString::fromUtf8( dict.ensureInitDone().c_str() ) );
    finish();
    return;
  }

  vector< WordArticleLink > chain = dict.findArticles( word, ignoreDiacritics );

  for ( const auto & alt : alts ) {
    vector< WordArticleLink > altChain = dict.findArticles( alt, ignoreDiacritics );
    chain.insert( chain.end(), altChain.begin(), altChain.end() );
  }

  set< uint32_t > articlesIncluded;
  string articleText;

  for ( unsigned x = 0; x < chain.size(); ++x ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      finish();
      return;
    }

    if ( articlesIncluded.find( chain[ x ].articleOffset ) != articlesIncluded.end() ) {
      continue;
    }

    string articleBody;
    bool hasError = false;
    QString errorMessage;

    try {
      dict.loadArticle( chain[ x ].articleOffset, articleBody );
    }
    catch ( exCorruptDictionary & ) {
      errorMessage = tr( "Dictionary file was tampered or corrupted" );
      hasError     = true;
    }
    catch ( std::exception & e ) {
      errorMessage = e.what();
      hasError     = true;
    }

    if ( hasError ) {
      setErrorString( tr( "Failed loading article from %1, reason: %2" )
                        .arg( QString::fromUtf8( dict.getDictionaryFilenames()[ 0 ].c_str() ), errorMessage ) );
      finish();
      return;
    }

    articlesIncluded.insert( chain[ x ].articleOffset );

    if ( strncmp( articleBody.c_str(), "@@@LINK=", 8 ) == 0 ) {
      QString target = Text::toQString( articleBody.c_str() + 8 );
      target         = Folding::trimWhitespace( target );
      vector< WordArticleLink > altChain = dict.findArticles( target );
      chain.insert( chain.end(), altChain.begin(), altChain.end() );
      continue;
    }

    string cleaner = Utils::Html::getHtmlCleaner();
    articleText += "<div class=\"mdict\">" + articleBody + cleaner + "</div>\n";
  }

  if ( !articleText.empty() ) {
    articleText += "</div></div></div></div></div></div></div></div></div>";

    QMutexLocker _( &dataMutex );
    data.insert( data.end(), articleText.begin(), articleText.end() );
    hasAnyData = true;
  }

  finish();
}

sptr< Dictionary::DataRequest > MdxDictionary::getArticle( const QString & word,
                                                           const vector< QString > & alts,
                                                           const QString &,
                                                           bool ignoreDiacritics )
{
  return std::make_shared< MdxArticleRequest >( word, alts, *this, ignoreDiacritics );
}

class MddResourceRequest: public Dictionary::DataRequest
{
  MdxDictionary & dict;
  QString resourceName;
  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  MddResourceRequest( MdxDictionary & dict_, const string & resourceName_ ):
    dict( dict_ ),
    resourceName( Text::toQString( resourceName_ ) )
  {
    f = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }

  QByteArray isolate_css();
  void run();

  void cancel() override
  {
    isCancelled.ref();
  }

  ~MddResourceRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }

private:
  static QString detectCssEncoding( const vector< char > & data, const string & dictEncoding )
  {
    if ( data.empty() ) {
      return "UTF-8";
    }

    string bomEncoding = Text::detectEncodingFromBom( data.data(), data.size() );
    if ( !bomEncoding.empty() ) {
      return QString::fromStdString( bomEncoding );
    }

    if ( Text::isValidUtf8( data.data(), data.size() ) ) {
      return "UTF-8";
    }

    if ( !dictEncoding.empty() ) {
      return QString::fromStdString( dictEncoding );
    }

    return "UTF-8";
  }
};


QByteArray MddResourceRequest::isolate_css()
{

  const QString id = QString::fromUtf8( dict.getId().c_str() );

  QString encoding = detectCssEncoding( data, dict.encoding );
  QString css      = MdictParser::toUtf16( encoding, data.data(), data.size() );

  int pos = 0;

  QString newCSS;
  QRegularExpressionMatchIterator it = RX::Mdx::links.globalMatch( css );
  while ( it.hasNext() ) {
    QRegularExpressionMatch match = it.next();
    newCSS += css.mid( pos, match.capturedStart() - pos );
    pos         = match.capturedEnd();
    QString url = match.captured( 2 );


    if ( url.indexOf( ":/" ) >= 0 || url.indexOf( "data:" ) >= 0 ) {
      newCSS += match.captured();
      continue;
    }

    if ( url.startsWith( "//" ) ) {
      newCSS += "url(" + match.captured( 1 ) + "https:" + url + match.captured( 3 ) + ")";
      continue;
    }

    QString newUrl = QString( "url(" ) + match.captured( 1 ) + "bres://" + id + "/" + url + match.captured( 3 ) + ")";
    newCSS += newUrl;
  }
  if ( pos ) {
    newCSS += css.mid( pos );
    css = newCSS;
    newCSS.clear();
  }

  dict.isolateCSS( css );
  auto bytes = css.toUtf8();

  return bytes;
}

void MddResourceRequest::run()
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

  set< QString, std::less<> > resourceIncluded;

  for ( ;; ) {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      finish();
      return;
    }
    string u8ResourceName = Text::toUtf8( resourceName );
    if ( !resourceIncluded.insert( resourceName ).second ) {
      finish();
      return;
    }

    QMutexLocker _( &dataMutex );
    data.clear();

    const QString id = QString::fromUtf8( dict.getId().c_str() );

    const QString unique_key = id + QString::fromStdString( u8ResourceName );

    dict.loadResourceFile( resourceName, data );

    static const char pattern[ 16 ] =
      { '@', '\0', '@', '\0', '@', '\0', 'L', '\0', 'I', '\0', 'N', '\0', 'K', '\0', '=', '\0' };

    if ( data.size() > sizeof( pattern ) ) {
      if ( memcmp( &data.front(), pattern, sizeof( pattern ) ) == 0 ) {
        data.push_back( '\0' );
        data.push_back( '\0' );
        QString target =
          MdictParser::toUtf16( "UTF-16LE", &data.front() + sizeof( pattern ), data.size() - sizeof( pattern ) );
        resourceName = target.trimmed();
        continue;
      }
    }

    if ( data.size() > 0 ) {
      hasAnyData = true;

      if ( Filetype::isNameOfCSS( u8ResourceName ) ) {
        QByteArray bytes = isolate_css();

        data.resize( bytes.size() );
        memcpy( &data.front(), bytes.constData(), bytes.size() );
      }
      if ( Filetype::isNameOfTiff( u8ResourceName ) ) {
        GdTiff::tiff2img( data );
      }
    }
    break;
  }

  finish();
}

sptr< Dictionary::DataRequest > MdxDictionary::getResource( const string & name )
{
  return std::make_shared< MddResourceRequest >( *this, name );
}

const QString & MdxDictionary::getDescription()
{
  if ( !dictionaryDescription.isEmpty() ) {
    return dictionaryDescription;
  }

  if ( idxHeader.descriptionSize == 0 ) {
    dictionaryDescription = "NONE";
  }
  else {
    vector< char > chunk;
    char * dictDescription = chunks.getBlock( idxHeader.descriptionAddress, chunk );
    string str( dictDescription );
    dictionaryDescription = QString::fromUtf8( str.c_str(), str.size() );
  }

  dictionaryDescription = MdictParser::substituteStylesheet( dictionaryDescription, styleSheets );
  dictionaryDescription = filterResource( dictionaryDescription );

  dictionaryDescription = dictionaryDescription + QString::fromStdString( Utils::Html::getHtmlCleaner() );
  return dictionaryDescription;
}

void MdxDictionary::loadIcon() noexcept
{
  if ( dictionaryIconLoaded ) {
    return;
  }

  QString fileName = QDir::fromNativeSeparators( getDictionaryFilenames()[ 0 ].c_str() );

  QString text = QString::fromStdString( dictionaryName );

  if ( !loadIconFromFileName( fileName ) && !loadIconFromText( ":/icons/mdict-bg.svg", text ) ) {
    dictionaryIcon = QIcon( ":/icons/mdict.png" );
  }

  dictionaryIconLoaded = true;
}

void MdxDictionary::loadArticle( uint32_t offset, string & articleText, bool noFilter )
{
  vector< char > chunk;

  MdictParser::RecordInfo recordInfo;
  const char * pRecordInfo = chunks.getBlock( offset, chunk );
  memcpy( &recordInfo, pRecordInfo, sizeof( recordInfo ) );

  QByteArray decompressed;

  {
    QMutexLocker _( &idxMutex );
    ScopedMemMap compressed( dictFile, recordInfo.compressedBlockPos, recordInfo.compressedBlockSize );
    if ( !compressed.startAddress() ) {
      throw exCorruptDictionary();
    }

    if ( !MdictParser::parseCompressedBlock( recordInfo.compressedBlockSize,
                                             (char *)compressed.startAddress(),
                                             recordInfo.decompressedBlockSize,
                                             decompressed ) ) {
      throw exCorruptDictionary();
    }
  }

  QString article =
    MdictParser::toUtf16( encoding.c_str(), decompressed.constData() + recordInfo.recordOffset, recordInfo.recordSize );

  if ( !noFilter ) {
    article = MdictParser::substituteStylesheet( article, styleSheets );
    article = filterResource( article );
  }

  articleText = Utils::c_string( article );
}

QString & MdxDictionary::filterResource( QString & article )
{
  QString id = QString::fromStdString( getId() );

  article.replace( RX::Mdx::protocolRelativeUrlQuoted, R"(\1https://)" );
  article.replace( RX::Mdx::protocolRelativeUrlUnquoted, R"(\1https://)" );

  replaceLinks( id, article );

  article.replace( RX::Mdx::htmlTagStart, "<gd-section-html" );
  article.replace( RX::Mdx::htmlTagEnd, "</gd-section-html>" );

  article.replace( RX::Mdx::bodyTagStart, "<gd-section-body" );
  article.replace( RX::Mdx::bodyTagEnd, "</gd-section-body>" );

  article.replace( RX::Mdx::headTagStart, "<gd-section-head" );
  article.replace( RX::Mdx::headTagEnd, "</gd-section-head>" );

  replaceStyleInHtml( id, article );
  article = isolateStyleCssInHtml( article );
  return article;
}

void MdxDictionary::replaceLinks( QString & id, QString & article )
{
  QString articleNewText;
  int linkPos                        = 0;
  QRegularExpressionMatchIterator it = RX::Mdx::allLinksRe.globalMatch( article );
  while ( it.hasNext() ) {
    QRegularExpressionMatch allLinksMatch = it.next();

    if ( allLinksMatch.capturedEnd() < linkPos ) {
      continue;
    }

    articleNewText += article.mid( linkPos, allLinksMatch.capturedStart() - linkPos );
    linkPos = allLinksMatch.capturedEnd();

    QString linkTxt  = allLinksMatch.captured();
    QString linkType = allLinksMatch.captured( 1 ).toLower();
    QString newLink;

    if ( linkType.compare( "a" ) == 0 || linkType.compare( "area" ) == 0 ) {
      newLink = linkTxt;

      QRegularExpressionMatch match = RX::Mdx::audioRe.match( newLink );
      if ( match.hasMatch() ) {
        QString newTxt = match.captured( 1 ) + match.captured( 2 ) + "gdau://" + id + "/" + match.captured( 3 )
          + match.captured( 2 ) + R"( onclick="return false;" )";
        newLink = QString::fromUtf8(
                    addAudioLink( "gdau://" + getId() + "/" + match.captured( 3 ).toUtf8().data(), getId() ).c_str() )
          + newLink.replace( match.capturedStart(), match.capturedLength(), newTxt );
      }

      match = RX::Mdx::wordCrossLink.match( newLink );
      if ( match.hasMatch() ) {
        if ( !match.captured( 3 ).isEmpty() ) {
          QString newTxt = match.captured( 1 ) + match.captured( 2 ) + "gdlookup://localhost/" + match.captured( 3 );

          if ( match.lastCapturedIndex() >= 4 && !match.captured( 4 ).isEmpty() ) {
            newTxt += QString( "?gdanchor=" ) + match.captured( 4 ).mid( 1 );
          }

          newTxt += match.captured( 2 );
          newLink.replace( match.capturedStart(), match.capturedLength(), newTxt );
        }
        else {
          QString newTxt = match.captured( 1 ) + match.captured( 2 );

          if ( match.lastCapturedIndex() >= 4 && !match.captured( 4 ).isEmpty() ) {
            newTxt += match.captured( 4 );
          }

          newTxt += match.captured( 2 );
          newLink.replace( match.capturedStart(), match.capturedLength(), newTxt );
        }
      }
    }
    else if ( linkType.compare( "link" ) == 0 ) {
      QRegularExpressionMatch match = RX::Mdx::stylesRe.match( linkTxt );
      if ( match.hasMatch() ) {
        QString newText =
          match.captured( 1 ) + match.captured( 2 ) + "bres://" + id + "/" + match.captured( 3 ) + match.captured( 2 );
        newLink = linkTxt.replace( match.capturedStart(), match.capturedLength(), newText );
      }
      else {
        newLink = linkTxt.replace( RX::Mdx::stylesRe2, R"(\1"bres://)" + id + R"(/\2")" );
      }
    }
    else {
      QRegularExpressionMatch match = RX::Mdx::inlineScriptRe.match( linkTxt );
      if ( linkType.compare( "script" ) == 0 && match.hasMatch() && match.capturedLength() == linkTxt.length() ) {
        articleNewText += linkTxt;
        match = RX::Mdx::closeScriptTagRe.match( article, linkPos );
        if ( match.hasMatch() ) {
          articleNewText += article.mid( linkPos, match.capturedEnd() - linkPos );
          linkPos = match.capturedEnd();
        }
        continue;
      }
      else {
        match = RX::Mdx::srcRe.match( linkTxt );
        if ( match.hasMatch() ) {
          QString newText;
          QString scheme;
          if ( linkType.compare( "source" ) == 0 ) {
            scheme = "gdvideo://";
          }
          else {
            scheme = "bres://";
          }
          newText =
            match.captured( 1 ) + match.captured( 2 ) + scheme + id + "/" + match.captured( 3 ) + match.captured( 2 );

          newLink = linkTxt.replace( match.capturedStart(), match.capturedLength(), newText );
        }
        else {
          newLink = linkTxt.replace( RX::Mdx::srcRe2, R"(\1"bres://)" + id + R"(/\2")" );
        }
      }

      if ( linkType.compare( "img" ) == 0 || linkType.compare( "source" ) == 0 ) {
        match = RX::Mdx::srcset.match( newLink );
        if ( match.hasMatch() ) {
          auto srcsetOriginalText = match.captured( "text" );

          QStringList srcsetNewText = {};
          QStringList chunks = srcsetOriginalText.split( QRegularExpression( R"(,\s+)" ), Qt::SkipEmptyParts );

          for ( QString chunk : chunks ) {
            chunk = chunk.trimmed();
            if ( chunk.isEmpty() ) {
              continue;
            }

            QString url, desc;
            int firstSpace = chunk.indexOf( QRegularExpression( R"(\s)" ) );
            if ( firstSpace != -1 ) {
              url  = chunk.left( firstSpace );
              desc = chunk.mid( firstSpace ).trimmed();
            }
            else {
              url = chunk;
            }

            if ( !url.isEmpty() ) {
              if ( url.startsWith( "//" ) ) {
                url = "https:" + url;
                srcsetNewText.append( desc.isEmpty() ? url : url + " " + desc );
              }
              else if ( !url.contains( "//" ) && !url.contains( ":" ) ) {
                QString converted = QString( R"(bres://%1/%2)" ).arg( id, url );
                srcsetNewText.append( desc.isEmpty() ? converted : converted + " " + desc );
              }
              else {
                srcsetNewText.append( desc.isEmpty() ? url : url + " " + desc );
              }
            }
          }
          if ( !srcsetNewText.isEmpty() ) {
            newLink.replace( match.capturedStart(),
                             match.capturedLength(),
                             match.captured( "before" ) % srcsetNewText.join( ',' ) % match.captured( "after" ) );
          }
        }
      }

      if ( linkType.compare( "object" ) == 0 ) {
        match = RX::Mdx::objectdata.match( newLink );
        if ( match.hasMatch() ) {
          auto srcsetOriginalText = match.captured( "text" );
          QString srcsetNewText;
          if ( !srcsetOriginalText.contains( "//" ) && !srcsetOriginalText.contains( ":" ) ) {
            srcsetNewText = QString( R"(bres://%1/%2)" ).arg( id, srcsetOriginalText );
          }
          else {
            srcsetNewText = srcsetOriginalText;
          }

          newLink.replace( match.capturedStart(),
                           match.capturedLength(),
                           match.captured( "before" ) % srcsetNewText % match.captured( "after" ) );
        }
      }
    }

    if ( !newLink.isEmpty() ) {
      articleNewText += newLink;
    }
    else {
      articleNewText += allLinksMatch.captured();
    }
  }
  if ( linkPos ) {
    articleNewText += article.mid( linkPos );
    article = articleNewText;
  }
}

QString MdxDictionary::isolateStyleCssInHtml( const QString & description )
{
  if ( description.contains( "<style", Qt::CaseInsensitive ) ) {
    QRegularExpression styleRegex( "<style[^>]*>(.*?)</style>",
                                   QRegularExpression::CaseInsensitiveOption
                                     | QRegularExpression::DotMatchesEverythingOption );
    QRegularExpressionMatchIterator it = styleRegex.globalMatch( description );

    QString newDescription;
    int lastPos = 0;

    while ( it.hasNext() ) {
      QRegularExpressionMatch match = it.next();
      QString styleContent          = match.captured( 1 );

      isolateCSS( styleContent, QString() );

      QString newStyleTag = "<style>" + styleContent + "</style>";

      newDescription += description.mid( lastPos, match.capturedStart() - lastPos );
      newDescription += newStyleTag;

      lastPos = match.capturedEnd();
    }

    newDescription += description.mid( lastPos );

    return newDescription;
  }

  return description;
}

void MdxDictionary::replaceStyleInHtml( QString & id, QString & article )
{
  QString articleNewText;
  int linkPos                        = 0;
  QRegularExpressionMatchIterator it = RX::Mdx::styleElement.globalMatch( article );
  while ( it.hasNext() ) {
    QRegularExpressionMatch allLinksMatch = it.next();

    if ( allLinksMatch.capturedEnd() < linkPos ) {
      continue;
    }

    articleNewText += article.mid( linkPos, allLinksMatch.capturedStart() - linkPos );
    linkPos = allLinksMatch.capturedEnd();

    articleNewText += allLinksMatch.captured( 1 );

    auto style = allLinksMatch.captured( 2 );
    replaceFontLinks( id, style );
    articleNewText += style;
    articleNewText += allLinksMatch.captured( 3 );
  }
  if ( linkPos ) {
    articleNewText += article.mid( linkPos );
    article = articleNewText;
  }
}

void MdxDictionary::replaceFontLinks( QString & id, QString & article )
{
  QString articleNewText;
  int linkPos                        = 0;
  QRegularExpressionMatchIterator it = RX::Mdx::fontFace.globalMatch( article );
  while ( it.hasNext() ) {
    QRegularExpressionMatch allLinksMatch = it.next();

    if ( allLinksMatch.capturedEnd() < linkPos ) {
      continue;
    }

    articleNewText += article.mid( linkPos, allLinksMatch.capturedStart() - linkPos );
    linkPos          = allLinksMatch.capturedEnd();
    QString linkTxt  = allLinksMatch.captured();
    QString linkType = allLinksMatch.captured( 1 );
    QString newLink  = linkTxt;

    if ( linkType.startsWith( "//" ) ) {
      newLink = QString( "url(\"https:%1\")" ).arg( linkType );
    }
    else if ( !linkType.contains( ":" ) ) {
      newLink = QString( "url(\"bres://%1/%2\")" ).arg( id, linkType );
    }
    articleNewText += newLink;
  }
  if ( linkPos ) {
    articleNewText += article.mid( linkPos );
    article = articleNewText;
  }
}


QString MdxDictionary::getCachedFileName( QString filename )
{
  QDir dir;
  QFileInfo info( cacheDirName );
  if ( !info.exists() || !info.isDir() ) {
    if ( !dir.mkdir( cacheDirName ) ) {
      qWarning( "Mdx: can't create cache directory \"%s\"", cacheDirName.toUtf8().data() );
      return QString();
    }
  }

  QString name = filename;
  name.replace( '/', '\\' );
  QStringList list = name.split( '\\' );
  int subFolders   = list.size() - 1;
  if ( subFolders > 0 ) {
    QString dirName = cacheDirName;
    for ( int i = 0; i < subFolders; i++ ) {
      dirName += QDir::separator() + list.at( i );
      QFileInfo dirInfo( dirName );
      if ( !dirInfo.exists() ) {
        if ( !dir.mkdir( dirName ) ) {
          qWarning( "Mdx: can't create cache directory \"%s\"", dirName.toUtf8().data() );
          return QString();
        }
      }
    }
  }

  QString fullName = cacheDirName + QDir::separator() + filename;

  info.setFile( fullName );
  if ( info.exists() ) {
    return fullName;
  }
  QFile f( fullName );
  if ( !f.open( QFile::WriteOnly ) ) {
    qWarning( R"(Mdx: file "%s" creating error: "%s")", fullName.toUtf8().data(), f.errorString().toUtf8().data() );
    return QString();
  }
  QString resourceName = filename;
  vector< char > data;

  set< QString, std::less<> > resourceIncluded;

  for ( ;; ) {
    if ( !resourceIncluded.insert( resourceName ).second ) {
      continue;
    }

    loadResourceFile( resourceName, data );

    if ( static const char pattern[ 16 ] =
           { '@', '\0', '@', '\0', '@', '\0', 'L', '\0', 'I', '\0', 'N', '\0', 'K', '\0', '=', '\0' };
         data.size() > sizeof( pattern ) && memcmp( &data.front(), pattern, sizeof( pattern ) ) == 0 ) {
      data.push_back( '\0' );
      data.push_back( '\0' );
      QString target =
        MdictParser::toUtf16( "UTF-16LE", &data.front() + sizeof( pattern ), data.size() - sizeof( pattern ) );
      resourceName = target.trimmed();
      continue;
    }
    break;
  }

  qint64 n = 0;
  if ( !data.empty() ) {
    n = f.write( data.data(), data.size() );
  }

  f.close();

  if ( n < (qint64)data.size() ) {
    qWarning( R"(Mdx: file "%s" writing error: "%s")", fullName.toUtf8().data(), f.errorString().toUtf8().data() );
    return QString();
  }
  return fullName;
}

void MdxDictionary::loadResourceFile( const QString & resourceName, vector< char > & data )
{
  QString newResourceName = resourceName;
  string u8ResourceName   = Text::toUtf8( resourceName );

  newResourceName.replace( u'/', u'\\' );
  if ( newResourceName[ 0 ] == u'.' ) {
    newResourceName.remove( 0, 1 );
  }
  if ( newResourceName[ 0 ] != u'\\' ) {
    newResourceName.prepend( u'\\' );
  }
  if ( string fn = getContainingFolder().toStdString() + Utils::Fs::separator() + u8ResourceName;
       Utils::Fs::exists( fn ) ) {
    File::loadFromFile( fn, data );
    return;
  }
  for ( const auto & mddResource : mddResources ) {
    if ( mddResource->loadFile( newResourceName, data ) ) {
      break;
    }
  }
}

static void addEntryToIndex( const QString & word, uint32_t offset, IndexedWords & indexedWords )
{
  QString wordTrimmed = word.trimmed();
  indexedWords.addWord( wordTrimmed, offset );
}

static void addEntryToIndexSingle( const QString & word, uint32_t offset, IndexedWords & indexedWords )
{
  QString wordTrimmed = word.trimmed();
  indexedWords.addSingleWord( wordTrimmed, offset );
}

class ArticleHandler: public MdictParser::RecordHandler
{
public:
  ArticleHandler( ChunkedStorage::Writer & chunks, IndexedWords & indexedWords ):
    chunks( chunks ),
    indexedWords( indexedWords )
  {
  }

  void handleRecord( const QString & headWord, const MdictParser::RecordInfo & recordInfo ) override
  {
    uint32_t articleAddress = chunks.startNewBlock();
    chunks.addToBlock( &recordInfo, sizeof( recordInfo ) );
    addEntryToIndex( headWord, articleAddress, indexedWords );
  }

private:
  ChunkedStorage::Writer & chunks;
  IndexedWords & indexedWords;
};

class ResourceHandler: public MdictParser::RecordHandler
{
public:
  ResourceHandler( ChunkedStorage::Writer & chunks, IndexedWords & indexedWords ):
    chunks( chunks ),
    indexedWords( indexedWords )
  {
  }

  void handleRecord( const QString & fileName, const MdictParser::RecordInfo & recordInfo ) override
  {
    uint32_t resourceInfoAddress = chunks.startNewBlock();
    chunks.addToBlock( &recordInfo, sizeof( recordInfo ) );
    addEntryToIndexSingle( fileName, resourceInfoAddress, indexedWords );
  }

private:
  ChunkedStorage::Writer & chunks;
  IndexedWords & indexedWords;
};


static bool indexIsOldOrBad( const vector< string > & dictFiles, const string & indexFile )
{
  File::Index idx( indexFile, QIODevice::ReadOnly );
  IdxHeader header;

  if ( idx.readRecords( &header, sizeof( header ), 1 ) != 1 || header.signature != kSignature
       || header.formatVersion != kCurrentFormatVersion || header.parserVersion != MdictParser::kParserVersion
       || header.foldingVersion != Folding::Version ) {
    return true;
  }

  qint64 lastModified = 0;
  for ( const auto & dictionaryFile : dictFiles ) {
    QFileInfo fileInfo( QString::fromUtf8( dictionaryFile.c_str() ) );
    if ( fileInfo.exists() ) {
      qint64 ts = fileInfo.lastModified().toMSecsSinceEpoch();
      if ( ts > lastModified ) {
        lastModified = ts;
      }
    }
  }

  return header.sourceLastModified != (uint64_t)lastModified;
}

static void findResourceFiles( const string & mdx, vector< string > & dictFiles )
{
  string base( mdx, 0, mdx.size() - 4 );
  string resFile;
  if ( File::tryPossibleName( base + ".mdd", resFile ) ) {
    dictFiles.push_back( resFile );
    QString baseU8 = QString::fromUtf8( base.c_str() );
    int vol        = 1;
    while ( File::tryPossibleName( string( QString( "%1.%2.mdd" ).arg( baseU8 ).arg( vol ).toUtf8().constBegin() ),
                                   resFile ) ) {
      dictFiles.push_back( resFile );
      vol++;
    }
  }
}

vector< sptr< Dictionary::Class > > makeDictionaries( const vector< string > & fileNames,
                                                      const string & indicesDir,
                                                      Dictionary::Initializing & initializing )
{
  vector< sptr< Dictionary::Class > > dictionaries;

  for ( const auto & fileName : fileNames ) {
    if ( !Utils::endsWithIgnoreCase( fileName, ".mdx" ) ) {
      continue;
    }

    vector< string > dictFiles( 1, fileName );
    findResourceFiles( fileName, dictFiles );

    initializing.loadingDictionary( fileName );

    string dictId    = Dictionary::makeDictionaryId( dictFiles );
    string indexFile = indicesDir + dictId;

    if ( Dictionary::needToRebuildIndex( dictFiles, indexFile ) || indexIsOldOrBad( dictFiles, indexFile ) ) {

      qDebug( "MDict: Building the index for dictionary: %s", fileName.c_str() );

      MdictParser parser;
      list< sptr< MdictParser > > mddParsers;

      if ( !parser.open( fileName.c_str() ) ) {
        continue;
      }

      string title = parser.title().toStdString();
      initializing.indexingDictionary( title );

      for ( vector< string >::const_iterator mddIter = dictFiles.begin() + 1; mddIter != dictFiles.end(); ++mddIter ) {
        if ( Utils::Fs::exists( *mddIter ) ) {
          sptr< MdictParser > mddParser = std::make_shared< MdictParser >();
          if ( !mddParser->open( mddIter->c_str() ) ) {
            qWarning( "Broken mdd (resource) file: %s", mddIter->c_str() );
            continue;
          }
          mddParsers.push_back( mddParser );
        }
      }

      File::Index idx( indexFile, QIODevice::WriteOnly );
      IdxHeader idxHeader;
      memset( &idxHeader, 0, sizeof( idxHeader ) );
      idx.write( idxHeader );

      idx.write< uint32_t >( title.size() );
      idx.write( title.data(), title.size() );

      {
        string encoding = parser.encoding().toStdString();
        idx.write< uint32_t >( encoding.size() );
        idx.write( encoding.data(), encoding.size() );
      }

      IndexedWords indexedWords;
      ChunkedStorage::Writer chunks( idx );

      idxHeader.isRightToLeft = parser.isRightToLeft();

      {
        string description           = parser.description().toStdString();
        idxHeader.descriptionAddress = chunks.startNewBlock();
        chunks.addToBlock( description.c_str(), description.size() + 1 );
        idxHeader.descriptionSize = description.size() + 1;
      }

      ArticleHandler articleHandler( chunks, indexedWords );
      MdictParser::HeadWordIndex headWordIndex;

      while ( parser.readNextHeadWordIndex( headWordIndex ) ) {
        parser.readRecordBlock( headWordIndex, articleHandler );
      }

      vector< sptr< IndexedWords > > mddIndices;
      vector< string > mddFileNames;
      while ( !mddParsers.empty() ) {
        sptr< MdictParser > mddParser        = mddParsers.front();
        sptr< IndexedWords > mddIndexedWords = std::make_shared< IndexedWords >();
        MdictParser::HeadWordIndex resourcesIndex;
        ResourceHandler resourceHandler( chunks, *mddIndexedWords );

        while ( mddParser->readNextHeadWordIndex( headWordIndex ) ) {
          resourcesIndex.insert( resourcesIndex.end(), headWordIndex.begin(), headWordIndex.end() );
        }
        mddParser->readRecordBlock( resourcesIndex, resourceHandler );

        mddIndices.push_back( mddIndexedWords );
        QFileInfo fi( mddParser->filename() );
        mddFileNames.push_back( fi.fileName().toStdString() );
        mddParsers.pop_front();
      }

      idxHeader.chunksOffset = chunks.finish();

      qDebug( "Writing index..." );

      IndexInfo idxInfo               = BtreeIndexing::buildIndex( indexedWords, idx );
      idxHeader.indexBtreeMaxElements = idxInfo.btreeMaxElements;
      idxHeader.indexRootOffset       = idxInfo.rootOffset;

      {
        const MdictParser::StyleSheets & styleSheets = parser.styleSheets();
        idxHeader.styleSheetAddress                  = idx.tell();
        idxHeader.styleSheetCount                    = styleSheets.size();

        for ( const auto & [ key, value ] : styleSheets ) {
          const string styleBegin( value.first.toStdString() );
          const string styleEnd( value.second.toStdString() );

          idx.write< qint32 >( key );
          idx.write< quint32 >( (quint32)styleBegin.size() + 1 );
          idx.write( styleBegin.c_str(), styleBegin.size() + 1 );
          idx.write< quint32 >( (quint32)styleEnd.size() + 1 );
          idx.write( styleEnd.c_str(), styleEnd.size() + 1 );
        }
      }

      auto langs = LangCoder::findLangIdPairFromPath( fileName );

      if ( langs.first == 0 || langs.second == 0 ) {
        langs = LangCoder::findLangIdPairFromName( parser.title() );
      }

      idxHeader.langFrom = langs.first;
      idxHeader.langTo   = langs.second;

      vector< IndexInfo > mddIndexInfos;
      for ( const auto & mddIndice : mddIndices ) {
        const IndexInfo resourceIdxInfo = BtreeIndexing::buildIndex( *mddIndice, idx );
        mddIndexInfos.push_back( resourceIdxInfo );
      }

      idxHeader.mddIndexInfosOffset = idx.tell();
      idxHeader.mddIndexInfosCount  = mddIndexInfos.size();
      for ( uint32_t mi = 0; mi < mddIndexInfos.size(); mi++ ) {
        const string & mddfile = mddFileNames[ mi ];

        idx.write< quint32 >( (quint32)mddfile.size() + 1 );
        idx.write( mddfile.c_str(), mddfile.size() + 1 );
        idx.write< uint32_t >( mddIndexInfos[ mi ].btreeMaxElements );
        idx.write< uint32_t >( mddIndexInfos[ mi ].rootOffset );
      }

      idxHeader.signature      = kSignature;
      idxHeader.formatVersion  = kCurrentFormatVersion;
      idxHeader.parserVersion  = MdictParser::kParserVersion;
      idxHeader.foldingVersion = Folding::Version;
      idxHeader.articleCount   = parser.wordCount();
      idxHeader.wordCount      = parser.wordCount();

      qint64 lastModified = 0;
      for ( const auto & dictionaryFile : dictFiles ) {
        QFileInfo fileInfo( QString::fromUtf8( dictionaryFile.c_str() ) );
        if ( fileInfo.exists() ) {
          qint64 ts = fileInfo.lastModified().toMSecsSinceEpoch();
          if ( ts > lastModified ) {
            lastModified = ts;
          }
        }
      }
      idxHeader.sourceLastModified = (uint64_t)lastModified;

      idx.rewind();
      idx.write( &idxHeader, sizeof( idxHeader ) );
    }

    dictionaries.push_back( std::make_shared< MdxDictionary >( dictId, indexFile, dictFiles ) );
  }

  return dictionaries;
}

}
