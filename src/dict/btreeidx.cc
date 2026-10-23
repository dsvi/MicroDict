/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "btreeidx.hh"
#include "folding.hh"
#include "text.hh"
#include <string.h>
#include "utils.hh"

#include <QRegularExpression>
#include "wildcard.hh"
#include "globalbroadcaster.hh"

#include <QtConcurrentRun>
#include <zlib.h>

namespace BtreeIndexing {

using std::pair;

enum {
  BtreeMinElements = 64,
  BtreeMaxElements = 8192
};

BtreeIndex::BtreeIndex():
  idxFile( nullptr ),
  rootNodeLoaded( false )
{
}

BtreeDictionary::BtreeDictionary( const string & id, const vector< string > & dictionaryFiles ):
  Dictionary::Class( id, dictionaryFiles )
{
}

const string & BtreeDictionary::ensureInitDone()
{
  static string empty;

  return empty;
}

void BtreeIndex::openIndex( const IndexInfo & indexInfo, File::Index & file, QMutex & mutex )
{
  indexNodeSize = indexInfo.btreeMaxElements;
  rootOffset    = indexInfo.rootOffset;

  idxFile      = &file;
  idxFileMutex = &mutex;

  rootNodeLoaded = false;
  rootNode.clear();
}

vector< WordArticleLink >
BtreeIndex::findArticles( const QString & search_word, bool ignoreDiacritics, uint32_t maxMatchCount )
{
  QString word = Text::removeTrailingZero( search_word );
  vector< WordArticleLink > result;

  try {
    QString folded = Folding::apply( word );
    if ( folded.isEmpty() ) {
      folded = Folding::applyWhitespaceOnly( word );
    }

    bool exactMatch;

    vector< char > leaf;
    uint32_t nextLeaf;

    const char * leafEnd;

    const char * chainOffset = findChainOffsetExactOrPrefix( folded, exactMatch, leaf, nextLeaf, leafEnd );

    if ( chainOffset && exactMatch ) {
      result = readChain( chainOffset, maxMatchCount );

      antialias( word, result, ignoreDiacritics );
    }
  }
  catch ( std::exception & e ) {
    qWarning( "Articles searching failed, error: %s", e.what() );
    result.clear();
  }
  catch ( ... ) {
    qWarning( "Articles searching failed" );
    result.clear();
  }

  return result;
}


BtreeWordSearchRequest::BtreeWordSearchRequest( BtreeDictionary & dict_,
                                                const QString & str_,
                                                unsigned minLength_,
                                                int maxSuffixVariation_,
                                                bool allowMiddleMatches_,
                                                unsigned long maxResults_,
                                                bool startRunnable ):
  dict( dict_ ),
  str( str_ ),
  maxResults( maxResults_ ),
  minLength( minLength_ ),
  maxSuffixVariation( maxSuffixVariation_ ),
  allowMiddleMatches( allowMiddleMatches_ )
{
  if ( startRunnable ) {
    f = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }
}

void BtreeWordSearchRequest::findMatches()
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

  QRegularExpression regexp;

  bool useWildcards = false;
  if ( allowMiddleMatches ) {
    useWildcards = str.contains( '*' ) || str.contains( '?' ) || str.contains( '[' ) || str.contains( ']' );
  }

  QString folded = Folding::apply( str );

  int minMatchLength = 0;

  if ( useWildcards ) {
    regexp.setPattern( wildcardsToRegexp( Folding::applyDiacriticsOnly( Folding::applySimpleCaseOnly( str ) ) ) );
    if ( !regexp.isValid() ) {
      regexp.setPattern( QRegularExpression::escape( regexp.pattern() ) );
    }
    regexp.setPatternOptions( QRegularExpression::CaseInsensitiveOption );

    bool bNoLetters = folded.isEmpty();
    QString foldedWithWildcards;

    if ( bNoLetters ) {
      foldedWithWildcards = Folding::applyWhitespaceOnly( str );
    }
    else {
      foldedWithWildcards = Folding::apply( str, useWildcards );
    }

    bool insideSet = false;
    bool escaped   = false;
    for ( const QChar & qch : foldedWithWildcards ) {
      const char32_t ch = qch.unicode();
      if ( ch == '\\' && !escaped ) {
        escaped = true;
        continue;
      }

      if ( ch == ']' && !escaped ) {
        insideSet = false;
        continue;
      }

      if ( insideSet ) {
        escaped = false;
        continue;
      }

      if ( ch == '[' && !escaped ) {
        minMatchLength += 1;
        insideSet = true;
        continue;
      }

      if ( ch == '*' && !escaped ) {
        continue;
      }

      escaped = false;
      minMatchLength += 1;
    }

    folded.clear();
    folded.reserve( foldedWithWildcards.size() );
    escaped = false;
    for ( const QChar & qch : foldedWithWildcards ) {
      const char32_t ch = qch.unicode();
      if ( escaped ) {
        if ( bNoLetters || ( ch != '*' && ch != '?' && ch != '[' && ch != ']' ) ) {
          folded.push_back( qch );
        }
        escaped = false;
        continue;
      }

      if ( ch == '\\' ) {
        if ( bNoLetters || folded.isEmpty() ) {
          escaped = true;
          continue;
        }
        else {
          break;
        }
      }

      if ( ch == '*' || ch == '?' || ch == '[' || ch == ']' ) {
        break;
      }

      folded.push_back( qch );
    }
  }
  else {
    if ( folded.isEmpty() ) {
      folded = Folding::applyWhitespaceOnly( str );
    }
  }

