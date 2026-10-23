/* This file is (c) 2014 Abs62
 * Licensed under GPLv3 or later, see the LICENSE file */
#include "xapian.h"

#if XAPIAN_MAJOR_VERSION < 2
#error "Xapian 2.x or newer is required: FLAG_FUZZY, FLAG_WILDCARD_GLOB and the per-kind set_max_expansion() overload do not exist in 1.4.x."
#endif
#include <cstdlib>
#include "fulltextsearch.hh"
#include "folding.hh"
#include "ftshelpers.hh"
#include "dictfile.hh"
#include "utils.hh"
#include "common/globalbroadcaster.hh"

#include <vector>
#include <string>

using std::vector;
using std::string;

DEF_EX( exUserAbort, "User abort", Dictionary::Ex )

namespace FtsHelpers {
const static std::string finish_mark = std::string( "dehsinif" );

const Xapian::termcount maxFtsExpansions = 500;

const unsigned maxTypoDistance = 1;

const size_t minTypoWordLength = 3;

static size_t charCount( const string & utf8 )
{
  size_t n = 0;
  for ( const unsigned char c : utf8 ) {
    if ( ( c & 0xC0 ) != 0x80 ) {
      ++n;
    }
  }
  return n;
}

static bool queryHasTerm( const Xapian::Query & query, const string & term )
{
  for ( auto it = query.get_unique_terms_begin(); it != query.get_unique_terms_end(); ++it ) {
    if ( *it == term ) {
      return true;
    }
  }
  return false;
}

bool ftsIndexIsOldOrBad( BtreeIndexing::BtreeDictionary * dict )
{
  try {
    const QByteArray encodedPath = QFile::encodeName( QString::fromStdString( dict->ftsIndexName() ) );
    const Xapian::Database db( encodedPath.toStdString() );

    auto docid = db.get_lastdocid();
    if ( docid == 0 ) {
      return true;
    }
    auto document = db.get_document( docid );

    const string lastDoc = document.get_data();
    return lastDoc != finish_mark;
  }
  catch ( Xapian::Error & e ) {
    qWarning() << e.get_description().c_str();
    Utils::Fs::removeDirectory( dict->ftsIndexName() );
    return true;
  }
  catch ( ... ) {
    return true;
  }
}

void removeObsoleteIndexes( BtreeIndexing::BtreeDictionary * dict )
{
  const std::string & name   = dict->ftsIndexName();
  const std::string   suffix = Dictionary::getFtsSuffix();

  if ( name.size() <= suffix.size() ||
       name.compare( name.size() - suffix.size(), suffix.size(), suffix ) != 0 ) {
    return;
  }

  const std::string base = name.substr( 0, name.size() - suffix.size() );

  const auto removeOldTrees = [ & ]( const std::string & indexBase, bool includeCurrentSuffix ) {
    for ( const auto & old : Dictionary::getObsoleteFtsSuffixes() ) {
      Utils::Fs::removeDirectory( indexBase + old );
      Utils::Fs::removeDirectory( indexBase + old + "_temp" );
    }

    if ( includeCurrentSuffix ) {
      Utils::Fs::removeDirectory( indexBase + suffix );
      Utils::Fs::removeDirectory( indexBase + suffix + "_temp" );
    }
  };

  removeOldTrees( base, false );

  const std::string id = dict->getId();

  if ( id.empty() || base.size() < id.size() ||
       base.compare( base.size() - id.size(), id.size(), id ) != 0 ) {
    return;
  }

  const std::string legacyId = Dictionary::makeLegacyDictionaryId( dict->getDictionaryFilenames() );

  if ( legacyId.empty() || legacyId == id ) {
    return;
  }

  const std::string legacyBase = base.substr( 0, base.size() - id.size() ) + legacyId;

  if ( QFile::remove( QString::fromStdString( legacyBase ) ) ) {
    qDebug() << "FTS:" << dict->getName().c_str() << ": removed the index named by the retired MD5 id";
  }

  removeOldTrees( legacyBase, true );
}

void requestFtsIndexing( Dictionary::Class & dict )
{
  if ( dict.canFTS() && GlobalBroadcaster::instance() != nullptr ) {
    GlobalBroadcaster::instance()->startFtsIndexing();
  }
}

void makeFTSIndex( BtreeIndexing::BtreeDictionary * dict, QAtomicInt & isCancelled )
{
  const QMutexLocker _( &dict->getFtsMutex() );

  if ( dict->haveFTSIndex() ) {
    qDebug() << "fts index already exists";
    return;
  }

  removeObsoleteIndexes( dict );

  try {
    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      qDebug() << "user abort the fts index creation";
      throw exUserAbort();
    }

    const std::string path       = dict->ftsIndexName() + "_temp";
    const QByteArray encodedPath = QFile::encodeName( QString::fromStdString( path ) );
    Xapian::WritableDatabase db( encodedPath.toStdString(), Xapian::DB_CREATE_OR_OPEN );

    Xapian::TermGenerator indexer;
    indexer.set_database( db );
    indexer.set_flags( Xapian::TermGenerator::FLAG_NGRAMS | Xapian::TermGenerator::FLAG_SPELLING );

    BtreeIndexing::IndexedWords indexedWords;

    QSet< uint32_t > setOfOffsets;
    setOfOffsets.reserve( dict->getArticleCount() );

    dict->findArticleLinks( nullptr, &setOfOffsets, nullptr, &isCancelled );

    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      throw exUserAbort();
    }

