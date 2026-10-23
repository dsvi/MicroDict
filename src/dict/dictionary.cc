/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include <vector>
#include <algorithm>
#include <cstdio>
#include "dictionary.hh"

#include <QFileInfo>
#include <QDateTime>

#include "config.hh"
#include "common/globalbroadcaster.hh"
#include <QDir>
#include <QCryptographicHash>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QRegularExpression>
#include "utils.hh"
#include "zipfile.hh"
#include <array>

namespace Dictionary {

bool Request::isFinished()
{
  return Utils::AtomicInt::loadAcquire( isFinishedFlag );
}

void Request::update()
{
  if ( !Utils::AtomicInt::loadAcquire( isFinishedFlag ) ) {
    emit updated();
  }
}

void Request::finish()
{
  if ( !Utils::AtomicInt::loadAcquire( isFinishedFlag ) ) {
    {
      QMutexLocker _( &dataMutex );
      isFinishedFlag.ref();

      cond.wakeAll();
    }
    emit finished();
  }
}

void Request::setErrorString( const QString & str )
{
  QMutexLocker _( &errorStringMutex );

  errorString = str;
}

QString Request::getErrorString()
{
  QMutexLocker _( &errorStringMutex );

  return errorString;
}

size_t WordSearchRequest::matchesCount()
{
  QMutexLocker _( &dataMutex );

  return matches.size();
}

WordMatch WordSearchRequest::operator[]( size_t index )
{
  QMutexLocker _( &dataMutex );

  if ( index >= matches.size() ) {
    throw exIndexOutOfRange();
  }

  return matches[ index ];
}

vector< WordMatch > & WordSearchRequest::getAllMatches()
{
  if ( !isFinished() ) {
    throw exRequestUnfinished();
  }

  return matches;
}

void WordSearchRequest::addMatch( const WordMatch & match )
{
  QMutexLocker _( &dataMutex );

  if ( std::find( matches.begin(), matches.end(), match ) == matches.end() ) {
    matches.push_back( match );
  }
}

long DataRequest::dataSize()
{
  QMutexLocker _( &dataMutex );
  long size = hasAnyData ? (long)data.size() : -1;

  if ( size == 0 && !isFinished() ) {
    cond.wait( &dataMutex );
    size = hasAnyData ? (long)data.size() : -1;
  }
  return size;
}

void DataRequest::appendDataSlice( const void * buffer, size_t size )
{
  QMutexLocker _( &dataMutex );

  size_t offset = data.size();

  data.resize( data.size() + size );

  memcpy( &data.front() + offset, buffer, size );
  cond.wakeAll();
}

void DataRequest::appendString( std::string_view str )
{
  QMutexLocker _( &dataMutex );
  data.reserve( data.size() + str.size() );
  data.insert( data.end(), str.begin(), str.end() );
  cond.wakeAll();
}

vector< char > & DataRequest::getFullData()
{
  if ( !isFinished() ) {
    throw exRequestUnfinished();
  }

  return data;
}

Class::Class( const string & id_, const vector< string > & dictionaryFiles_ ):
  id( id_ ),
  dictionaryFiles( dictionaryFiles_ ),
  indexedFtsDoc( 0 ),
  dictionaryIconLoaded( false ),
  can_FTS( false ),
  FTS_index_completed( false )
{
}

void Class::deferredInit()
{
}

sptr< WordSearchRequest > Class::stemmedMatch( const QString &, unsigned, unsigned, unsigned long )
{
  return std::make_shared< WordSearchRequestInstant >();
}

sptr< WordSearchRequest > Class::findHeadwordsForSynonym( const QString & )
{
  return std::make_shared< WordSearchRequestInstant >();
}

vector< QString > Class::getAlternateWritings( const QString & ) noexcept
{
  return {};
}

QString Class::getContainingFolder() const
{
  if ( !dictionaryFiles.empty() ) {
    auto fileInfo = QFileInfo( QString::fromStdString( dictionaryFiles[ 0 ] ) );
    if ( fileInfo.isDir() ) {
      return fileInfo.absoluteFilePath();
    }
    return fileInfo.absolutePath();
  }

  return {};
}

sptr< DataRequest > Class::getResource( const string & )

{
  return std::make_shared< DataRequestInstant >( false );
}

sptr< DataRequest > Class::getSearchResults( const QString &, int, bool, bool )
{
  return std::make_shared< DataRequestInstant >( false );
}

const QString & Class::getDescription()
{
  return dictionaryDescription;
}

void Class::setIndexedFtsDoc( long _indexedFtsDoc )
{
  indexedFtsDoc = _indexedFtsDoc;

  auto newProgress = getIndexingFtsProgress();
  if ( newProgress != lastProgress ) {
    lastProgress = newProgress;
    emit GlobalBroadcaster::instance()
      -> indexingDictionary( QString( "%1......%%2" ).arg( QString::fromStdString( getName() ) ).arg( newProgress ) );
  }
}


QString Class::getMainFilename()
{
  return {};
}

const QIcon & Class::getIcon() noexcept
{
  if ( !dictionaryIconLoaded ) {
    loadIcon();
  }
  return dictionaryIcon;
}

void Class::loadIcon() noexcept
{
  dictionaryIconLoaded = true;
}

int Class::getOptimalIconSize()
{
  return 64 * qGuiApp->devicePixelRatio();
}

bool Class::loadIconFromFileName( const QString & mainDictFileName )
{
  const QFileInfo info( mainDictFileName );
  QDir dir = info.absoluteDir();

  dir.setFilter( QDir::Files );
  dir.setNameFilters( QStringList() << "*.bmp"
                                    << "*.png"
                                    << "*.jpg"
                                    << "*.ico"
                                    << "*.jpeg"
                                    << "*.gif"
                                    << "*.webp"
                                    << "*.svg"
                                    << "*.svgz" );

  const QString basename = info.baseName();
  for ( const auto & f : dir.entryInfoList() ) {
    if ( f.baseName() == basename && loadIconFromFilePath( f.absoluteFilePath() ) ) {
      return true;
    }
  }
  return false;
}

bool Class::loadIconFromFilePath( const QString & filename )
{
  auto iconSize = getOptimalIconSize();
  QImage img( filename );

  if ( img.isNull() ) {
    return false;
  }
  else {
    auto result    = img.scaled( { iconSize, iconSize }, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation );
    dictionaryIcon = QIcon( QPixmap::fromImage( result ) );

    return !dictionaryIcon.isNull();
  }
}

bool Class::loadIconFromText( const QString & iconUrl, const QString & text )
{
  auto abbrName = getAbbrName( text, QString::fromStdString( getId() ) );
  if ( abbrName.isEmpty() ) {
    return false;
  }
  QImage img( iconUrl );

  if ( !img.isNull() ) {
    auto iconSize = getOptimalIconSize();

    QImage result = img.scaled( { iconSize, iconSize }, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation );

    QPainter painter( &result );
    const QRect rectangle = result.rect();
    painter.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing );
    painter.setCompositionMode( QPainter::CompositionMode_SourceAtop );

    QFont font = painter.font();
    font.setPixelSize( iconSize * 0.75 );
    font.setWeight( QFont::Bold );
    painter.setFont( font );

    const auto & id = getId();
    unsigned int idHash = qHash( QString::fromStdString( id ) );

    painter.setPen( QColor( 0, 0, 0, 80 ) );
    painter.drawText( rectangle.adjusted( 1, 1, 1, 1 ), Qt::AlignCenter, abbrName.at( 0 ) );

    painter.setPen( intToFixedColor( idHash ) );
    painter.drawText( rectangle, Qt::AlignCenter, abbrName.at( 0 ) );

    font.setPixelSize( iconSize * 0.5 );
    QFontMetrics fm1( font );
    const QString & orderNum = abbrName.mid( 1 );

    painter.setFont( font );

    painter.setPen( QColor( 0, 0, 0, 100 ) );
    QRect orderRect = rectangle;
    orderRect.setLeft( rectangle.left() + rectangle.width() / 2 );
    painter.drawText( orderRect.adjusted( 1, 1, 1, 1 ), Qt::AlignHCenter | Qt::AlignBottom, orderNum );

    painter.setPen( intToFixedColor( idHash + 5 ) );
    painter.drawText( orderRect, Qt::AlignHCenter | Qt::AlignBottom, orderNum );

    painter.end();

    dictionaryIcon = QIcon( QPixmap::fromImage( result ) );

    return !dictionaryIcon.isNull();
  }
  return false;
}

