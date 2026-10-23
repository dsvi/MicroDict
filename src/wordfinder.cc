/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "wordfinder.hh"
#include "folding.hh"
#include <map>


using std::vector;
using std::list;
using std::map;
using std::pair;

WordFinder::WordFinder( QObject * parent ):
  QObject( parent ),
  searchInProgress( false ),
  updateResultsTimer( this )
{
  updateResultsTimer.setInterval( 1000 );
  updateResultsTimer.setSingleShot( true );

  connect( &updateResultsTimer, &QTimer::timeout, this, &WordFinder::updateResults, Qt::QueuedConnection );
}

WordFinder::~WordFinder()
{
  clear();
}

void WordFinder::prefixMatch( const QString & str,
                              const std::vector< sptr< Dictionary::Class > > & dicts,
                              unsigned long maxResults,
                              Dictionary::Features features )
{
  cancel();

  searchType          = PrefixMatch;
  inputWord           = str;
  inputDicts          = &dicts;
  requestedMaxResults = maxResults;
  requestedFeatures   = features;

  resultsArray.clear();
  resultsIndex.clear();
  searchResults.clear();

  startSearch();
}
void WordFinder::stemmedMatch( const QString & str,
                               const std::vector< sptr< Dictionary::Class > > & dicts,
                               unsigned minLength,
                               unsigned maxSuffixVariation,
                               unsigned long maxResults,
                               Dictionary::Features features )
{
  cancel();

  searchType                = StemmedMatch;
  inputWord                 = str;
  inputDicts                = &dicts;
  requestedMaxResults       = maxResults;
  requestedFeatures         = features;
  stemmedMinLength          = minLength;
  stemmedMaxSuffixVariation = maxSuffixVariation;

  resultsArray.clear();
  resultsIndex.clear();
  searchResults.clear();

  startSearch();
}

void WordFinder::expressionMatch( const QString & str,
                                  const std::vector< sptr< Dictionary::Class > > & dicts,
                                  unsigned long maxResults,
                                  Dictionary::Features features )
{
  cancel();

  searchType          = ExpressionMatch;
  inputWord           = str;
  inputDicts          = &dicts;
  requestedMaxResults = maxResults;
  requestedFeatures   = features;

  resultsArray.clear();
  resultsIndex.clear();
  searchResults.clear();

  startSearch();
}

void WordFinder::startSearch()
{
  if ( searchInProgress.load() ) {
    return;
  }

  {
    queuedRequests.clear();

    searchErrorString.clear();
    searchResultsUncertain = false;

    searchInProgress = true;
  }

  updateResultsTimer.start();

  std::vector< QString > allWordWritings( 1, inputWord );

  for ( const auto & inputDict : *inputDicts ) {
    vector< QString > writings = inputDict->getAlternateWritings( allWordWritings[ 0 ] );

    allWordWritings.insert( allWordWritings.end(), writings.begin(), writings.end() );
  }

  setAllWordWritings( allWordWritings );

  for ( const auto & inputDict : *inputDicts ) {
    if ( ( inputDict->getFeatures() & requestedFeatures ) != requestedFeatures ) {
      continue;
    }

    for ( const auto & allWordWriting : allWordWritings ) {
      try {
        sptr< Dictionary::WordSearchRequest > sr = ( searchType == PrefixMatch || searchType == ExpressionMatch ) ?
          inputDict->prefixMatch( allWordWriting, requestedMaxResults ) :
          inputDict->stemmedMatch( allWordWriting, stemmedMinLength, stemmedMaxSuffixVariation, requestedMaxResults );

        connect( sr.get(), &Dictionary::Request::finished, this, [ this ]() {
          requestFinished();
        } );

        queuedRequests.push_back( sr );
      }
      catch ( std::exception & e ) {
        qWarning( "Word \"%s\" search error (%s) in \"%s\"",
                  inputWord.toUtf8().data(),
                  e.what(),
                  inputDict->getName().c_str() );
      }
    }
  }

  requestFinished();
}

void WordFinder::cancel()
{
  searchInProgress = false;

  cancelSearches();
}

void WordFinder::clear()
{
  cancel();

  queuedRequests.clear();
}

void WordFinder::requestFinished()
{
  if ( !searchInProgress.load() ) {
    return;
  }
  auto snapshot = queuedRequests.snapshot();

  bool all_finished = true;
  for ( const auto & request : snapshot ) {
    if ( !searchInProgress.load() ) {
      return;
    }

    if ( !request->isFinished() ) {
      all_finished = false;
      break;
    }
  }

  if ( !searchInProgress.load() ) {
    return;
  }

  if ( all_finished ) {
    updateResults();
  }
}