  int initialFoldedSize = folded.size();

  int charsLeftToChop = 0;

  if ( maxSuffixVariation >= 0 ) {
    charsLeftToChop = initialFoldedSize - (int)minLength;

    if ( charsLeftToChop < 0 ) {
      charsLeftToChop = 0;
    }
    else if ( charsLeftToChop > maxSuffixVariation ) {
      charsLeftToChop = maxSuffixVariation;
    }
  }

  try {
    for ( ;; ) {
      bool exactMatch;
      vector< char > leaf;
      uint32_t nextLeaf;
      const char * leafEnd;

      const char * chainOffset = dict.findChainOffsetExactOrPrefix( folded, exactMatch, leaf, nextLeaf, leafEnd );

      if ( chainOffset ) {
        for ( ;; ) {
          if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
            break;
          }

          vector< WordArticleLink > chain = dict.readChain( chainOffset );

          QString chainHead = Text::toQString( chain[ 0 ].word );

          QString resultFolded = Folding::apply( chainHead );
          if ( resultFolded.isEmpty() ) {
            resultFolded = Folding::applyWhitespaceOnly( chainHead );
          }

          if ( ( useWildcards && folded.isEmpty() )
               || ( resultFolded.size() >= folded.size() && resultFolded.startsWith( folded ) ) ) {

            for ( auto & x : chain ) {
              if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
                break;
              }
              if ( useWildcards ) {
                QString word   = Text::toQString( x.prefix + x.word );
                QString result = Folding::applyDiacriticsOnly( word );
                if ( result.size() >= minMatchLength ) {
                  QRegularExpressionMatch match = regexp.match( result );
                  if ( match.hasMatch() && match.capturedStart() == 0 ) {
                    addMatch( word );
                  }
                }
              }
              else {
                if ( ( allowMiddleMatches || Folding::apply( Text::toQString( x.prefix ) ).isEmpty() )
                     && ( maxSuffixVariation < 0
                          || (int)resultFolded.size() - initialFoldedSize <= maxSuffixVariation ) ) {
                  addMatch( Text::toQString( x.prefix + x.word ) );
                }
              }
              if ( matches.size() >= maxResults ) {
                break;
              }
            }

            if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
              break;
            }

            if ( matches.size() >= maxResults ) {
              break;
            }
          }
          else {
            break;
          }

          if ( chainOffset >= leafEnd ) {

            if ( nextLeaf ) {
              QMutexLocker _( dict.idxFileMutex );

              dict.readNode( nextLeaf, leaf );
              leafEnd = &leaf.front() + leaf.size();

              nextLeaf    = dict.idxFile->read< uint32_t >();
              chainOffset = &leaf.front() + sizeof( uint32_t );

              uint32_t leafEntries = *(uint32_t *)&leaf.front();

              if ( leafEntries == 0xffffFFFF ) {
                exit( 1 );
              }
            }
            else {
              break;
            }
          }
        }
      }

      if ( charsLeftToChop && !Utils::AtomicInt::loadAcquire( isCancelled ) ) {
        --charsLeftToChop;
        folded.resize( folded.size() - 1 );
      }
      else {
        break;
      }
    }
  }
  catch ( std::exception & e ) {
    qWarning( "Index searching failed: \"%s\", error: %s", dict.getName().c_str(), e.what() );
  }
  catch ( ... ) {
    qWarning( "Index searching failed: \"%s\"", dict.getName().c_str() );
  }
}

void BtreeWordSearchRequest::run()
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

  findMatches();

  finish();
}

BtreeWordSearchRequest::~BtreeWordSearchRequest()
{
  isCancelled.ref();
  f.waitForFinished();
}

sptr< Dictionary::WordSearchRequest > BtreeDictionary::prefixMatch( const QString & str, unsigned long maxResults )

{
  return std::make_shared< BtreeWordSearchRequest >( *this, str, 0, -1, true, maxResults );
}