    std::vector< uint32_t > offsets;
    offsets.assign( setOfOffsets.begin(), setOfOffsets.end() );
    std::sort( offsets.begin(), offsets.end() );

    setOfOffsets.clear();

    if ( Utils::AtomicInt::loadAcquire( isCancelled ) ) {
      throw exUserAbort();
    }

    bool skip            = true;
    uint32_t lastAddress = -1;
    try {
      if ( db.get_lastdocid() > 0 ) {
        Xapian::Document lastDoc = db.get_document( db.get_lastdocid() );
        lastAddress              = atoi( lastDoc.get_data().c_str() );
      }
      else {
        skip = false;
      }
    }
    catch ( Xapian::Error & e ) {
      qDebug() << "get last doc failed: " << e.get_description().c_str();
      skip = false;
    }

    long indexedDoc = 0L;

    auto start_it = offsets.cbegin();
    if ( skip ) {
      start_it         = std::upper_bound( offsets.cbegin(), offsets.cend(), lastAddress );
      long num_skipped = std::distance( offsets.cbegin(), start_it );
      qDebug() << "Resuming FTS indexing from offset" << ( start_it != offsets.cend() ? *start_it : -1 ) << "at index"
               << num_skipped;
      indexedDoc += num_skipped;
    }

    for ( auto it = start_it; it != offsets.cend(); ++it ) {
      const auto & address = *it;
      indexedDoc++;

      if ( Utils::AtomicInt::loadAcquire( isCancelled ) || !dict->canFTS() ) {
        qDebug() << "user abort the fts index creation" << isCancelled << "canFTS:" << dict->canFTS();
        throw exUserAbort();
      }

      QString headword, articleStr;

      dict->getArticleText( address, headword, articleStr );

      Xapian::Document doc;

      indexer.set_document( doc );

      indexer.index_text( Folding::applyForIndex( articleStr ) );
      indexer.index_text( Folding::applyForIndex( headword ) );

      doc.set_data( std::to_string( address ) );
      db.add_document( doc );
      dict->setIndexedFtsDoc( indexedDoc );
    }

    Xapian::Document doc;
    doc.set_data( finish_mark );
    db.add_document( doc );

    offsets.clear();

    db.commit();

    const QByteArray encodedFtsPath = QFile::encodeName( QString::fromStdString( dict->ftsIndexName() ) );
    db.compact( encodedFtsPath.toStdString() );

    db.close();

    Utils::Fs::removeDirectory( dict->ftsIndexName() + "_temp" );
  }
  catch ( Xapian::Error & e ) {
    qWarning() << "create xapian index failed:" << QString::fromStdString( e.get_description() );
    Utils::Fs::removeDirectory( dict->ftsIndexName() );
    Utils::Fs::removeDirectory( dict->ftsIndexName() + "_temp" );
    throw;
  }
  catch ( ... ) {
    Utils::Fs::removeDirectory( dict->ftsIndexName() + "_temp" );
    throw;
  }
}

unsigned ftsQueryFlags( int searchMode )
{
  unsigned flags = Xapian::QueryParser::FLAG_DEFAULT | Xapian::QueryParser::FLAG_PURE_NOT
    | Xapian::QueryParser::FLAG_NGRAMS | Xapian::QueryParser::FLAG_FUZZY;

  if ( searchMode == FTS::Wildcards ) {
    flags |= Xapian::QueryParser::FLAG_WILDCARD_GLOB | Xapian::QueryParser::FLAG_PARTIAL;
  }

  return flags;
}

