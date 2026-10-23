/* Licensed under GPLv3 or later, see the LICENSE file */

#include "lite_html.hh"
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>
#include <QTextDocument>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QList>

namespace LiteHtml {

namespace {

const QHash< QString, QString > & tagAliases()
{
  static const QHash< QString, QString > aliases = {
    { "article", "div" },      { "section", "div" },   { "header", "div" },
    { "footer", "div" },       { "nav", "div" },       { "aside", "div" },
    { "main", "div" },         { "figure", "div" },    { "figcaption", "div" },
    { "hgroup", "div" },       { "gd-dict-header", "div" }, { "mark", "span" },
    { "summary", "span" },     { "time", "span" },     { "label", "span" },
    { "kbd", "code" },         { "samp", "code" },     { "var", "i" },
    { "abbr", "span" },        { "cite", "i" },        { "q", "span" },
    { "small", "span" },       { "big", "span" },      { "output", "span" },
    { "picture", "span" },     { "hgroupx", "div" },
  };
  return aliases;
}

bool isVoidTag( const QString & t )
{
  static const QSet< QString > v = { "img",   "br",    "hr",     "input",  "meta", "link",  "base",
                                     "area",  "col",   "embed",  "source", "track", "wbr",  "param",
                                     "keygen", "source" };
  return v.contains( t );
}

bool isUnrenderableContainer( const QString & t )
{
  static const QSet< QString > v = { "script",  "noscript", "form",   "button", "select", "textarea",
                                     "object",  "applet",   "video",  "audio",  "canvas", "svg",
                                     "math",    "template", "portal", "dialog", "option", "optgroup",
                                     "fieldset", "legend" };
  return v.contains( t );
}

bool isRawTextTag( const QString & t )
{
  return t == "script" || t == "style" || t == "textarea" || t == "title";
}

QString attrValue( const QString & attrs, const QString & name )
{
  static const QRegularExpression reDouble( R"re(([\w:-]+)\s*=\s*"([^"]*)")re" );
  static const QRegularExpression reSingle( R"re(([\w:-]+)\s*=\s*'([^']*)')re" );
  static const QRegularExpression reBare( R"re(([\w:-]+)\s*=\s*([^\s"'>]+))re" );

  for ( const QRegularExpression & re : { reDouble, reSingle, reBare } ) {
    auto it = re.globalMatch( attrs );
    while ( it.hasNext() ) {
      const auto m = it.next();
      if ( m.captured( 1 ).compare( name, Qt::CaseInsensitive ) == 0 )
        return m.captured( 2 );
    }
  }
  return {};
}

bool hasClass( const QString & classAttr, const QString & cls )
{
  const auto parts = classAttr.split( ' ', Qt::SkipEmptyParts );
  for ( const auto & p : parts ) {
    if ( p.compare( cls, Qt::CaseInsensitive ) == 0 )
      return true;
  }
  return false;
}

const QSet< QString > & alwaysHiddenClasses()
{
  static const QSet< QString > c = {
    "gdfromprefix",
    "gddicticon",
    "collapse_expand_area",
    "gdexpandicon",
    "gdcollapseicon",
    "bglpos",
    "dsl_stress_without_accent",
    "xdxf_k",
    "xdxf_rref",
    "slobdict_headword",
    "mw-empty-elt",
    "hiddenStructure",
    "catlinks-allhidden",
    "printfooter",
    "mainLegend",
    "jump-to-nav",
    "siteSub",
    "topicon",
    "mw-cite-backlink",
    "mw-editsection",
    "nav-popup",
  };
  return c;
}

bool hiddenByDefaultAttr( const QString & attributes )
{
  const QString cls = attrValue( attributes, "class" );
  for ( const auto & c : alwaysHiddenClasses() ) {
    if ( hasClass( cls, c ) )
      return true;
  }
  return false;
}

const QStringList gColourProperties = { QStringLiteral( "color" ),
                                        QStringLiteral( "background" ),
                                        QStringLiteral( "background-color" ),
                                        QStringLiteral( "background-image" ),
                                        QStringLiteral( "background-repeat" ),
                                        QStringLiteral( "background-position" ),
                                        QStringLiteral( "background-size" ),
                                        QStringLiteral( "background-attachment" ),
                                        QStringLiteral( "border-color" ),
                                        QStringLiteral( "border-top-color" ),
                                        QStringLiteral( "border-right-color" ),
                                        QStringLiteral( "border-bottom-color" ),
                                        QStringLiteral( "border-left-color" ),
                                        QStringLiteral( "outline-color" ),
                                        QStringLiteral( "text-decoration-color" ) };

QString stripColours( const QString & css )
{
  QString out;
  int i = 0;

  while ( i < css.size() ) {
    const int open = css.indexOf( QLatin1Char( '{' ), i );

    if ( open < 0 ) {
      out += css.mid( i );
      break;
    }

    out += css.mid( i, open - i + 1 );

    int close = open + 1;
    int depth = 1;

    while ( close < css.size() && depth ) {
      if ( css.at( close ) == QLatin1Char( '{' ) )
        ++depth;
      else if ( css.at( close ) == QLatin1Char( '}' ) )
        --depth;
      if ( depth )
        ++close;
    }

    const QString body = css.mid( open + 1, close - open - 1 );

    if ( body.contains( QLatin1Char( '{' ) ) ) {
      out += body;
      if ( close < css.size() )
        out += QLatin1Char( '}' );
      i = close + 1;
      continue;
    }

    const QStringList parts = body.split( QLatin1Char( ';' ) );

    for ( int k = 0; k < parts.size(); ++k ) {
      const QString & decl = parts.at( k );
      const int colon      = decl.indexOf( QLatin1Char( ':' ) );

      if ( colon < 0 ) {
        if ( !decl.trimmed().isEmpty() ) {
          out += decl;
          if ( k + 1 < parts.size() )
            out += QLatin1Char( ';' );
        }
        continue;
      }

      if ( gColourProperties.contains( decl.left( colon ).trimmed().toLower() ) )
        continue;

      out += decl;
      if ( k + 1 < parts.size() )
        out += QLatin1Char( ';' );
    }

    if ( close < css.size() )
      out += QLatin1Char( '}' );

    i = close + 1;
  }

  return out;
}

QString sanitizeCss( const QString & css )
{
  QString s = css;
  s.remove( QRegularExpression( R"re(@import[^;]*;)re" ) );
  s.remove( QRegularExpression( R"re(@font-face\s*\{[^}]*\})re" ) );

  QString out;
  out.reserve( s.size() );
  static const QRegularExpression atBlock( R"re(@(media|supports|layer|container|scope)[^{]*\{)re" );

  int i = 0;
  while ( i < s.size() ) {
    const auto m = atBlock.match( s, i );
    if ( !m.hasMatch() ) {
      out += s.mid( i );
      break;
    }
    out += s.mid( i, m.capturedStart() - i );
    int pos  = m.capturedEnd();
    int open = pos;
    int depth = 1;
    while ( pos < s.size() && depth > 0 ) {
      if ( s[ pos ] == '{' )
        ++depth;
      else if ( s[ pos ] == '}' )
        --depth;
      ++pos;
    }
    out += sanitizeCss( s.mid( open, pos - open - 1 ) );
    i = pos;
  }
  return stripColours( out );
  return out;
}

QString sheetFromLink( const QString & href )
{
  static QHash< QString, QString > cache;

  const QString key = href.trimmed();

  if ( key.isEmpty() )
    return {};

  if ( cache.contains( key ) )
    return cache.value( key );

  const QUrl url( key );

  if ( url.scheme() != QLatin1String( "qrc" ) && url.scheme() != QLatin1String( "qrcx" ) ) {
    cache.insert( key, QString() );
    return {};
  }

  QString path = url.path();
  if ( !path.startsWith( '/' ) )
    path = '/' + path;

  QFile f( QString( ":" ) + path );

  if ( !f.open( QFile::ReadOnly ) ) {
    qWarning() << "lite: missing stylesheet" << path;
    cache.insert( key, QString() );
    return {};
  }

  const QString css = QString::fromUtf8( f.readAll() ).trimmed();

  if ( css.isEmpty() ) {
    cache.insert( key, QString() );
    return {};
  }

  const QString sheet = QStringLiteral( "<style type=\"text/css\">" ) + sanitizeCss( css )
                        + QStringLiteral( "</style>" );
  cache.insert( key, sheet );
  return sheet;
}

QString placeStyleSheets( QString html, const QString & sheets )
{
  if ( sheets.isEmpty() )
    return html;

  static const QRegularExpression headClose( R"re(</head\s*>)re", QRegularExpression::CaseInsensitiveOption );

  if ( const auto m = headClose.match( html ); m.hasMatch() ) {
    html.insert( m.capturedStart(), sheets );
    return html;
  }

  static const QRegularExpression bodyOpen( R"re(<body[\s>/])re", QRegularExpression::CaseInsensitiveOption );

  if ( const auto m = bodyOpen.match( html ); m.hasMatch() ) {
    html.insert( m.capturedStart(), sheets );
    return html;
  }

  return sheets + html;
}

QString sanitizeAttributes( const QString & tagName, QString attrs )
{
  static const QRegularExpression onRe( R"re((?:\s|\A)on[a-z]+\s*=\s*("[^"]*"|'[^']*'|[^\s">']+))re" );
  attrs.remove( onRe );

  if ( tagName == "a" ) {
    const QString href = attrValue( attrs, "href" ).trimmed();
    if ( href.isEmpty() || href.compare( "#", Qt::CaseInsensitive ) == 0
         || href.startsWith( "javascript:", Qt::CaseInsensitive ) )
      attrs.remove( QRegularExpression( R"re((?:\s|\A)href\s*=\s*("[^"]*"|'[^']*'|[^\s">']+))re" ) );
  }
  return attrs;
}

struct Frame
{
  QString tag;
  QString outTag;
  bool drop = false;
  bool emitted = false;
};

bool dropping( const QList< Frame > & stack )
{
  return !stack.isEmpty() && stack.back().drop;
}

}

bool isHiddenByDefault( [[maybe_unused]] const QString & tagName, const QString & attributes )
{
  return hiddenByDefaultAttr( attributes );
}

QString stripScripts( const QString & html )
{
  QString out = html;
  out.remove( QRegularExpression( R"re(<script\b[^>]*>.*?</script\s*>)re",
                                  QRegularExpression::DotMatchesEverythingOption ) );
  out.remove( QRegularExpression( R"re(<script\b[^>]*/\s*>)re" ) );
  return out;
}

QString prepare( const QString & html, const Options & opt )
{
  QString out;
  out.reserve( html.size() + 64 );

  static const QRegularExpression tagRe(
    R"re(<(/?)([a-zA-Z][a-zA-Z0-9:-]*)((?:"[^"]*"|'[^']*'|[^'">])*)(/?)>)re" );

  QList< Frame > stack;
  QString styleSheets;
  const int n = html.size();
  int i = 0;

  while ( i < n ) {
    if ( html[ i ] != '<' ) {
      int next = html.indexOf( '<', i );
      if ( next < 0 )
        next = n;
      if ( !dropping( stack ) )
        out += html.mid( i, next - i );
      i = next;
      continue;
    }

    if ( html.mid( i, 4 ) == "<!--" ) {
      const int end = html.indexOf( "-->", i + 4 );
      i = ( end < 0 ) ? n : end + 3;
      continue;
    }
    if ( html.mid( i, 2 ) == "<!" ) {
      const int end = html.indexOf( '>', i );
      i = ( end < 0 ) ? n : end + 1;
      continue;
    }

    const auto m = tagRe.match( html, i );
    if ( !m.hasMatch() || m.capturedStart() != i ) {
      if ( !dropping( stack ) )
        out.append( html[ i ] );
      ++i;
      continue;
    }

    const bool closing   = !m.captured( 1 ).isEmpty();
    const QString srcTag = m.captured( 2 ).toLower();
    QString attrs        = m.captured( 3 );
    const bool selfClose = !m.captured( 4 ).isEmpty();
    const int tagEnd     = m.capturedEnd();

    if ( closing ) {
      for ( int k = stack.size() - 1; k >= 0; --k ) {
        if ( stack[ k ].tag == srcTag ) {
          if ( k == stack.size() - 1 && !stack[ k ].drop && stack[ k ].emitted )
            out += "</" + stack[ k ].outTag + ">";
          stack.resize( k );
          break;
        }
      }
      i = tagEnd;
      continue;
    }

    if ( dropping( stack ) ) {
      if ( !isVoidTag( srcTag ) && !selfClose ) {
        Frame f;
        f.tag  = srcTag;
        f.drop = true;
        stack.append( f );
      }
      i = tagEnd;
      continue;
    }

    QString tag = srcTag;
    const auto alias = tagAliases().constFind( tag );
    if ( alias != tagAliases().constEnd() )
      tag = alias.value();

    const QString cls   = attrValue( attrs, "class" );
    const QString style = attrValue( attrs, "style" );
    const QString id    = attrValue( attrs, "id" );

    bool drop = false;

    if ( isUnrenderableContainer( tag ) || tag == "script" || tag == "noscript" || tag == "template"
         || tag == "iframe" || tag == "basefont" )
      drop = true;
    else if ( hiddenByDefaultAttr( attrs ) )
      drop = true;
    else if ( !opt.expandOptionalParts && ( hasClass( cls, "dsl_opt" ) || hasClass( cls, "hidden_expand_opt" ) ) )
      drop = true;
    else if ( QString( style ).remove( ' ' ).contains( "display:none", Qt::CaseInsensitive ) ) {
      const bool articleBody = hasClass( cls, "gdarticlebody" ) || id.startsWith( "gd-" );
      drop                   = !( opt.expandCollapsedArticles && articleBody );
    }

    const bool opensFrame = !isVoidTag( tag ) && !selfClose;

    if ( drop ) {
      if ( opensFrame ) {
        Frame f;
        f.tag  = srcTag;
        f.drop = true;
        stack.append( f );
        if ( isRawTextTag( tag ) ) {
          static const QRegularExpression closeRe( R"re(</\s*(script|style|textarea|title)\s*>)re" );
          const auto cm = closeRe.match( html, tagEnd );
          if ( cm.hasMatch() ) {
            stack.resize( stack.size() - 1 );
            i = cm.capturedEnd();
            continue;
          }
        }
      }
      i = tagEnd;
      continue;
    }

    if ( tag == "style" ) {
      int end = html.indexOf( "</style", tagEnd, Qt::CaseInsensitive );
      if ( end < 0 )
        end = n;
      const QString css = sanitizeCss( html.mid( tagEnd, end - tagEnd ) );
      if ( !css.trimmed().isEmpty() )
        styleSheets += QStringLiteral( "<style type=\"text/css\">" ) + css
                       + QStringLiteral( "</style>" );
      const int gt = html.indexOf( '>', end );
      i            = ( gt < 0 ) ? n : gt + 1;
      continue;
    }

    if ( tag == "link" ) {
      const QString rel  = attrValue( attrs, "rel" ).trimmed();
      const QString type = attrValue( attrs, "type" ).trimmed();

      if ( rel.compare( "stylesheet", Qt::CaseInsensitive ) == 0
           && ( type.isEmpty() || type.compare( "text/css", Qt::CaseInsensitive ) == 0 ) )
        styleSheets += sheetFromLink( attrValue( attrs, "href" ) );
      i = tagEnd;
      continue;
    }

    if ( tag == "title" ) {
      if ( opensFrame ) {
        Frame f;
        f.tag  = srcTag;
        f.drop = true;
        stack.append( f );
      }
      i = tagEnd;
      continue;
    }

    if ( opensFrame ) {
      Frame f;
      f.tag     = srcTag;
      f.outTag  = tag;
      f.drop   = false;
      f.emitted = true;
      stack.append( f );
    }

    QString clean = sanitizeAttributes( tag, attrs );
    if ( tag == "span" || tag == "div" )
      clean.remove( QRegularExpression( R"re(display\s*:\s*none\s*;?)re" ) );
    out += '<' + tag + clean + '>';
    i = tagEnd;
  }

  while ( !stack.isEmpty() ) {
    if ( !stack.back().drop && stack.back().emitted )
      out += "</" + stack.back().tag + ">";
    stack.resize( stack.size() - 1 );
  }

  return placeStyleSheets( out, styleSheets );
}

QString extractTitle( const QString & html )
{
  static const QRegularExpression re( R"re(<title[^>]*>(.*?)</title\s*>)re",
                                      QRegularExpression::DotMatchesEverythingOption );
  const auto m = re.match( html );
  if ( !m.hasMatch() )
    return {};
  QString t = QUrl::fromPercentEncoding( m.captured( 1 ).toUtf8() );
  QTextDocument tmp;
  tmp.setHtml( t );
  return tmp.toPlainText().simplified();
}

}