sptr< Dictionary::WordSearchRequest > BtreeDictionary::stemmedMatch( const QString & str,
                                                                     unsigned minLength,
                                                                     unsigned maxSuffixVariation,
                                                                     unsigned long maxResults )

{
  return std::make_shared< BtreeWordSearchRequest >( *this,
                                                     str,
                                                     minLength,
                                                     (int)maxSuffixVariation,
                                                     false,
                                                     maxResults );
}

void BtreeIndex::readNode( uint32_t offset, vector< char > & out )
{
  idxFile->seek( offset );

  uint32_t uncompressedSize = idxFile->read< uint32_t >();
  uint32_t compressedSize   = idxFile->read< uint32_t >();

  out.resize( uncompressedSize );

  vector< unsigned char > compressedData( compressedSize );

  idxFile->read( &compressedData.front(), compressedData.size() );

  unsigned long decompressedLength = out.size();

  if ( uncompress( (unsigned char *)&out.front(), &decompressedLength, &compressedData.front(), compressedData.size() )
         != Z_OK
       || decompressedLength != out.size() ) {
    throw exFailedToDecompressNode();
  }
}

namespace {

int compareForIndex( const QString & a, const QString & b )
{
  auto nextScalar = []( const QString & s, qsizetype & i ) -> uint32_t {
    if ( i >= s.size() ) {
      return 0;
    }
    const QChar c = s[ i++ ];
    if ( c.isHighSurrogate() && i < s.size() && s[ i ].isLowSurrogate() ) {
      return QChar::surrogateToUcs4( c, s[ i++ ] );
    }
    return c.unicode();
  };

  for ( qsizetype i = 0, j = 0;; ) {
    const uint32_t ca = nextScalar( a, i );
    const uint32_t cb = nextScalar( b, j );

    if ( ca != cb ) {
      return ca < cb ? -1 : 1;
    }
    if ( ca == 0 ) {
      return 0;
    }
  }
}

}

const char * BtreeIndex::findChainOffsetExactOrPrefix(
  const QString & target, bool & exactMatch, vector< char > & extLeaf, uint32_t & nextLeaf, const char *& leafEnd )
{
  if ( !idxFile ) {
    throw exIndexWasNotOpened();
  }

  QMutexLocker _( idxFileMutex );

  QString w_word;
  exactMatch = false;

  uint32_t currentNodeOffset = rootOffset;

  if ( !rootNodeLoaded ) {
    readNode( rootOffset, rootNode );
    rootNodeLoaded = true;
  }

  const char * leaf = &rootNode.front();
  leafEnd           = leaf + rootNode.size();

  if ( target.isEmpty() ) {
    for ( ;; ) {
      uint32_t leafEntries = *(uint32_t *)leaf;

      if ( leafEntries == 0xffffFFFF ) {
        currentNodeOffset = *( (uint32_t *)leaf + 1 );
        readNode( currentNodeOffset, extLeaf );
        leaf     = &extLeaf.front();
        leafEnd  = leaf + extLeaf.size();
        nextLeaf = idxFile->read< uint32_t >();
      }
      else {
        if ( currentNodeOffset == rootOffset ) {
          nextLeaf = 0;
        }
        if ( !leafEntries ) {
          return nullptr;
        }

        return leaf + sizeof( uint32_t );
      }
    }
  }

  for ( ;; ) {

    uint32_t leafEntries = *(uint32_t *)leaf;

    if ( leafEntries == 0xffffFFFF ) {

      const uint32_t * offsets = (uint32_t *)leaf + 1;

      const char * ptr = leaf + sizeof( uint32_t ) + ( indexNodeSize + 1 ) * sizeof( uint32_t );

      const char * closestString;

      int compareResult;

      const char * window = ptr;
      unsigned windowSize = leafEnd - ptr;

      for ( ;; ) {
        const char * testPoint = window + windowSize / 2;

        closestString = testPoint;

        while ( closestString > ptr && closestString[ -1 ] ) {
          --closestString;
        }

        size_t wordSize = strlen( closestString );

        w_word = Text::toQString( string( closestString, wordSize ) );

        compareResult = compareForIndex( target, w_word );

        if ( !compareResult ) {
          break;
        }
        if ( compareResult < 0 ) {
          windowSize = closestString - window;

          if ( !windowSize ) {
            break;
          }
        }
        else {
          windowSize -= ( closestString - window ) + wordSize + 1;
          window = closestString + wordSize + 1;

          if ( !windowSize ) {
            break;
          }
        }
      }

      unsigned entry = 0;

      for ( const char * next = ptr; next != closestString; next += strlen( next ) + 1, ++entry ) {
        ;
      }

      if ( !compareResult ) {
        currentNodeOffset = offsets[ entry + 1 ];
      }
      if ( compareResult < 0 ) {
        currentNodeOffset = offsets[ entry ];
      }
      else {
        currentNodeOffset = offsets[ entry + 1 ];
      }

      readNode( currentNodeOffset, extLeaf );
      leaf    = &extLeaf.front();
      leafEnd = leaf + extLeaf.size();
    }
    else {

      nextLeaf = ( currentNodeOffset != rootOffset ? idxFile->read< uint32_t >() : 0 );

      if ( !leafEntries ) {
        if ( currentNodeOffset != rootOffset ) {
          throw exCorruptedChainData();
        }
        else {
          return nullptr;
        }
      }

      const char * ptr = leaf + sizeof( uint32_t );

      uint32_t chainSize;

      vector< const char * > chainOffsets( leafEntries );

      {
        const char ** nextOffset = &chainOffsets.front();

        while ( leafEntries-- ) {
          *nextOffset++ = ptr;

          memcpy( &chainSize, ptr, sizeof( uint32_t ) );

          ptr += sizeof( uint32_t ) + chainSize;
        }
      }

      const char ** window = &chainOffsets.front();
      unsigned windowSize  = chainOffsets.size();

      for ( ;; ) {

        const char ** chainToCheck = window + windowSize / 2;
        ptr                        = *chainToCheck;

        memcpy( &chainSize, ptr, sizeof( uint32_t ) );
        ptr += sizeof( uint32_t );

        size_t wordSize = strlen( ptr );

        w_word = Text::toQString( string( ptr, wordSize ) );

        QString foldedWord = Folding::apply( w_word );
        if ( foldedWord.isEmpty() ) {
          foldedWord = Folding::applyWhitespaceOnly( w_word );
        }

        int compareResult = compareForIndex( target, foldedWord );

        if ( !compareResult ) {
          exactMatch = true;

          return ptr - sizeof( uint32_t );
        }
        else if ( compareResult < 0 ) {

          windowSize /= 2;

          if ( !windowSize ) {
            return ptr - sizeof( uint32_t );
          }
        }
        else {

          windowSize -= windowSize / 2 + 1;

          if ( !windowSize ) {
            if ( chainToCheck == &chainOffsets.back() ) {
              if ( nextLeaf ) {
                readNode( nextLeaf, extLeaf );

                leafEnd = &extLeaf.front() + extLeaf.size();

                nextLeaf = idxFile->read< uint32_t >();

                return &extLeaf.front() + sizeof( uint32_t );
              }
              else {
                return nullptr;
              }
            }
            else {
              return chainToCheck[ 1 ];
            }
          }

          window = chainToCheck + 1;
        }
      }
    }
  }
}