QColor Class::intToFixedColor( int index )
{
  static const std::array colors = { QColor( 255, 0, 0, 220 ),
                                     QColor( 0, 0, 255, 220 ),
                                     QColor( 230, 200, 0, 220 ),
                                     QColor( 100, 0, 200, 220 ),
                                     QColor( 255, 0, 255, 220 ),
                                     QColor( 255, 120, 0, 220 ),
                                     QColor( 0, 150, 255, 220 ),
                                     QColor( 128, 0, 0, 220 ),
                                     QColor( 180, 0, 180, 220 ),
                                     QColor( 75, 0, 130, 220 ),
                                     QColor( 210, 105, 30, 220 ),
                                     QColor( 255, 69, 0, 220 ),
                                     QColor( 255, 20, 147, 220 ),
                                     QColor( 105, 105, 105, 220 ),
                                     QColor( 70, 130, 180, 220 ) };

  return colors[ index % colors.size() ];
}

QString Class::getAbbrName( const QString & text, const QString & key )
{
  return GlobalBroadcaster::instance()->getAbbrName( text, key );
}

qsizetype findMatchingBracket( const QString & css, qsizetype startPos );

void Class::isolateCSS( QString & css, const QString & wrapperSelector )
{
  if ( css.isEmpty() ) {
    return;
  }

  static const QRegularExpression commentRegex( R"(\/\*.*?\*\/)", QRegularExpression::DotMatchesEverythingOption );
  css.remove( commentRegex );

  QString idSelector = "#gd-" + QString::fromStdString( getId() );
  QString prefix     = idSelector;
  if ( !wrapperSelector.isEmpty() ) {
    prefix += " " + wrapperSelector;
  }

  auto isolateSelector = [ & ]( QString selectorsPart ) {
    static const QString leaderSeparators   = " \t\r\n,>~+(";
    static const QString followerSeparators = " \t\r\n.#[:>~+,)";

    QStringList selectors = selectorsPart.split( ',', Qt::SkipEmptyParts );
    QStringList isolated;

    for ( QString s : selectors ) {
      s = s.trimmed();
      if ( s.isEmpty() ) {
        continue;
      }

      auto replaceTag = [ & ]( const QString & tag, const QString & replacement ) {
        qsizetype p = 0;
        while ( ( p = s.indexOf( tag, p, Qt::CaseInsensitive ) ) != -1 ) {
          bool leaderOk   = ( p == 0 || leaderSeparators.contains( s[ p - 1 ] ) );
          bool followerOk = ( p + tag.length() == s.length() || followerSeparators.contains( s[ p + tag.length() ] ) );
          if ( leaderOk && followerOk ) {
            s.replace( p, tag.length(), replacement );
            p += replacement.length();
          }
          else {
            p += tag.length();
          }
        }
      };

      replaceTag( "body", "gd-section-body" );
      replaceTag( "html", "gd-section-html" );
      replaceTag( ":root", "gd-section-html" );

      if ( !s.startsWith( idSelector ) ) {
        if ( s == "gd-section-body" || s == "gd-section-html" ) {
          isolated << idSelector;
        }
        isolated << prefix + " " + s;
      }
      else {
        isolated << s;
      }
    }
    return isolated.join( ", " );
  };

  QString result;
  int pos = 0;
  int len = css.length();

  while ( pos < len ) {
    int openBrace = css.indexOf( '{', pos );
    int atRule    = css.indexOf( '@', pos );

    if ( atRule != -1 && ( openBrace == -1 || atRule < openBrace ) ) {
      int semicolon = css.indexOf( ';', atRule );
      int brace     = css.indexOf( '{', atRule );

      if ( brace != -1 && ( semicolon == -1 || brace < semicolon ) ) {
        qsizetype matchingBrace = findMatchingBracket( css, brace );
        if ( matchingBrace != -1 ) {
          QString header = css.mid( atRule, brace - atRule + 1 );
          result.append( header );

          QString sub = css.mid( brace + 1, matchingBrace - brace - 1 );
          QString lowerHeader = header.toLower();
          if ( lowerHeader.contains( "@media" ) || lowerHeader.contains( "@supports" )
               || lowerHeader.contains( "@container" ) || lowerHeader.contains( "@layer" ) ) {
            isolateCSS( sub, wrapperSelector );
          }
          result.append( sub );
          result.append( '}' );
          pos = matchingBrace + 1;
        }
        else {
          result.append( css.mid( atRule ) );
          break;
        }
      }
      else if ( semicolon != -1 ) {
        result.append( css.mid( atRule, semicolon - atRule + 1 ) );
        pos = semicolon + 1;
      }
      else {
        result.append( css.mid( atRule ) );
        break;
      }
    }
    else if ( openBrace != -1 ) {
      QString selectors = css.mid( pos, openBrace - pos );
      result.append( isolateSelector( selectors ) );
      result.append( " {" );

      qsizetype matchingBrace = findMatchingBracket( css, openBrace );
      if ( matchingBrace != -1 ) {
        result.append( css.mid( openBrace + 1, matchingBrace - openBrace ) );
        pos = matchingBrace + 1;
      }
      else {
        result.append( css.mid( openBrace + 1 ) );
        break;
      }
    }
    else {
      result.append( css.mid( pos ) );
      break;
    }
  }

  css = result;
}

