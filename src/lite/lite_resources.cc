/* Licensed under GPLv3 or later, see the LICENSE file */

#include "lite_resources.hh"

#include "config.hh"
#include "globalbroadcaster.hh"
#include "utils.hh"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdint.h>

using std::string;

namespace {

struct ExtensionMime
{
  const char * ext;
  const char * mime;
};
const std::array< ExtensionMime, 17 > kExtensionMimeTable = { {
  { "html", "text/html; charset=utf-8" },
  { "htm", "text/html; charset=utf-8" },
  { "css", "text/css; charset=utf-8" },
  { "js", "text/javascript; charset=utf-8" },
  { "mjs", "text/javascript; charset=utf-8" },
  { "png", "image/png" },
  { "jpg", "image/jpeg" },
  { "jpeg", "image/jpeg" },
  { "gif", "image/gif" },
  { "svg", "image/svg+xml" },
  { "webp", "image/webp" },
  { "woff", "font/woff" },
  { "woff2", "font/woff2" },
  { "ttf", "font/ttf" },
  { "mp3", "audio/mpeg" },
  { "wav", "audio/wav" },
  { "ogg", "audio/ogg" },
} };

QByteArray lookupMimeByExtension( const QString & ext )
{
  auto it =
    std::find_if( kExtensionMimeTable.begin(), kExtensionMimeTable.end(), [ &ext ]( const ExtensionMime & entry ) {
      return ext.compare( QLatin1StringView( entry.ext ), Qt::CaseInsensitive ) == 0;
    } );
  if ( it != kExtensionMimeTable.end() ) {
    return QByteArray( it->mime );
  }
  return {};
}

bool sniffHtmlContent( const QByteArray & data )
{
  const QByteArray trimmed = data.trimmed().toLower();
  return trimmed.startsWith( "<!doctype html" ) || trimmed.startsWith( "<html" ) || trimmed.startsWith( "<?xml" );
}

QByteArray withUtf8Charset( const QString & mimeName )
{
  QByteArray result = mimeName.toUtf8();
  if ( result.startsWith( "text/" ) || result == "application/javascript" ) {
    result.append( "; charset=utf-8" );
  }
  return result;
}

}

QByteArray getMimeTypeWithFallback( const QUrl & url, const QByteArray & data )
{
  const QString scheme = url.scheme().toLower();

  if ( scheme == "gdlookup" || scheme == "bword" || scheme == "entry" || scheme == "gdinternal" ) {
    return "text/html; charset=utf-8";
  }

  const QString ext        = QFileInfo( url.path() ).suffix().toLower();
  const QByteArray extMime = lookupMimeByExtension( ext );
  if ( !extMime.isEmpty() ) {
    return extMime;
  }

  QMimeDatabase mimeDb;
  QMimeType urlMime = mimeDb.mimeTypeForUrl( url );
  QString mimeName  = ( urlMime.isValid() && !urlMime.isDefault() ) ? urlMime.name() : QString();

  if ( mimeName.isEmpty() && !data.isEmpty() ) {
    QMimeType dataMime = mimeDb.mimeTypeForData( data );
    if ( dataMime.isValid() && !dataMime.isDefault() ) {
      mimeName = dataMime.name();
    }
  }

  const bool misclassifiedHtml = !data.isEmpty() && sniffHtmlContent( data )
    && ( mimeName.isEmpty() || mimeName == "application/octet-stream" || mimeName == "text/plain" );
  if ( misclassifiedHtml ) {
    return "text/html; charset=utf-8";
  }

  if ( mimeName.isEmpty() ) {
    return "application/octet-stream";
  }
  return withUtf8Charset( mimeName );
}

ArticleResourceManager::ArticleResourceManager( QObject * parent,
                                                 const vector< sptr< Dictionary::Class > > & dictionaries_,
                                                 const ArticleMaker & articleMaker_ ):
  QObject( parent ),
  dictionaries( dictionaries_ ),
  articleMaker( articleMaker_ )
{
}

sptr< Dictionary::DataRequest > ArticleResourceManager::getResource( const QUrl & url )
{
  QString contentType;
  return getResource( url, contentType );
}

string ArticleResourceManager::getHtml( ResourceType resourceType )
{
  switch ( resourceType ) {
    case ResourceType::UNTITLE:
      return articleMaker.makeUntitleHtml();
    case ResourceType::WELCOME:
      return articleMaker.makeWelcomeHtml();
    case ResourceType::BLANK:
      return articleMaker.makeBlankHtml();
    default:
      return {};
  }
}