vector< WordArticleLink > BtreeIndex::readChain( const char *& ptr, uint32_t maxMatchCount )
{
  uint32_t chainSize;

  memcpy( &chainSize, ptr, sizeof( uint32_t ) );

  ptr += sizeof( uint32_t );

  vector< WordArticleLink > result;

  while ( chainSize && ( maxMatchCount < 0 || result.size() < maxMatchCount ) ) {
    string str = ptr;
    ptr += str.size() + 1;

    string prefix = ptr;
    ptr += prefix.size() + 1;

    uint32_t articleOffset;

    memcpy( &articleOffset, ptr, sizeof( uint32_t ) );

    ptr += sizeof( uint32_t );

    result.emplace_back( str, articleOffset, prefix );

    if ( chainSize < str.size() + 1 + prefix.size() + 1 + sizeof( uint32_t ) ) {
      throw exCorruptedChainData();
    }
    else {
      chainSize -= str.size() + 1 + prefix.size() + 1 + sizeof( uint32_t );
    }
  }

  return result;
}

void BtreeIndex::antialias( const QString & str, vector< WordArticleLink > & chain, bool ignoreDiacritics )
{
  QString caseFolded = Folding::applySimpleCaseOnly( Text::normalize( str ) );
  if ( ignoreDiacritics ) {
    caseFolded = Folding::applyDiacriticsOnly( caseFolded );
  }

  if ( GlobalBroadcaster::instance()->getPreference()->ignorePunctuation ) {
    caseFolded = Folding::trimWhitespaceOrPunct( caseFolded );
  }

  for ( unsigned x = chain.size(); x--; ) {
    QString entry =
      Folding::applySimpleCaseOnly( Text::normalize( Text::toQString( chain[ x ].prefix + chain[ x ].word ) ) );
    if ( ignoreDiacritics ) {
      entry = Folding::applyDiacriticsOnly( entry );
    }

    if ( GlobalBroadcaster::instance()->getPreference()->ignorePunctuation ) {
      entry = Folding::trimWhitespaceOrPunct( entry );
    }

    if ( entry != caseFolded ) {
      chain.erase( chain.begin() + x );
    }
    else if ( !chain[ x ].prefix.empty() ) {
      chain[ x ].word.insert( 0, chain[ x ].prefix );
      chain[ x ].prefix.clear();
    }
  }
}