void FTSResultsRequest::run()
{
  if ( !dict.ensureInitDone().empty() ) {
    setErrorString( QString::fromUtf8( dict.ensureInitDone().c_str() ) );
    finish();
    return;
  }

  try {
    if ( dict.haveFTSIndex() ) {
      const QByteArray encodedPath = QFile::encodeName( QString::fromStdString( dict.ftsIndexName() ) );
      Xapian::Database db( encodedPath.toStdString() );

      Xapian::Enquire enquire( db );

      string query_string( Folding::applyForIndex( searchString ) );

      Xapian::QueryParser qp;
      qp.set_database( db );
      qp.set_default_op( Xapian::Query::op::OP_AND );

      const unsigned flags = ftsQueryFlags( searchMode );

      qp.set_max_expansion( maxFtsExpansions, Xapian::Query::WILDCARD_LIMIT_MOST_FREQUENT );

      Xapian::Query query = qp.parse_query( query_string, flags );
      qDebug() << "Parsed query is: " << query.get_description().c_str();

      enquire.set_query( query );
      Xapian::MSet matches = enquire.get_mset( 0, 100 );

      if ( matches.empty() && charCount( query_string ) >= minTypoWordLength
        && query_string.find_first_of( " \t\r\n" ) == string::npos ) {
        const string corrected = db.get_spelling_suggestion( query_string, maxTypoDistance );

        if ( !corrected.empty() && corrected != query_string && !queryHasTerm( query, corrected ) ) {
          try {
            enquire.set_query( qp.parse_query( corrected, flags ) );
            const Xapian::MSet correctedMatches = enquire.get_mset( 0, 100 );
            if ( !correctedMatches.empty() ) {
              qInfo() << "FTS:" << dict.getName().c_str() << "found nothing for" << searchString << ", retried as"
                      << QString::fromUtf8( corrected.c_str() );
              matches = correctedMatches;
            }
          }
          catch ( const Xapian::Error & e ) {
            qDebug() << "FTS: spelling correction" << QString::fromUtf8( corrected.c_str() )
                     << "did not parse:" << e.get_description().c_str();
          }
        }
      }

      emit matchCount( matches.get_matches_estimated() );

      qDebug() << matches.get_matches_estimated() << " results found.\n";
      qDebug() << "Matches " << matches.size() << ":\n\n";
      QList< uint32_t > offsetsForHeadwords;
      for ( Xapian::MSetIterator i = matches.begin(); i != matches.end(); ++i ) {
        qDebug() << i.get_rank() + 1 << ": " << i.get_weight() << " docid=" << *i << " ["
                 << i.get_document().get_data().c_str() << "]";
        if ( i.get_document().get_data() == finish_mark ) {
          continue;
        }
        offsetsForHeadwords.append( atoi( i.get_document().get_data().c_str() ) );
      }

      if ( !offsetsForHeadwords.isEmpty() ) {
        QList< QString > headwords;
        QMutexLocker _( &dataMutex );
        QString id = QString::fromUtf8( dict.getId().c_str() );
        dict.getHeadwordsFromOffsets( offsetsForHeadwords, headwords, &isCancelled );
        for ( const auto & headword : std::as_const( headwords ) ) {
          foundHeadwords.append( FTS::FtsHeadword( headword, id, QStringList(), matchCase ) );
        }
      }
    }
    else {
      qWarning() << "There is no fulltext index right now.";
      requestFtsIndexing( dict );
      finish();
      return;
    }
  }
  catch ( const Xapian::DatabaseModifiedError & e ) {
    qWarning() << "FTS:" << dict.getName().c_str() << ":" << e.get_msg().c_str();
  }
  catch ( const Xapian::DatabaseError & e ) {
    dict.resetFTSIndexState();
    qWarning() << "FTS:" << dict.getName().c_str() << ": full-text index unusable ("
               << e.get_msg().c_str() << "), asking the indexer to rebuild it";
    requestFtsIndexing( dict );
  }
  catch ( std::exception & ex ) {
    qWarning( "FTS: Failed full-text search for \"%s\", reason: %s", dict.getName().c_str(), ex.what() );
  }

  finish();
}

QList< FTS::FtsHeadword > FTSResultsRequest::takeHeadwords()
{
  QMutexLocker _( &dataMutex );

  QList< FTS::FtsHeadword > out;
  out.swap( foundHeadwords );

  return out;
}

}