namespace {

enum Score {
  ScoreExactMatch              = 100,
  ScoreExactNoFullCaseMatch    = 95,
  ScoreExactNoDiaMatch         = 90,
  ScoreExactNoPunctMatch       = 85,
  ScoreExactNoWsMatch          = 80,
  ScoreExactInsideMatch        = 75,
  ScoreExactNoDiaInsideMatch   = 70,
  ScoreExactNoPunctInsideMatch = 65,
  ScorePrefixMatch             = 60,
  ScorePrefixNoDiaMatch        = 55,
  ScorePrefixNoPunctMatch      = 50,
  ScorePrefixNoWsMatch         = 45,
  ScoreWorstMatch              = 0
};

unsigned saturated( unsigned x )
{
  return x < 255 ? x : 255;
}

bool hasSurroundedWithWs( const QString & haystack, const QString & needle, qsizetype & pos )
{
  if ( haystack.size() < needle.size() ) {
    return false;
  }

  for ( pos = 0;; ++pos ) {
    pos = haystack.indexOf( needle, pos );
    if ( pos < 0 ) {
      return false;
    }

    if ( ( !pos || Folding::isWhitespace( haystack[ pos - 1 ].unicode() )
           || Folding::isPunct( haystack[ pos - 1 ].unicode() ) )
         && ( ( pos + needle.size() == haystack.size() )
              || Folding::isWhitespace( haystack[ pos + needle.size() ].unicode() )
              || Folding::isPunct( haystack[ pos + needle.size() ].unicode() ) ) ) {
      pos = saturated( pos );
      return true;
    }
  }
}

struct ResultFoldings
{
  QString noFullCase;
  QString noDia;
  QString noPunct;
  QString noWs;
};

ResultFoldings computeFoldings( const QString & str )
{
  ResultFoldings f;
  f.noFullCase = Folding::applyFullCaseOnly( str );
  f.noDia      = Folding::applyDiacriticsOnly( f.noFullCase );
  f.noPunct    = Folding::applyPunctOnly( f.noDia );
  f.noWs       = Folding::applyWhitespaceOnly( f.noPunct );
  return f;
}

int computeMatchScore( const QString & result,
                       const QString & target,
                       const QString & targetNoFullCase,
                       const QString & targetNoDia,
                       const QString & targetNoPunct,
                       const QString & targetNoWs,
                       QString::size_type & matchPos,
                       const ResultFoldings & rf )
{
  if ( result == target ) {
    return ScoreExactMatch;
  }
  if ( rf.noFullCase == targetNoFullCase ) {
    return ScoreExactNoFullCaseMatch;
  }
  if ( rf.noDia == targetNoDia ) {
    return ScoreExactNoDiaMatch;
  }
  if ( rf.noPunct == targetNoPunct ) {
    return ScoreExactNoPunctMatch;
  }
  if ( rf.noWs == targetNoWs ) {
    return ScoreExactNoWsMatch;
  }
  if ( hasSurroundedWithWs( result, target, matchPos ) ) {
    return ScoreExactInsideMatch;
  }
  if ( hasSurroundedWithWs( rf.noDia, targetNoDia, matchPos ) ) {
    return ScoreExactNoDiaInsideMatch;
  }
  if ( hasSurroundedWithWs( rf.noPunct, targetNoPunct, matchPos ) ) {
    return ScoreExactNoPunctInsideMatch;
  }
  if ( result.size() > target.size() && result.startsWith( target ) ) {
    return ScorePrefixMatch;
  }
  if ( rf.noDia.size() > targetNoDia.size() && rf.noDia.startsWith( targetNoDia ) ) {
    return ScorePrefixNoDiaMatch;
  }
  if ( rf.noPunct.size() > targetNoPunct.size() && rf.noPunct.startsWith( targetNoPunct ) ) {
    return ScorePrefixNoPunctMatch;
  }
  if ( rf.noWs.size() > targetNoWs.size() && rf.noWs.startsWith( targetNoWs ) ) {
    return ScorePrefixNoWsMatch;
  }
  return ScoreWorstMatch;
}

}