static uint32_t buildBtreeNode( IndexedWords::const_iterator & nextIndex,
                                size_t indexSize,
                                File::Index & file,
                                size_t maxElements,
                                uint32_t & lastLeafLinkOffset )
{
  vector< unsigned char > uncompressedData;

  bool isLeaf = indexSize <= maxElements;

  if ( isLeaf ) {

    uint32_t totalChainsLength = 0;

    auto nextWord = nextIndex;

    for ( unsigned x = indexSize; x--; ++nextWord ) {
      totalChainsLength += sizeof( uint32_t );

      const vector< WordArticleLink > & chain = nextWord->second;

      for ( const auto & y : chain ) {
        totalChainsLength += y.word.size() + 1 + y.prefix.size() + 1 + sizeof( uint32_t );
      }
    }

    uncompressedData.resize( sizeof( uint32_t ) + totalChainsLength );

    *(uint32_t *)&uncompressedData.front() = indexSize;

    unsigned char * ptr = &uncompressedData.front() + sizeof( uint32_t );

    for ( unsigned x = indexSize; x--; ++nextIndex ) {
      const vector< WordArticleLink > & chain = nextIndex->second;

      unsigned char * saveSizeHere = ptr;

      ptr += sizeof( uint32_t );

      uint32_t size = 0;

      for ( const auto & y : chain ) {
        memcpy( ptr, y.word.c_str(), y.word.size() + 1 );
        ptr += y.word.size() + 1;

        memcpy( ptr, y.prefix.c_str(), y.prefix.size() + 1 );
        ptr += y.prefix.size() + 1;

        memcpy( ptr, &( y.articleOffset ), sizeof( uint32_t ) );
        ptr += sizeof( uint32_t );

        size += y.word.size() + 1 + y.prefix.size() + 1 + sizeof( uint32_t );
      }

      memcpy( saveSizeHere, &size, sizeof( uint32_t ) );
    }
  }
  else {

    uncompressedData.resize( sizeof( uint32_t ) + ( maxElements + 1 ) * sizeof( uint32_t ) );

    *(uint32_t *)&uncompressedData.front() = 0xffffFFFF;

    unsigned prevEntry = 0;

    for ( unsigned x = 0; x < maxElements; ++x ) {
      unsigned curEntry = (uint64_t)indexSize * ( x + 1 ) / ( maxElements + 1 );

      uint32_t offset = buildBtreeNode( nextIndex, curEntry - prevEntry, file, maxElements, lastLeafLinkOffset );

      memcpy( &uncompressedData.front() + sizeof( uint32_t ) + x * sizeof( uint32_t ), &offset, sizeof( uint32_t ) );

      size_t sz = nextIndex->first.size() + 1;

      size_t prevSize = uncompressedData.size();
      uncompressedData.resize( prevSize + sz );

      memcpy( &uncompressedData.front() + prevSize, nextIndex->first.c_str(), sz );

      prevEntry = curEntry;
    }

    uint32_t offset = buildBtreeNode( nextIndex, indexSize - prevEntry, file, maxElements, lastLeafLinkOffset );
    memcpy( &uncompressedData.front() + sizeof( uint32_t ) + maxElements * sizeof( uint32_t ),
            &offset,
            sizeof( offset ) );
  }

  vector< unsigned char > compressedData( compressBound( uncompressedData.size() ) );

  unsigned long compressedSize = compressedData.size();

  if ( compress( &compressedData.front(), &compressedSize, &uncompressedData.front(), uncompressedData.size() )
       != Z_OK ) {
    qFatal( "Failed to compress btree node." );
    abort();
  }

  uint32_t offset = file.tell();

  file.write< uint32_t >( uncompressedData.size() );
  file.write< uint32_t >( compressedSize );
  file.write( &compressedData.front(), compressedSize );

  if ( isLeaf ) {

    file.write( (uint32_t)0 );

    uint32_t here = file.tell();

    if ( lastLeafLinkOffset ) {
      file.seek( lastLeafLinkOffset );
      file.write( offset );
      file.seek( here );
    }

    lastLeafLinkOffset = here - sizeof( uint32_t );
  }

  return offset;
}