qsizetype findMatchingBracket( const QString & css, qsizetype startPos )
{
  qsizetype depth = 1;
  for ( qsizetype i = startPos + 1; i < css.length(); i++ ) {
    QChar ch = css.at( i );

    if ( ch == '\\' && i + 1 < css.length() ) {
      i++;
      continue;
    }

    if ( ch == '{' ) {
      depth++;
    }
    else if ( ch == '}' ) {
      depth--;
      if ( depth == 0 ) {
        return i;
      }
    }
  }
  return -1;
}

namespace {
vector< string > sortedDictionaryPaths( const vector< string > & dictionaryFiles )
{
  std::vector< string > sortedList;

  if ( Config::isPortableVersion() ) {
    sortedList.reserve( dictionaryFiles.size() );

    const QDir dictionariesDir( Config::getPortableVersionDictionaryDir() );

    for ( const auto & full : dictionaryFiles ) {
      QFileInfo fileInfo( QString::fromStdString( full ) );

      if ( fileInfo.isAbsolute() ) {
        sortedList.push_back( dictionariesDir.relativeFilePath( fileInfo.filePath() ).toStdString() );
      }
      else {
        sortedList.push_back( full );
      }
    }
  }
  else {
    sortedList = dictionaryFiles;
  }

  std::sort( sortedList.begin(), sortedList.end() );

  return sortedList;
}

string digestDictionaryPaths( const vector< string > & dictionaryFiles,
                              QCryptographicHash::Algorithm algorithm )
{
  const vector< string > sortedList = sortedDictionaryPaths( dictionaryFiles );

  QCryptographicHash hash( algorithm );

  for ( const auto & i : sortedList ) {
    hash.addData( { i.c_str(), static_cast< qsizetype >( i.size() + 1 ) } );
  }

  return hash.result().toHex().data();
}
}