void WordFinder::updateResults()
{
  if ( !searchInProgress.load() ) {
    return;
  }

  if ( updateResultsTimer.isActive() ) {
    updateResultsTimer.stop();
  }

  auto allWordWritings = getAllWordWritings();

  QString original = Folding::applySimpleCaseOnly( allWordWritings[ 0 ] );

  auto snapshot = queuedRequests.snapshot();

  bool all_finished = true;
  for ( const auto & request : snapshot ) {

    if ( !request->isFinished() ) {
      all_finished = false;
      continue;
    }
    if ( !request->matchesCount() ) {
      continue;
    }
    if ( !request->getErrorString().isEmpty() ) {
      searchErrorString += tr( "Failed to query some dictionaries." );
      continue;
    }
    if ( request->isUncertain() && !searchResultsUncertain ) {
      searchResultsUncertain = true;
    }


    size_t count = request->matchesCount();

    for ( size_t x = 0; x < count; ++x ) {
      QString match      = ( *request )[ x ].word;
      int weight         = ( *request )[ x ].weight;
      QString lowerCased = Folding::applySimpleCaseOnly( match );

      if ( searchType == ExpressionMatch ) {
        unsigned ws;

        for ( ws = 0; ws < allWordWritings.size(); ws++ ) {
          if ( ws == 0 ) {
            if ( lowerCased.startsWith( original ) ) {
              break;
            }
          }
          else if ( lowerCased == Folding::applySimpleCaseOnly( allWordWritings[ ws ] ) ) {
            break;
          }
        }

        if ( ws >= allWordWritings.size() ) {
          continue;
        }
        weight = ws;
      }

      auto insertResult =
        resultsIndex.insert( pair< QString, ResultsArray::iterator >( lowerCased, resultsArray.end() ) );

      if ( !insertResult.second ) {
        if ( insertResult.first->second->word != match ) {
          insertResult.first->second->word = lowerCased;
        }
        if ( !weight && insertResult.first->second->wasSuggested ) {
          insertResult.first->second->wasSuggested = false;
        }
      }
      else {
        resultsArray.emplace_back();

        resultsArray.back().word         = match;
        resultsArray.back().rank         = INT_MAX;
        resultsArray.back().wasSuggested = ( weight != 0 );
        resultsArray.back().rankFeatures = RankFeatures( 0, INT_MAX );

        insertResult.first->second = --resultsArray.end();
      }
    }
  }

  size_t maxSearchResults = 500;

  if ( !resultsArray.empty() ) {
    auto computePrefixScore = []( const QString & result,
                                  const QString & target,
                                  const QString & targetNoFullCase,
                                  const QString & targetNoDia,
                                  const QString & targetNoPunct,
                                  const QString & targetNoWs,
                                  const ResultFoldings & rf ) -> RankFeatures {
      QString::size_type matchPos = 0;
      int score =
        computeMatchScore( result, target, targetNoFullCase, targetNoDia, targetNoPunct, targetNoWs, matchPos, rf );
      int lengthDelta = std::abs( static_cast< int >( target.size() ) - static_cast< int >( result.size() ) );
      return RankFeatures( score, lengthDelta );
    };

    auto computeStemmedScore = []( const QString & result, const QString & target ) -> RankFeatures {
      QString resultFolded = Folding::apply( result );
      QString targetFolded = Folding::apply( target );

      int charsInCommon = 0;
      for ( qsizetype i = 0;
            i < targetFolded.size() && i < resultFolded.size() && targetFolded[ i ] == resultFolded[ i ];
            ++i, ++charsInCommon ) {
        ;
      }
      int lengthDelta = std::abs( static_cast< int >( target.size() ) - static_cast< int >( result.size() ) );
      return RankFeatures( charsInCommon, lengthDelta );
    };

    if ( searchType == PrefixMatch ) {
      std::map< QString, ResultFoldings > resultFoldingsCache;
      for ( const auto & i : resultsIndex ) {
        resultFoldingsCache[ i.first ] = computeFoldings( i.first );
      }

      for ( const auto & allWordWriting : allWordWritings ) {
        QString target           = Folding::applySimpleCaseOnly( allWordWriting );
        QString targetNoFullCase = Folding::applyFullCaseOnly( target );
        QString targetNoDia      = Folding::applyDiacriticsOnly( targetNoFullCase );
        QString targetNoPunct    = Folding::applyPunctOnly( targetNoDia );
        QString targetNoWs       = Folding::applyWhitespaceOnly( targetNoPunct );

        for ( const auto & i : resultsIndex ) {
          RankFeatures rf = computePrefixScore( i.first,
                                                target,
                                                targetNoFullCase,
                                                targetNoDia,
                                                targetNoPunct,
                                                targetNoWs,
                                                resultFoldingsCache[ i.first ] );

          RankFeatures & destRf = i.second->rankFeatures;
          if ( rf.baseScore > destRf.baseScore
               || ( rf.baseScore == destRf.baseScore && rf.lengthDelta < destRf.lengthDelta ) ) {
            destRf = rf;
          }
        }
      }
      resultsArray.sort( SortByRankFeatures() );
    }
    else if ( searchType == StemmedMatch ) {
      for ( const auto & allWordWriting : allWordWritings ) {
        for ( const auto & i : resultsIndex ) {
          RankFeatures rf = computeStemmedScore( i.first, allWordWriting );

          RankFeatures & destRf = i.second->rankFeatures;
          if ( rf.baseScore > destRf.baseScore
               || ( rf.baseScore == destRf.baseScore && rf.lengthDelta < destRf.lengthDelta ) ) {
            destRf = rf;
          }
        }
      }
      resultsArray.sort( SortByRankFeatures() );
      maxSearchResults = 15;
    }
  }

  searchResults.clear();
  searchResults.reserve( resultsArray.size() < maxSearchResults ? resultsArray.size() : maxSearchResults );

  for ( const auto & i : resultsArray ) {
    if ( searchResults.size() < maxSearchResults ) {
      searchResults.emplace_back( i.word, i.wasSuggested );
    }
    else {
      break;
    }
  }

  if ( !all_finished ) {
    emit updated();
  }
  else {
    searchInProgress = false;
    emit finished();
  }
}

void WordFinder::cancelSearches()
{
  auto snapshot = queuedRequests.snapshot();

  for ( const auto & queuedRequest : snapshot ) {
    if ( queuedRequest ) {
      queuedRequest->cancel();
    }
  }
}