void IndexedWords::addWord( const QString & index_word, uint32_t articleOffset, unsigned int maxHeadwordSize )
{
  QString word     = Text::removeTrailingZero( index_word );
  qsizetype length = word.size();

  if ( length > (qsizetype)maxHeadwordSize ) {
    qWarning() << "Abbreviate the too long headword: " << word.left( 30 ) << "size:" << length;

    qsizetype cutPos = maxHeadwordSize;
    while ( cutPos > 0 && !Folding::isWhitespace( word[ cutPos ].unicode() ) ) {
      --cutPos;
    }
    if ( cutPos == 0 ) {
      cutPos = maxHeadwordSize;
    }
    word.truncate( cutPos );

    length = word.size();
  }

  qsizetype wordBegin = 0;
  qsizetype wordEnd   = length;

  while ( wordBegin < wordEnd && Folding::isWhitespace( word[ wordBegin ].unicode() ) ) {
    ++wordBegin;
    --wordEnd;
  }

  while ( wordEnd > wordBegin && Folding::isWhitespace( word[ wordEnd - 1 ].unicode() ) ) {
    --wordEnd;
  }

  qsizetype nextChar = wordBegin;

  int wordsAdded = 0;

  for ( ;; ) {
    for ( ;; ++nextChar ) {
      if ( nextChar == length ) {
        if ( wordsAdded == 0 ) {
          QString folded = Folding::applyWhitespaceOnly( word.mid( wordBegin, wordEnd - wordBegin ) );
          if ( !folded.isEmpty() ) {
            auto i = insert( { Text::toUtf8( folded ), vector< WordArticleLink >() } ).first;

            string utfWord = Text::toUtf8( word.mid( wordBegin, wordEnd - wordBegin ) );
            string utfPrefix;
            i->second.emplace_back( utfWord, articleOffset, utfPrefix );
          }
        }
        return;
      }

      if ( !Folding::isWhitespace( word[ nextChar ].unicode() ) && !Folding::isPunct( word[ nextChar ].unicode() ) ) {
        break;
      }
    }

    QString folded = Folding::apply( word.mid( nextChar ) );
    auto name      = Text::toUtf8( folded );

    auto i = insert( { std::move( name ), vector< WordArticleLink >() } ).first;

    if ( ( i->second.size() < 1024 ) || ( nextChar == wordBegin ) ) {
      string utfWord   = Text::toUtf8( word.mid( nextChar, wordEnd - nextChar ) );
      string utfPrefix = Text::toUtf8( word.mid( wordBegin, nextChar - wordBegin ) );

      if ( utfPrefix.empty() ) {
        auto it = std::find_if( i->second.begin(), i->second.end(), []( const WordArticleLink & link ) {
          return !link.prefix.empty();
        } );
        i->second.emplace( it, std::move( utfWord ), articleOffset, std::move( utfPrefix ) );
      }
      else {
        i->second.emplace_back( std::move( utfWord ), articleOffset, std::move( utfPrefix ) );
      }
      if ( i->second.size() * 1.0 / i->second.capacity() > 0.75 ) {
        i->second.reserve( i->second.capacity() * 2 );
      }
    }

    wordsAdded += 1;

    for ( ++nextChar;; ++nextChar ) {
      if ( nextChar == length ) {
        return;
      }

      if ( Folding::isWhitespace( word[ nextChar ].unicode() ) || Folding::isPunct( word[ nextChar ].unicode() ) ) {
        break;
      }
    }
  }
}

void IndexedWords::addSingleWord( const QString & index_word, uint32_t articleOffset )
{
  const QString word = Text::removeTrailingZero( index_word );
  QString folded     = Folding::apply( word );
  if ( folded.isEmpty() ) {
    folded = Folding::applyWhitespaceOnly( word );
  }
  operator[]( Text::toUtf8( folded ) ).emplace_back( Text::toUtf8( word ), articleOffset );
}

IndexInfo buildIndex( const IndexedWords & indexedWords, File::Index & file )
{
  size_t indexSize = indexedWords.size();
  auto nextIndex   = indexedWords.begin();

  while ( indexSize && nextIndex->first.empty() ) {
    indexSize--;
    ++nextIndex;
  }

  size_t btreeMaxElements = ( (size_t)sqrt( (double)indexSize ) ) + 1;

  if ( btreeMaxElements < BtreeMinElements ) {
    btreeMaxElements = BtreeMinElements;
  }
  else if ( btreeMaxElements > BtreeMaxElements ) {
    btreeMaxElements = BtreeMaxElements;
  }

  qDebug( "Building a tree of %u elements", (unsigned)btreeMaxElements );


  uint32_t lastLeafOffset = 0;

  uint32_t rootOffset = buildBtreeNode( nextIndex, indexSize, file, btreeMaxElements, lastLeafOffset );

  return IndexInfo( btreeMaxElements, rootOffset );
}

void BtreeIndex::getAllHeadwords( QSet< QString > & headwords )
{
  if ( !idxFile ) {
    throw exIndexWasNotOpened();
  }

  findArticleLinks( nullptr, nullptr, &headwords );
}

