/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "dict/dictionary.hh"
#include "dictfile.hh"
#include <map>
#include <stdint.h>
#include <string>
#include <vector>
#include <QFuture>
#include <QList>

namespace BtreeIndexing {

using std::string;
using std::vector;
using std::map;

enum {
  FormatVersion = 4,
  ZipParseLogicVersion = 2
};

DEF_EX( exIndexWasNotOpened, "The index wasn't opened", Dictionary::Ex )
DEF_EX( exFailedToDecompressNode, "Failed to decompress a btree's node", Dictionary::Ex )
DEF_EX( exCorruptedChainData, "Corrupted chain data in the leaf of a btree encountered", Dictionary::Ex )

struct WordArticleLink
{
  string word, prefix;
  uint32_t articleOffset = 0;

  WordArticleLink() = default;

  WordArticleLink( const string & word_, uint32_t articleOffset_, const string & prefix_ = string() ):
    word( word_ ),
    prefix( prefix_ ),
    articleOffset( articleOffset_ )
  {
  }
};

struct IndexInfo
{
  uint32_t btreeMaxElements, rootOffset;

  IndexInfo( uint32_t btreeMaxElements_, uint32_t rootOffset_ ):
    btreeMaxElements( btreeMaxElements_ ),
    rootOffset( rootOffset_ )
  {
  }
};

class BtreeIndex
{
public:

  BtreeIndex();

  void openIndex( const IndexInfo &, File::Index &, QMutex & );

  vector< WordArticleLink > findArticles( const QString &, bool ignoreDiacritics = false, uint32_t maxMatchCount = -1 );

  void findAllArticleLinks( QList< WordArticleLink > & articleLinks );

  void getAllHeadwords( QSet< QString > & headwords );

  void findArticleLinks( QList< WordArticleLink > * articleLinks,
                         QSet< uint32_t > * offsets,
                         QSet< QString > * headwords,
                         QAtomicInt * isCancelled = 0 );

  void findHeadWords( QList< uint32_t > offsets, int & index, QSet< QString > * headwords, uint32_t length );
  void findSingleNodeHeadwords( uint32_t offsets, QSet< QString > * headwords );
  QList< uint32_t > findNodes();

  void
  getHeadwordsFromOffsets( QList< uint32_t > & offsets, QList< QString > & headwords, QAtomicInt * isCancelled = 0 );

protected:

  const char * findChainOffsetExactOrPrefix(
    const QString & target, bool & exactMatch, vector< char > & leaf, uint32_t & nextLeaf, const char *& leafEnd );

  void readNode( uint32_t offset, vector< char > & out );

  vector< WordArticleLink > readChain( const char *&, uint32_t maxMatchCount = -1 );

  void antialias( const QString &, vector< WordArticleLink > &, bool ignoreDiacritics );

protected:

  QMutex * idxFileMutex;
  File::Index * idxFile;

private:

  uint32_t indexNodeSize;
  uint32_t rootOffset;
  bool rootNodeLoaded;
  vector< char > rootNode;
};

class BtreeDictionary: public Dictionary::Class, public BtreeIndex
{
public:

  BtreeDictionary( const string & id, const vector< string > & dictionaryFiles );

  virtual Dictionary::Features getFeatures() const noexcept
  {
    return Dictionary::SuitableForCompoundSearching;
  }

  virtual sptr< Dictionary::WordSearchRequest > prefixMatch( const QString &, unsigned long );

  virtual sptr< Dictionary::WordSearchRequest >
  stemmedMatch( const QString &, unsigned minLength, unsigned maxSuffixVariation, unsigned long maxResults );

  virtual bool isLocalDictionary()
  {
    return true;
  }

  virtual bool getHeadwords( QStringList & headwords );
  virtual void findHeadWordsWithLenth( int &, QSet< QString > * headwords, uint32_t length );

  virtual void getArticleText( uint32_t articleAddress, QString & headword, QString & text );

  const string & ftsIndexName() const
  {
    return ftsIdxName;
  }

  QMutex & getFtsMutex()
  {
    return ftsIdxMutex;
  }

  virtual uint32_t getFtsIndexVersion()
  {
    return 0;
  }

  virtual const string & ensureInitDone();

protected:
  QMutex ftsIdxMutex;
  string ftsIdxName;

  friend class BtreeWordSearchRequest;
  friend class FTSResultsRequest;
};

class BtreeWordSearchRequest: public Dictionary::WordSearchRequest
{
protected:
  BtreeDictionary & dict;
  QString str;
  unsigned long maxResults;
  unsigned minLength;
  int maxSuffixVariation;
  bool allowMiddleMatches;
  QAtomicInt isCancelled;
  QFuture< void > f;

public:

  BtreeWordSearchRequest( BtreeDictionary & dict_,
                          const QString & str_,
                          unsigned minLength_,
                          int maxSuffixVariation_,
                          bool allowMiddleMatches_,
                          unsigned long maxResults_,
                          bool startRunnable = true );

  virtual void findMatches();

  void run();

  virtual void cancel()
  {
    isCancelled.ref();
  }

  ~BtreeWordSearchRequest();
};

struct IndexedWords: public map< string, vector< WordArticleLink > >
{
  void addWord( const QString & word, uint32_t articleOffset, unsigned int maxHeadwordSize = 100U );

  void addSingleWord( const QString & word, uint32_t articleOffset );
};

IndexInfo buildIndex( const IndexedWords &, File::Index & file );

}
