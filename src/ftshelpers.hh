#pragma once

#include <QString>
#include <QList>
#include <QtConcurrentRun>
#include "dict/dictionary.hh"
#include "btreeidx.hh"
#include "fulltextsearch.hh"
#include "folding.hh"

namespace FtsHelpers {

bool ftsIndexIsOldOrBad( BtreeIndexing::BtreeDictionary * dict );

unsigned ftsQueryFlags( int searchMode );

void makeFTSIndex( BtreeIndexing::BtreeDictionary * dict, QAtomicInt & isCancelled );

class FTSResultsRequest: public Dictionary::DataRequest
{
  BtreeIndexing::BtreeDictionary & dict;

  QString searchString;
  int searchMode;
  bool matchCase;

  QAtomicInt isCancelled;

  QAtomicInt results;
  QFuture< void > f;

  QList< FTS::FtsHeadword > foundHeadwords;

public:

  FTSResultsRequest( BtreeIndexing::BtreeDictionary & dict_,
                     const QString & searchString_,
                     int searchMode_,
                     bool matchCase_,
                     bool ignoreDiacritics_ ):
    dict( dict_ ),
    searchString( searchString_ ),
    searchMode( searchMode_ ),
    matchCase( matchCase_ )
  {
    if ( ignoreDiacritics_ )
      searchString = Folding::applyDiacriticsOnly( Text::removeTrailingZero( searchString_ ) );

    results = 0;
    f       = QtConcurrent::run( [ this ]() {
      this->run();
    } );
  }

  void run();

  QList< FTS::FtsHeadword > takeHeadwords();

  void cancel() override
  {
    isCancelled.ref();
  }

  ~FTSResultsRequest()
  {
    isCancelled.ref();
    f.waitForFinished();
  }
};

}