void BtreeIndex::findAllArticleLinks( QList< WordArticleLink > & articleLinks )
{
  if ( !idxFile ) {
    throw exIndexWasNotOpened();
  }

  QSet< uint32_t > offsets;

  findArticleLinks( &articleLinks, &offsets, nullptr );
}

void BtreeIndex::findArticleLinks( QList< WordArticleLink > * articleLinks,
                                   QSet< uint32_t > * offsets,
                                   QSet< QString > * headwords,
                                   QAtomicInt * isCancelled )
{
  uint32_t currentNodeOffset = rootOffset;
  uint32_t nextLeaf          = 0;
  uint32_t leafEntries;

  QMutexLocker _( idxFileMutex );

  if ( !rootNodeLoaded ) {
    readNode( rootOffset, rootNode );
    rootNodeLoaded = true;
  }

  const char * leaf     = &rootNode.front();
  const char * leafEnd  = leaf + rootNode.size();
  const char * chainPtr = nullptr;

  vector< char > extLeaf;

  for ( ;; ) {
    leafEntries = *(uint32_t *)leaf;

    if ( isCancelled && Utils::AtomicInt::loadAcquire( *isCancelled ) ) {
      return;
    }

    if ( leafEntries == 0xffffFFFF ) {
      currentNodeOffset = *( (uint32_t *)leaf + 1 );
      readNode( currentNodeOffset, extLeaf );
      leaf     = &extLeaf.front();
      leafEnd  = leaf + extLeaf.size();
      nextLeaf = idxFile->read< uint32_t >();
    }
    else {
      chainPtr = leaf + sizeof( uint32_t );
      break;
    }
  }

  if ( !leafEntries ) {
    if ( currentNodeOffset != rootOffset ) {
      throw exCorruptedChainData();
    }
    else {
      return;
    }
  }

  for ( ;; ) {
    vector< WordArticleLink > result = readChain( chainPtr );

    for ( auto & i : result ) {
      if ( isCancelled && Utils::AtomicInt::loadAcquire( *isCancelled ) ) {
        return;
      }

      if ( headwords ) {
        headwords->insert( QString::fromUtf8( ( i.prefix + i.word ).c_str() ) );
      }

      if ( offsets && offsets->contains( i.articleOffset ) ) {
        continue;
      }

      if ( offsets ) {
        offsets->insert( i.articleOffset );
      }

      if ( articleLinks ) {
        articleLinks->push_back( WordArticleLink( i.prefix + i.word, i.articleOffset ) );
      }
    }

    if ( chainPtr >= leafEnd ) {

      if ( nextLeaf ) {
        readNode( nextLeaf, extLeaf );
        leaf    = &extLeaf.front();
        leafEnd = leaf + extLeaf.size();

        nextLeaf = idxFile->read< uint32_t >();
        chainPtr = leaf + sizeof( uint32_t );

        leafEntries = *(uint32_t *)leaf;

        if ( leafEntries == 0xffffFFFF ) {
          throw exCorruptedChainData();
        }
      }
      else {
        break;
      }
    }
  }
}

void BtreeIndex::findHeadWords( QList< uint32_t > offsets, int & index, QSet< QString > * headwords, uint32_t length )
{
  for ( auto begin = offsets.begin() + index; begin != offsets.end(); begin++ ) {
    findSingleNodeHeadwords( *begin, headwords );
    index++;

    if ( headwords->size() >= length ) {
      break;
    }
  }
}

void BtreeIndex::findSingleNodeHeadwords( uint32_t offsets, QSet< QString > * headwords )
{
  uint32_t currentNodeOffset = offsets;

  QMutexLocker _( idxFileMutex );

  const char * leaf     = nullptr;
  const char * leafEnd  = nullptr;
  const char * chainPtr = nullptr;

  vector< char > extLeaf;

  readNode( currentNodeOffset, extLeaf );
  leaf    = &extLeaf.front();
  leafEnd = leaf + extLeaf.size();

  chainPtr = leaf + sizeof( uint32_t );

  for ( ;; ) {
    vector< WordArticleLink > result = readChain( chainPtr );

    if ( headwords ) {
      for ( auto & i : result ) {
        if ( i.prefix.empty() ) {
          headwords->insert( QString::fromUtf8( ( i.prefix + i.word ).c_str() ) );
        }
      }
    }

    if ( chainPtr >= leafEnd ) {
      break;
    }
  }
}

