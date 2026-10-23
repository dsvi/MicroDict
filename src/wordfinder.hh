/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <list>
#include <map>
#include <atomic>
#include <QTimer>
#include <QRunnable>
#include "dict/dictionary.hh"
#include "concurrent_list.hh"

class WordFinder: public QObject
{
  Q_OBJECT

public:

  using SearchResults = std::vector< std::pair< QString, bool > >;

private:

  SearchResults searchResults;
  QString searchErrorString;
  bool searchResultsUncertain;
  concurrent_list< sptr< Dictionary::WordSearchRequest > > queuedRequests;
  std::atomic_bool searchInProgress;
  QMutex mutex;

  QTimer updateResultsTimer;

  QString inputWord;
  enum SearchType {
    PrefixMatch,
    StemmedMatch,
    ExpressionMatch
  } searchType;
  unsigned long requestedMaxResults;
  Dictionary::Features requestedFeatures;
  unsigned stemmedMinLength;
  unsigned stemmedMaxSuffixVariation;

  const std::vector< sptr< Dictionary::Class > > * inputDicts;

  std::vector< QString > _allWordWritings;

  struct RankFeatures
  {
    int baseScore;
    int lengthDelta;

    RankFeatures():
      baseScore( 0 ),
      lengthDelta( INT_MAX )
    {
    }
    RankFeatures( int score, int delta ):
      baseScore( score ),
      lengthDelta( delta )
    {
    }
  };

  struct OneResult
  {
    QString word;
    int rank;
    bool wasSuggested;
    RankFeatures rankFeatures;
  };

  using ResultsArray = std::list< OneResult >;
  using ResultsIndex = std::map< QString, ResultsArray::iterator >;
  ResultsArray resultsArray;
  ResultsIndex resultsIndex;

  std::vector< QString > getAllWordWritings()
  {
    QMutexLocker locker( &mutex );
    return _allWordWritings;
  }

  void setAllWordWritings( std::vector< QString > writings )
  {
    QMutexLocker locker( &mutex );
    _allWordWritings = std::move( writings );
  }

public:

  WordFinder( QObject * parent );
  ~WordFinder();

  void prefixMatch( const QString &,
                    const std::vector< sptr< Dictionary::Class > > &,
                    unsigned long maxResults = 40,
                    Dictionary::Features     = Dictionary::NoFeatures );

  void stemmedMatch( const QString &,
                     const std::vector< sptr< Dictionary::Class > > &,
                     unsigned minLength          = 3,
                     unsigned maxSuffixVariation = 3,
                     unsigned long maxResults    = 30,
                     Dictionary::Features        = Dictionary::NoFeatures );

  void expressionMatch( const QString &,
                        const std::vector< sptr< Dictionary::Class > > &,
                        unsigned long maxResults = 40,
                        Dictionary::Features     = Dictionary::NoFeatures );

  const SearchResults & getResults() const
  {
    return searchResults;
  }

  const QString & getErrorString()
  {
    return searchErrorString;
  }

  bool wasSearchUncertain() const
  {
    return searchResultsUncertain;
  }

  void cancel();

  void clear();
signals:

  void updated();

  void finished();

private slots:

  void requestFinished();

  void updateResults();

private:

  void startSearch();

  void cancelSearches();

  struct SortByRankFeatures
  {
    bool operator()( const OneResult & first, const OneResult & second )
    {
      if ( first.rankFeatures.baseScore != second.rankFeatures.baseScore ) {
        return first.rankFeatures.baseScore > second.rankFeatures.baseScore;
      }

      if ( first.rankFeatures.lengthDelta != second.rankFeatures.lengthDelta ) {
        return first.rankFeatures.lengthDelta < second.rankFeatures.lengthDelta;
      }

      return first.word < second.word;
    }
  };
};
