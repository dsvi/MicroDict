#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QString>
#include <QTextDocument>
#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>
#include <QWidget>
#include "filetype.hh"
#include <string>


using std::string;

namespace Utils {

bool isWayland();
QMap< QString, QString > str2map( const QString & contextsEncoded );

inline QString rstrip( const QString & str )
{
  int n = str.size() - 1;
  for ( ; n >= 0; --n ) {
    if ( !str.at( n ).isSpace() ) {
      return str.left( n + 1 );
    }
  }
  return {};
}

inline QString trimQuotes( const QString & str )
{
  const auto * begin = str.cbegin();
  const auto * end   = str.cend();
  while ( begin < end && ( end[ -1 ] == '\'' || end[ -1 ] == '\"' ) ) {
    --end;
  }
  while ( begin < end && ( *begin == '\'' || *begin == '\"' ) ) {
    begin++;
  }
  return str.sliced( begin - str.cbegin(), end - begin );
}

std::string c_string( const QString & str );
bool endsWithIgnoreCase( QByteArrayView str, QByteArrayView extension );
inline QString trimNonChar( const QString & str )
{
  QString remain;
  int n = str.size() - 1;
  for ( ; n >= 0; --n ) {
    auto c = str.at( n );
    if ( !c.isSpace() && !c.isSymbol() && !c.isNonCharacter() && !c.isPunct() && !c.isNull() ) {
      remain = str.left( n + 1 );
      break;
    }
  }

  n = 0;
  for ( ; n < remain.size(); n++ ) {
    auto c = remain.at( n );
    if ( !c.isSpace() && !c.isSymbol() && !c.isNonCharacter() && !c.isPunct() ) {
      return remain.mid( n );
    }
  }

  return "";
}

inline QString rstripnull( const QString & str )
{
  int n = str.size() - 1;
  for ( ; n >= 0; --n ) {
    auto c = str.at( n );
    if ( !c.isSpace() && !c.isNull() ) {
      return str.left( n + 1 );
    }
  }
  return "";
}

inline bool isExternalLink( const QUrl & url )
{
  return url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "ftp" || url.scheme() == "mailto"
    || url.scheme() == "file" || url.toString().startsWith( "//" );
}

inline QString escape( const QString & plain )
{
  return plain.toHtmlEscaped();
}

inline bool ignoreKeyEvent( QKeyEvent * keyEvent )
{
  if ( keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Tab
       || keyEvent->key() == Qt::Key_Backtab || keyEvent->key() == Qt::Key_Escape )
    return true;
  return false;
}

namespace AtomicInt {

inline int loadAcquire( const QAtomicInt & ref )
{
  return ref.loadAcquire();
}

}

QString getAudioMimeType( const QString & path, QString & extension );

namespace Url {

inline QString ensureLeadingSlash( const QString & path )
{
  QLatin1Char slash( '/' );
  if ( path.startsWith( slash ) )
    return path;
  return slash + path;
}

inline bool hasQueryItem( const QUrl & url, const QString & key )
{
  return QUrlQuery( url ).hasQueryItem( key );
}

inline QString queryItemValue( const QUrl & url, const QString & item )
{
  return QUrlQuery( url ).queryItemValue( item, QUrl::FullyDecoded );
}

inline QByteArray encodedQueryItemValue( const QUrl & url, const QString & item )
{
  return QUrlQuery( url ).queryItemValue( item, QUrl::FullyEncoded ).toLatin1();
}

inline void addQueryItem( QUrl & url, const QString & key, const QString & value )
{
  QUrlQuery urlQuery( url );
  urlQuery.addQueryItem( key, value );
  url.setQuery( urlQuery );
}

inline QString getWordFromPathOrHost( const QUrl & url )
{
  auto path = url.path();
  if ( !path.isEmpty() ) {
    return path.startsWith( "/" ) ? path.mid( 1 ) : path;
  }
  auto host = url.host();
  return host != "localhost" ? host : QString();
}

inline void removeQueryItem( QUrl & url, const QString & key )
{
  QUrlQuery urlQuery( url );
  urlQuery.removeQueryItem( key );
  url.setQuery( urlQuery );
}

inline void setQueryItems( QUrl & url, const QList< std::pair< QString, QString > > & query )
{
  QUrlQuery urlQuery( url );
  urlQuery.setQueryItems( query );
  url.setQuery( urlQuery );
}

inline QString path( const QUrl & url )
{
  return url.path( QUrl::FullyDecoded );
}

inline void setFragment( QUrl & url, const QString & fragment )
{
  url.setFragment( fragment, QUrl::DecodedMode );
}

inline QString fragment( const QUrl & url )
{
  return url.fragment( QUrl::FullyDecoded );
}

inline std::pair< bool, QString > getQueryWord( const QUrl & url )
{
  QString word;
  bool validScheme = false;
  if ( url.scheme().compare( "gdlookup" ) == 0 ) {
    validScheme = true;
    if ( hasQueryItem( url, "word" ) ) {
      word = queryItemValue( url, "word" );
    }
    else {
      word = Url::getWordFromPathOrHost( url );
    }
  }
  if ( url.scheme().compare( "bword" ) == 0 || url.scheme().compare( "entry" ) == 0 ) {
    validScheme = true;
    word        = Url::getWordFromPathOrHost( url );
  }
  return std::make_pair( validScheme, word );
}

inline bool isAudioUrl( const QUrl & url )
{
  if ( !url.isValid() ) {
    return false;
  }

  if ( url.scheme() == "gdau" || url.scheme() == "gdprg" || url.scheme() == "gdtts" ) {
    return true;
  }

  return ( url.scheme() == "http" || url.scheme() == "https" )
    && Filetype::isNameOfSound( url.path().toUtf8().data() );
}

QString extractBaseDomain( const QString & domain );

inline QString decodeUrlEncodedWord( QString word )
{
  if ( word.startsWith( QStringLiteral( "xn--" ) ) ) {
    word = QUrl::fromAce( word.toLatin1(), QUrl::IgnoreIDNWhitelist );
  }
  else if ( word.contains( QStringLiteral( "%" ) ) ) {
    word = QUrl::fromPercentEncoding( word.toLatin1() );
  }
  return word;
}

inline QString extractWordFromUrl( const QUrl & url )
{
  QString word = url.authority();

  if ( word.isEmpty() && !url.path().isEmpty() ) {
    word = url.path().startsWith( "/" ) ? url.path().mid( 1 ) : url.path();
  }

  if ( word.endsWith( "/" ) ) {
    word.chop( 1 );
  }

  return word;
}

inline QString extractWordFromUrl( const QString & urlStr )
{
  QString word = extractWordFromUrl( QUrl( urlStr ) );

  if ( word.isEmpty() ) {
    auto schemePos = urlStr.indexOf( "://" );
    if ( schemePos != -1 ) {
      int startPos = schemePos + 3;
      int endPos   = urlStr.indexOf( "/", startPos );

      word = ( endPos == -1 ) ? urlStr.mid( startPos ) : urlStr.mid( startPos, endPos - startPos );
    }
  }

  return decodeUrlEncodedWord( word );
}

}

namespace Path {
QString combine( const QString & path1, const QString & path2 );
}

namespace Widget {
void setNoResultColor( QWidget * widget, bool noResult );
}

namespace Html {
std::string getHtmlCleaner();
}

namespace Fs {

using std::string;

char separator();

string basename( const string & );
void removeDirectory( const QString & directory );

void removeDirectory( const string & directory );

inline QString findFirstExistingFile( std::initializer_list< QString > filePaths )
{
  for ( const QString & filePath : filePaths ) {
    if ( QFileInfo::exists( filePath ) ) {
      return filePath;
    }
  }
  return QString();
}

inline std::string findFirstExistingFile( std::initializer_list< std::string > filePaths )
{
  for ( const std::string & filePath : filePaths ) {
    auto fp = QString::fromStdString( filePath );
    if ( QFileInfo::exists( fp ) ) {
      return filePath;
    }
  }
  return {};
}

inline bool exists( std::string_view filename ) noexcept
{
  return QFileInfo::exists( QString::fromUtf8( filename.data(), filename.size() ) );
}

}

QString escapeAmps( const QString & str );

QString ellipsizeString( const QString & str, int maxLength );

}