QList< uint32_t > BtreeIndex::findNodes()
{
  QMutexLocker _( idxFileMutex );

  if ( !rootNodeLoaded ) {
    readNode( rootOffset, rootNode );
    rootNodeLoaded = true;
  }

  const char * leaf = &rootNode.front();
  QList< uint32_t > leafOffset;

  uint32_t leafEntries;
  leafEntries = *(uint32_t *)leaf;
  if ( leafEntries != 0xffffFFFF ) {
    leafOffset.append( rootOffset );
    return leafOffset;
  }

  uint32_t * offsets = (uint32_t *)leaf + 1;
  uint32_t i         = 0;

  while ( i++ < ( indexNodeSize + 1 ) ) {
    leafOffset.append( *( offsets++ ) );
  }

  return leafOffset;
}

void BtreeIndex::getHeadwordsFromOffsets( QList< uint32_t > & offsets,
                                          QList< QString > & headwords,
                                          QAtomicInt * isCancelled )
{
  uint32_t currentNodeOffset = rootOffset;
  uint32_t nextLeaf          = 0;
  uint32_t leafEntries;

  std::sort( offsets.begin(), offsets.end() );

  QMutexLocker _( idxFileMutex );

  if ( !rootNodeLoaded ) {
    readNode( rootOffset, rootNode );
    rootNodeLoaded = true;
  }

  const char * leaf     = &rootNode.front();
  const char * leafEnd  = leaf + rootNode.size();
  const char * chainPtr = nullptr;

  vector< char > extLeaf;

  for ( ;; ) {
    leafEntries = *(uint32_t *)leaf;

    if ( isCancelled && Utils::AtomicInt::loadAcquire( *isCancelled ) ) {
      return;
    }

    if ( leafEntries == 0xffffFFFF ) {
      currentNodeOffset = *( (uint32_t *)leaf + 1 );
      readNode( currentNodeOffset, extLeaf );
      leaf     = &extLeaf.front();
      leafEnd  = leaf + extLeaf.size();
      nextLeaf = idxFile->read< uint32_t >();
    }
    else {
      chainPtr = leaf + sizeof( uint32_t );
      break;
    }
  }

  if ( !leafEntries ) {
    if ( currentNodeOffset != rootOffset ) {
      throw exCorruptedChainData();
    }
    else {
      return;
    }
  }

  QList< uint32_t >::Iterator begOffsets = offsets.begin();
  QList< uint32_t >::Iterator endOffsets = offsets.end();

  for ( ;; ) {
    vector< WordArticleLink > result = readChain( chainPtr );

    for ( auto & i : result ) {
      uint32_t articleOffset = i.articleOffset;

      QList< uint32_t >::Iterator it = std::lower_bound( begOffsets, endOffsets, articleOffset );

      if ( it != offsets.end() && *it == articleOffset ) {
        if ( isCancelled && Utils::AtomicInt::loadAcquire( *isCancelled ) ) {
          return;
        }

        auto word = QString::fromUtf8( ( i.prefix + i.word ).c_str() );

        if ( headwords.indexOf( word ) == -1 ) {
          headwords.append( word );
        }
        offsets.erase( it );
        begOffsets = offsets.begin();
        endOffsets = offsets.end();
      }

      if ( offsets.isEmpty() ) {
        break;
      }
    }

    if ( offsets.isEmpty() ) {
      break;
    }

    if ( chainPtr >= leafEnd ) {

      if ( nextLeaf ) {
        readNode( nextLeaf, extLeaf );
        leaf    = &extLeaf.front();
        leafEnd = leaf + extLeaf.size();

        nextLeaf = idxFile->read< uint32_t >();
        chainPtr = leaf + sizeof( uint32_t );

        leafEntries = *(uint32_t *)leaf;

        if ( leafEntries == 0xffffFFFF ) {
          throw exCorruptedChainData();
        }
      }
      else {
        break;
      }
    }
  }
}

bool BtreeDictionary::getHeadwords( QStringList & headwords )
{
  QSet< QString > setOfHeadwords;

  headwords.clear();
  setOfHeadwords.reserve( getWordCount() );

  try {
    getAllHeadwords( setOfHeadwords );

    if ( setOfHeadwords.size() ) {
      headwords.reserve( setOfHeadwords.size() );

      QSet< QString >::const_iterator it  = setOfHeadwords.constBegin();
      QSet< QString >::const_iterator end = setOfHeadwords.constEnd();

      for ( ; it != end; ++it ) {
        headwords.append( *it );
      }
    }
  }
  catch ( std::exception & ex ) {
    qWarning( "Failed headwords retrieving for \"%s\", reason: %s", getName().c_str(), ex.what() );
  }

  return headwords.size() > 0;
}

void BtreeDictionary::findHeadWordsWithLenth( int & index, QSet< QString > * headwords, uint32_t length )
{
  auto leafNodeOffsets = findNodes();
  findHeadWords( leafNodeOffsets, index, headwords, length );
}

void BtreeDictionary::getArticleText( uint32_t, QString &, QString & ) {}

}
