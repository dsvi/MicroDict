/* Licensed under GPLv3 or later, see the LICENSE file */

#include "lite_fonts.hh"

#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>

namespace LiteFonts {

namespace {

QString genericName( Family which )
{
  switch ( which ) {
    case Serif:
      return QStringLiteral( "serif" );
    case SansSerif:
      return QStringLiteral( "sans-serif" );
    case Monospace:
      return QStringLiteral( "monospace" );
    case Standard:
    default:
      return QString();
  }
}

QFont::StyleHint styleHintFor( Family which )
{
  switch ( which ) {
    case Serif:
      return QFont::Serif;
    case SansSerif:
      return QFont::SansSerif;
    case Monospace:
      return QFont::Monospace;
    case Standard:
    default:
      return QFont::AnyStyle;
  }
}

}

QFont defaultFamily( Family which )
{
  if ( which == Standard ) {
    QFont font = QFontDatabase::systemFont( QFontDatabase::GeneralFont );
    font.setStyleStrategy( QFont::PreferMatch );
    return font;
  }

  QFont font( genericName( which ) );
  font.setStyleHint( styleHintFor( which ) );
  font.setStyleStrategy( QFont::PreferMatch );
  return font;
}

QString styleSheet()
{
  auto face = []( Family which ) {
    const QFont requested  = defaultFamily( which );
    const QString resolved = QFontInfo( requested ).family();
    return resolved.isEmpty() ? requested.family() : resolved;
  };

  return QStringLiteral( "body, p, div, td, th, li, span { font-family: '%1'; }"
                         "serif, .serif { font-family: '%2'; }"
                         "sans-serif, .sans-serif { font-family: '%3'; }"
                         "monospace, tt, code, pre, kbd, samp { font-family: '%4'; }" )
    .arg( face( Standard ), face( Serif ), face( SansSerif ), face( Monospace ) );
}

}