namespace {
sptr< Dictionary::DataRequest > handleUserFileRequest( const QUrl & url )
{
  QString filePath = url.path().mid( 1 );

  QDir userDir     = Config::getHomeDir();
  QString fullPath = userDir.filePath( filePath );

  QFile file( fullPath );
  if ( file.open( QIODevice::ReadOnly ) ) {
    QByteArray content                         = file.readAll();
    sptr< Dictionary::DataRequestInstant > req = std::make_shared< Dictionary::DataRequestInstant >( true );
    req->getData().resize( content.size() );
    memcpy( &( req->getData().front() ), content.data(), content.size() );
    return req;
  }
  else {
    qWarning( "Failed to open user file: %s", fullPath.toUtf8().data() );
    return {};
  }
}

sptr< Dictionary::DataRequest > handleDictionaryResource(
  const QUrl & url, const string & id, const std::vector< sptr< Dictionary::Class > > & dictionaries )
{
  for ( const auto & dictionary : dictionaries ) {
    if ( dictionary->getId() == id ) {
      if ( url.scheme() == "gico" ) {
        QByteArray bytes;
        QBuffer buffer( &bytes );
        buffer.open( QIODevice::WriteOnly );
        dictionary->getIcon().pixmap( 64 ).save( &buffer, "PNG" );
        buffer.close();
        sptr< Dictionary::DataRequestInstant > ico = std::make_shared< Dictionary::DataRequestInstant >( true );
        ico->getData().resize( bytes.size() );
        memcpy( &( ico->getData().front() ), bytes.data(), bytes.size() );
        return ico;
      }
      try {
        return dictionary->getResource( Utils::Url::path( url ).mid( 1 ).toUtf8().data() );
      }
      catch ( std::exception & e ) {
        qWarning( "getResource request error (%s) in \"%s\"", e.what(), dictionary->getName().c_str() );
        return {};
      }
    }
  }
  return {};
}
}

sptr< Dictionary::DataRequest > ArticleResourceManager::getResource( const QUrl & url, QString & contentType )
{
  const QString scheme = url.scheme();

  if ( scheme == "gdinternal" ) {
    return handleInternalScheme( url, contentType );
  }
  if ( scheme == "gdlookup" ) {
    return handleLookupScheme( url, contentType );
  }

  if ( ( scheme == "bres" || scheme == "gdau" || scheme == "gdvideo" || scheme == "gico" || scheme == "qrcx" )
       && url.path().size() ) {
    return handleResourceScheme( url, contentType );
  }

  if ( scheme == "file" && url.host().isEmpty() ) {
    QString fileName = url.toLocalFile();
    if ( ArticleMaker::adjustFilePath( fileName ) ) {
      QFile f( fileName );
      if ( f.open( QIODevice::ReadOnly ) ) {
        QByteArray content                         = f.readAll();
        sptr< Dictionary::DataRequestInstant > req = std::make_shared< Dictionary::DataRequestInstant >( true );
        req->getData().resize( content.size() );
        if ( !content.isEmpty() ) {
          memcpy( &( req->getData().front() ), content.data(), content.size() );
        }
        contentType = QString::fromLatin1( getMimeTypeWithFallback( QUrl::fromLocalFile( fileName ), content ) );
        return req;
      }
    }
    return {};
  }

  if ( scheme == "http" || scheme == "https" || scheme == "ftp" ) {
    qWarning( "MicroDict has no network support, refusing \"%s\"", url.toEncoded().data() );
  }

  return {};
}

sptr< Dictionary::DataRequest > ArticleResourceManager::handleInternalScheme( const QUrl & url,
                                                                              QString & contentType )
{
  contentType = "text/html; charset=utf-8";
  if ( url.host() == "welcome-page" ) {
    return articleMaker.makeWelcomePage();
  }
  return articleMaker.makeEmptyPage();
}

sptr< Dictionary::DataRequest > ArticleResourceManager::handleLookupScheme( const QUrl & url, QString & contentType )
{
  if ( !url.host().isEmpty() && url.host() != "localhost" ) {
    return std::make_shared< Dictionary::DataRequestInstant >( false );
  }

  contentType  = "text/html; charset=utf-8";
  QString word = Utils::Url::queryItemValue( url, "word" ).trimmed();

  bool groupIsValid = false;
  unsigned group    = Utils::Url::queryItemValue( url, "group" ).toUInt( &groupIsValid );

  if ( QString dictIDs = Utils::Url::queryItemValue( url, "dictionaries" ); !dictIDs.isEmpty() ) {
    return articleMaker.makeDefinitionFor( word, group, {}, {}, dictIDs.split( "," ) );
  }

  QSet< QString > mutedDicts;
  if ( QString muted = Utils::Url::queryItemValue( url, "muted" ); !muted.isEmpty() ) {
    QStringList lists = muted.split( ',' );
    mutedDicts        = QSet< QString >( lists.begin(), lists.end() );
  }
  else if ( const Config::Class * cfg = GlobalBroadcaster::instance()->getConfig() ) {
    const Config::Group * grp = cfg->getGroup( group );
    const QSet< QString > * ms = nullptr;
    if ( group == GroupId::AllGroupId ) {
      ms = &cfg->mutedDictionaries;
    }
    else if ( grp ) {
      ms = &grp->mutedDictionaries;
    }

    if ( ms ) {
      mutedDicts = *ms;
    }
  }

  QMap< QString, QString > contexts = Utils::str2map( Utils::Url::queryItemValue( url, "contexts" ) );
  bool ignoreDiacritics             = Utils::Url::queryItemValue( url, "ignore_diacritics" ) == "1";

  if ( groupIsValid && !word.isEmpty() ) {
    return articleMaker.makeDefinitionFor( word, group, contexts, mutedDicts, {}, ignoreDiacritics );
  }

  return std::make_shared< Dictionary::DataRequestInstant >( false );
}

sptr< Dictionary::DataRequest > ArticleResourceManager::handleResourceScheme( const QUrl & url,
                                                                              QString & contentType )
{
  contentType = QString::fromLatin1( getMimeTypeWithFallback( url ) );
  string id   = url.host().toStdString();

  if ( id == "user" && url.scheme() == "bres" ) {
    return handleUserFileRequest( url );
  }

  return handleDictionaryResource( url, id, dictionaries );
}
