#include "utils.hh"
#include <QDir>
#include <QMessageBox>
#include <string>
#include <QBuffer>
#include <QMimeDatabase>
#include <QGuiApplication>

using std::string;
namespace Utils {

bool isWayland()
{
  return QGuiApplication::platformName().startsWith( "wayland", Qt::CaseInsensitive );
}

QMap< QString, QString > str2map( const QString & contextsEncoded )
{
  QMap< QString, QString > contexts;

  if ( contextsEncoded.size() ) {
    QByteArray ba = QByteArray::fromBase64( contextsEncoded.toLatin1() );
    QBuffer buf( &ba );
    buf.open( QBuffer::ReadOnly );
    QDataStream stream( &buf );
    stream >> contexts;
  }
  return contexts;
}
std::string c_string( const QString & str )
{
  return std::string( str.toUtf8().constData() );
}

bool endsWithIgnoreCase( QByteArrayView str, QByteArrayView extension )
{
  return ( str.size() >= extension.size() )
    && ( str.last( extension.size() ).compare( extension, Qt::CaseInsensitive ) == 0 );
}

QString escapeAmps( const QString & str )
{
  QString result( str );
  result.replace( "&", "&&" );
  return result;
}

QString ellipsizeString( const QString & str, int maxLength )
{
  if ( str.length() <= maxLength ) {
    return str;
  }

  int spacePos = str.lastIndexOf( ' ', maxLength );
  if ( spacePos > 0 ) {
    return str.left( spacePos ) + "...";
  }
  else {
    return str.left( maxLength ) + "...";
  }
}
}

QString Utils::Path::combine( const QString & path1, const QString & path2 )
{
  return QDir::cleanPath( path1 + QDir::separator() + path2 );
}

QString Utils::Url::extractBaseDomain( const QString & domain )
{
  QStringList parts = domain.split( '.' );
  if ( parts.size() >= 3 ) {
    QString secondLevel = parts[ parts.size() - 2 ];
    QString topLevel    = parts[ parts.size() - 1 ];

    if ( ( secondLevel == "com" || secondLevel == "co" || secondLevel == "org" || secondLevel == "gov"
           || secondLevel == "net" || secondLevel == "edu" )
         && ( topLevel.length() == 2 || topLevel.length() == 3 ) ) {
      if ( parts.size() >= 3 ) {
        return parts[ parts.size() - 3 ] + "." + secondLevel;
      }
      return secondLevel + "." + topLevel;
    }
  }

  int dotCount = domain.count( '.' );
  if ( dotCount >= 2 ) {
    if ( parts.isEmpty() ) {
      parts = domain.split( '.' );
    }
    if ( parts.size() >= 2 ) {
      return parts[ parts.size() - 2 ] + "." + parts[ parts.size() - 1 ];
    }
  }

  return domain;
}

void Utils::Widget::setNoResultColor( QWidget * widget, bool noResult )
{
  (void)widget;
  (void)noResult;
}

std::string Utils::Html::getHtmlCleaner()
{
  return R"(</font></font></font></font></font></font></font></font></font></font></font></font>
                     </b></b></b></b></b></b></b></b>
                     </i></i></i></i></i></i></i></i>
                     </a></a></a></a></a></a></a></a>)";
}


namespace Utils::Fs {

char separator()
{
  return QDir::separator().toLatin1();
}

std::string basename( const std::string & str )
{
  size_t x = str.rfind( separator() );

  if ( x == std::string::npos ) {
    return str;
  }

  return std::string( str, x + 1 );
}

void removeDirectory( const QString & directory )
{
  QDir dir( directory );
  dir.removeRecursively();
}

void removeDirectory( const string & directory )
{
  removeDirectory( QString::fromStdString( directory ) );
}

}

QString Utils::getAudioMimeType( const QString & path, QString & extension )
{
  extension        = ".wav";
  QString mimeType = "audio/wav";

  QMimeDatabase mimeDb;
  QFileInfo fileInfo( path );
  if ( !fileInfo.suffix().isEmpty() ) {
    extension      = "." + fileInfo.suffix().toLower();
    QMimeType mime = mimeDb.mimeTypeForFile( fileInfo, QMimeDatabase::MatchExtension );
    if ( mime.isValid() && mime.name().startsWith( "audio/" ) ) {
      mimeType = mime.name();
    }
  }

  return mimeType;
}