string makeDictionaryId( const vector< string > & dictionaryFiles ) noexcept
{
  return digestDictionaryPaths( dictionaryFiles, QCryptographicHash::Sha256 );
}

string makeLegacyDictionaryId( const vector< string > & dictionaryFiles ) noexcept
{
  return digestDictionaryPaths( dictionaryFiles, QCryptographicHash::Md5 );
}

bool needToRebuildIndex( const vector< string > & dictionaryFiles, const string & indexFile ) noexcept
{
  Config::Class * cfg = GlobalBroadcaster::instance()->getConfig();

  if ( cfg && !cfg->dictionariesToReindex.isEmpty() ) {
    std::string dictId = makeDictionaryId( dictionaryFiles );

    if ( cfg->dictionariesToReindex.contains( QString::fromStdString( dictId ) ) ) {
      return true;
    }
  }

  qint64 lastModified = 0;

  for ( const auto & dictionaryFile : dictionaryFiles ) {
    QString name = QString::fromUtf8( dictionaryFile.c_str() );
    QFileInfo fileInfo( name );
    qint64 ts;

    if ( fileInfo.isDir() ) {
      continue;
    }

    if ( name.toLower().endsWith( ".zip" ) ) {
      ZipFile::SplitZipFile zf( name );
      if ( !zf.exists() ) {
        return true;
      }
      ts = zf.lastModified().toSecsSinceEpoch();
    }
    else {
      if ( !fileInfo.exists() ) {
        continue;
      }
      ts = fileInfo.lastModified().toMSecsSinceEpoch();
    }

    if ( ts > lastModified ) {
      lastModified = ts;
    }
  }


  QFileInfo fileInfo( indexFile.c_str() );

  if ( !fileInfo.exists() ) {
    return true;
  }

  return fileInfo.lastModified().toMSecsSinceEpoch() < lastModified;
}

string getFtsSuffix()
{
  return "_xpn_v2";
}

vector< string > getObsoleteFtsSuffixes()
{
  return { "_FTS_x", "_FTS_y" };
}

QString generateRandomDictionaryId()
{
  return QCryptographicHash::hash( QUuid::createUuid().toString().toUtf8(), QCryptographicHash::Sha256 )
    .toHex();
}

QMap< std::string, sptr< Dictionary::Class > > dictToMap( const std::vector< sptr< Dictionary::Class > > & dicts )
{
  QMap< std::string, sptr< Dictionary::Class > > dictMap;
  for ( const auto & dict : dicts ) {
    if ( !dict ) {
      continue;
    }
    dictMap.insert( dict.get()->getId(), dict );
  }
  return dictMap;
}
}
