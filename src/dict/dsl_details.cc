/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "dsl_details.hh"

#include "folding.hh"
#include "langcoder.hh"
#include "text.hh"

#include <exception>
#include <stdio.h>
#include <wctype.h>
#include <algorithm>

namespace Dsl {
namespace Details {

using std::list;
using Text::Encoding;

static QMap< int, string > lang_codes = {
  { 1, "en" },    { 1033, "en" }, { 2, "ru" },    { 1049, "ru" }, { 1068, "az" }, { 1025, "ar" }, { 1067, "am" },
  { 15, "af" },   { 1078, "af" }, { 9, "eu" },    { 1069, "eu" }, { 1133, "ba" }, { 21, "be" },   { 1059, "be" },
  { 22, "bg" },   { 1026, "bg" }, { 19, "hu" },   { 1038, "hu" }, { 10, "nl" },   { 1043, "nl" }, { 1032, "el" },
  { 1079, "ka" }, { 13, "da" },   { 1030, "da" }, { 16, "id" },   { 1057, "id" }, { 1039, "is" }, { 6, "es" },
  { 7, "es" },    { 3082, "es" }, { 1034, "es" }, { 5, "it" },    { 1040, "it" }, { 1087, "kk" }, { 1595, "ky" },
  { 28, "ch" },   { 29, "ch" },   { 1028, "ch" }, { 2052, "ch" }, { 30, "la" },   { 1540, "la" }, { 1142, "la" },
  { 1062, "lv" }, { 1063, "lt" }, { 1086, "ms" }, { 3, "de" },    { 26, "de" },   { 1031, "de" }, { 32775, "de" },
  { 14, "nb" },   { 1044, "nb" }, { 25, "nn" },   { 2068, "nn" }, { 20, "pl" },   { 1045, "pl" }, { 8, "pt" },
  { 2070, "pt" }, { 1048, "ro" }, { 23, "sr" },   { 3098, "sr" }, { 1051, "sk" }, { 1060, "sl" }, { 17, "sw" },
  { 1089, "sw" }, { 1064, "tg" }, { 1092, "tt" }, { 27, "tr" },   { 1055, "tr" }, { 1090, "tk" }, { 1091, "tz" },
  { 24, "uk" },   { 1058, "uk" }, { 11, "fi" },   { 1035, "fi" }, { 4, "fr" },    { 1036, "fr" }, { 18, "cs" },
  { 1029, "cs" }, { 12, "sv" },   { 1053, "sv" }, { 1061, "et" } };

string findCodeForDslId( int id )
{
  return lang_codes[ id ];
}

bool isAtSignFirst( const QString & str )
{
  QRegularExpression reg( R"([ \t]*(?:\[[^\]]+\][ \t]*)*@)", QRegularExpression::PatternOption::CaseInsensitiveOption );
  return str.indexOf( reg ) == 0;
}

QString ArticleDom::Node::renderAsText( bool stripTrsTag ) const
{
  if ( !isTag ) {
    return text;
  }

  QString result;

  for ( const auto & i : *this ) {
    if ( !stripTrsTag || i.tagName != QStringLiteral( "!trs" ) ) {
      result += i.renderAsText( stripTrsTag );
    }
  }

  return result;
}

namespace {

bool is_mN( const QString & tagName )
{
  return tagName.size() == 2 && tagName[ 0 ] == u'm' && tagName[ 1 ] >= u'0' && tagName[ 1 ] <= u'9';
}

bool isAnyM( const QString & tagName )
{
  return tagName == QStringLiteral( "m" ) || is_mN( tagName );
}

bool checkM( const QString & dest, const QString & src )
{
  return src == QStringLiteral( "m" ) && is_mN( dest );
}

struct MustTagBeClosed
{
  bool operator()( const ArticleDom::Node * tag ) const
  {
    Q_ASSERT( tag->isTag );
    return !isAnyM( tag->tagName );
  }
};

}

ArticleDom::ArticleDom( const QString & str, const string & dictName, const QString & headword_ ):
  root( Node::Tag(), QString(), QString() ),
  source( str ),
  stringPos( 0 ),
  lineStartPos( 0 ),
  transcriptionCount( 0 ),
  mediaCount( 0 ),
  dictionaryName( dictName ),
  headword( headword_ )
{
  list< Node * > stack;

  Node * textNode = 0;

  try {
    for ( ;; ) {
      nextChar();

      if ( ch == u'@' && !escaped ) {
        if ( !atSignFirstInLine() ) {
          if ( dictName.empty() ) {
            qWarning( "Unescaped '@' symbol found" );
          }
          else {
            qWarning( "Unescaped '@' symbol found in \"%s\"", dictName.c_str() );
          }
        }
        else {
          QString linkTo;
          nextChar();
          for ( ;; nextChar() ) {
            if ( ch == u'\n' ) {
              break;
            }
            if ( ch != u'\r' ) {
              if ( escaped && ( ch == u'(' || ch == ')' ) ) {
                linkTo.push_back( u'\\' );
              }
              linkTo.push_back( ch );
            }
          }
          linkTo = Folding::trimWhitespace( linkTo );

          if ( !linkTo.isEmpty() ) {
            list< QString > allLinkEntries;
            processUnsortedParts( linkTo, true );
            expandOptionalParts( linkTo, &allLinkEntries );

            for ( auto entry = allLinkEntries.begin(); entry != allLinkEntries.end(); ) {
              if ( !textNode ) {
                Node text = Node( Node::Text(), QString() );

                if ( stack.empty() ) {
                  root.push_back( text );
                  stack.push_back( &root.back() );
                }
                else {
                  stack.back()->push_back( text );
                  stack.push_back( &stack.back()->back() );
                }

                textNode = stack.back();
              }
              textNode->text.push_back( u'-' );
              textNode->text.push_back( u' ' );

              stack.pop_back();
              textNode = 0;

              QString linkText = Folding::trimWhitespace( *entry );
              ArticleDom nodeDom( linkText, dictName, headword_ );

              Node link( Node::Tag(), QStringLiteral( "@" ), QString() );
              for ( auto & n : nodeDom.root ) {
                link.push_back( n );
              }

              ++entry;

              if ( stack.empty() ) {
                root.push_back( link );
                if ( entry != allLinkEntries.end() ) {
                  root.push_back( Node( Node::Tag(), QStringLiteral( "br" ), QString() ) );
                }
              }
              else {
                stack.back()->push_back( link );
                if ( entry != allLinkEntries.end() ) {
                  stack.back()->push_back( Node( Node::Tag(), QStringLiteral( "br" ), QString() ) );
                }
              }
            }

            while ( !( ch == u'@' && !escaped && atSignFirstInLine() ) ) {
              nextChar();
            }

            --stringPos;
            ch      = u'\n';
            escaped = false;
          }
        }
      }

      if ( ch == u'[' && !escaped ) {
        bool isClosing;
        QString name;
        QString attrs;

        try {
          do {
            nextChar();
          } while ( Folding::isWhitespace( ch.unicode() ) );

          if ( ch == u'/' && !escaped ) {
            isClosing = true;
            nextChar();
          }
          else {
            isClosing = false;
          }

          while ( ( ch != u']' || escaped ) && !Folding::isWhitespace( ch.unicode() ) ) {
            name.push_back( ch );
            nextChar();
          }

          while ( Folding::isWhitespace( ch.unicode() ) ) {
            nextChar();
          }

          while ( ch != u']' || escaped ) {
            attrs.push_back( ch );
            nextChar();
          }
        }
        catch ( std::exception & ex ) {
          if ( !dictionaryName.empty() ) {
            qWarning( R"(DSL: Unfinished tag "%s" with attributes "%s" found in "%s", article "%s".)",
                      name.toUtf8().constData(),
                      attrs.toUtf8().constData(),
                      dictionaryName.c_str(),
                      headword.toUtf8().constData() );
          }
          else {
            qWarning( R"(DSL: Unfinished tag "%s" with attributes "%s" found)",
                      name.toUtf8().constData(),
                      attrs.toUtf8().constData() );
          }

          throw ex;
        }

        if ( textNode ) {
          stack.pop_back();
          textNode = 0;
        }

        if ( name == QStringLiteral( "t" ) ) {
          if ( isClosing ) {
            if ( transcriptionCount ) {
              --transcriptionCount;
            }
          }
          else {
            ++transcriptionCount;
          }
        }

        if ( name == QStringLiteral( "s" ) ) {
          if ( isClosing ) {
            if ( mediaCount ) {
              --mediaCount;
            }
          }
          else {
            ++mediaCount;
          }
        }

        if ( !isClosing ) {
          if ( isAnyM( name ) ) {
            closeTag( QStringLiteral( "m" ), stack, false );
          }
          openTag( name, attrs, stack );
          if ( name == QStringLiteral( "br" ) ) {
            closeTag( name, stack );
          }
        }
        else {
          closeTag( name, stack );
        }
        continue;
      }

      if ( ch == u'<' && !escaped ) {

        nextChar();

        if ( ch != u'<' || escaped ) {
          --stringPos;

          if ( escaped ) {
            --stringPos;
            escaped = false;
          }
          ch = u'<';
        }
        else {
          do {
            nextChar();
          } while ( Folding::isWhitespace( ch.unicode() ) );

          QString linkTo, linkText;

          for ( ;; nextChar() ) {
            if ( ch == u'>' && !escaped ) {
              nextChar();

              if ( ch == u'>' && !escaped ) {
                break;
              }
              else {
                linkTo.push_back( u'>' );
                linkTo.push_back( ch );

                linkText.push_back( u'>' );
                if ( escaped ) {
                  linkText.push_back( u'\\' );
                }
                linkText.push_back( ch );
              }
            }
            else {
              linkTo.push_back( ch );

              if ( escaped ) {
                linkText.push_back( u'\\' );
              }
              linkText.push_back( ch );
            }
          }

          if ( textNode ) {
            stack.pop_back();
            textNode = 0;
          }

          linkText = Folding::trimWhitespace( linkText );
          processUnsortedParts( linkText, true );
          ArticleDom nodeDom( linkText, dictName, headword_ );

          Node link( Node::Tag(), QStringLiteral( "ref" ), QString() );
          for ( auto & n : nodeDom.root ) {
            link.push_back( n );
          }

          if ( stack.empty() ) {
            root.push_back( link );
          }
          else {
            stack.back()->push_back( link );
          }

          continue;
        }
      }

      if ( ch == u'{' && !escaped ) {

        nextChar();

        if ( ch != u'{' || escaped ) {
          --stringPos;

          if ( escaped ) {
            --stringPos;
            escaped = false;
          }
          ch = u'{';
        }
        else {
          for ( ;; ) {
            nextChar();

            if ( ch == u'}' && !escaped ) {
              nextChar();

              if ( ch == u'}' && !escaped ) {
                break;
              }
            }
          }

          continue;
        }
      }

      if ( !textNode ) {
        Node text = Node( Node::Text(), QString() );

        if ( stack.empty() ) {
          root.push_back( text );
          stack.push_back( &root.back() );
        }
        else {
          stack.back()->push_back( text );
          stack.push_back( &stack.back()->back() );
        }

        textNode = stack.back();
      }

      if ( transcriptionCount ) {
        switch ( ch.unicode() ) {
          case 0x2021:
            ch = QChar( 0xE6 );
            break;
          case 0x407:
            ch = QChar( 0x72 );
            break;
          case 0xB0:
            ch = QChar( 0x6B );
            break;
          case 0x20AC:
            ch = QChar( 0x254 );
            break;
          case 0x404:
            ch = QChar( 0x7A );
            break;
          case 0x40F:
            ch = QChar( 0x283 );
            break;
          case 0xAB:
            ch = QChar( 0x74 );
            break;
          case 0xAC:
            ch = QChar( 0x64 );
            break;
          case 0x2020:
            ch = QChar( 0x259 );
            break;
          case 0x490:
            ch = QChar( 0x6D );
            break;
          case 0xA7:
            ch = QChar( 0x66 );
            break;
          case 0xAE:
            ch = QChar( 0x6C );
            break;
          case 0xB1:
            ch = QChar( 0x67 );
            break;
          case 0x45E:
            ch = QChar( 0x65 );
            break;
          case 0xAD:
            ch = QChar( 0x6E );
            break;
          case 0xA9:
            ch = QChar( 0x73 );
            break;
          case 0xA6:
            ch = QChar( 0x77 );
            break;
          case 0x2026:
            ch = QChar( 0x28C );
            break;
          case 0x452:
            ch = QChar( 0x76 );
            break;
          case 0x408:
            ch = QChar( 0x70 );
            break;
          case 0x40C:
            ch = QChar( 0x75 );
            break;
          case 0x406:
            ch = QChar( 0x68 );
            break;
          case 0xB5:
            ch = QChar( 0x61 );
            break;
          case 0x491:
            ch = QChar( 0x25B );
            break;
          case 0x40A:
            ch = QChar( 0x14B );
            break;
          case 0x2030:
            ch = QChar( 0xF0 );
            break;
          case 0x456:
            ch = QChar( 0x6A );
            break;
          case 0xA4:
            ch = QChar( 0x62 );
            break;
          case 0x409:
            ch = QChar( 0x292 );
            break;
          case 0x40E:
            ch = QChar( 0x69 );
            break;
          case 0x40B:
            ch = QChar( 0x4E8 );
            break;
          case 0xB6:
            ch = QChar( 0x28A );
            break;
          case 0x2018:
            ch = QChar( 0x251 );
            break;
          case 0x457:
            ch = QChar( 0x265 );
            break;
          case 0x458:
            ch = QChar( 0x153 );
            break;
          case 0x405:
            textNode->text.push_back( QChar( 0x153 ) );
            ch = QChar( 0x303 );
            break;
          case 0x441:
            ch = QChar( 0x272 );
            break;
          case 0x442:
            textNode->text.push_back( QChar( 0x254 ) );
            ch = QChar( 0x303 );
            break;
          case 0x443:
            ch = QChar( 0xF8 );
            break;
          case 0x445:
            textNode->text.push_back( QChar( 0x25B ) );
            ch = QChar( 0x303 );
            break;
          case 0x446:
            ch = QChar( 0xE7 );
            break;
          case 0x44C:
            textNode->text.push_back( QChar( 0x251 ) );
            ch = QChar( 0x303 );
            break;
          case 0x44D:
            ch = QChar( 0x26A );
            break;
          case 0x44F:
            ch = QChar( 0x252 );
            break;
          case 0x30:
            ch = QChar( 0x3B2 );
            break;
          case 0x31:
            textNode->text.push_back( QChar( 0x65 ) );
            ch = QChar( 0x303 );
            break;
          case 0x32:
            ch = QChar( 0x25C );
            break;
          case 0x33:
            ch = QChar( 0x129 );
            break;
          case 0x34:
            ch = QChar( 0xF5 );
            break;
          case 0x36:
            ch = QChar( 0x28E );
            break;
          case 0x37:
            ch = QChar( 0x263 );
            break;
          case 0x38:
            ch = QChar( 0x1DD );
            break;
          case 0x3A:
            ch = QChar( 0x2D0 );
            break;
          case 0x27:
            ch = QChar( 0x2C8 );
            break;
          case 0x455:
            ch = QChar( 0x1D0 );
            break;
          case 0xB7:
            ch = QChar( 0xE3 );
            break;

          case 0x00a0:
            ch = QChar( 0x02A7 );
            break;
          case 0x0402:
            textNode->text.push_back( QChar( 0x0069 ) );
            ch = u':';
            break;
          case 0x0403:
            textNode->text.push_back( QChar( 0x0251 ) );
            ch = u':';
            break;
          case 0x0428:
            ch = QChar( 0x0061 );
            break;
          case 0x0453:
            textNode->text.push_back( QChar( 0x0075 ) );
            ch = u':';
            break;
          case 0x201a:
            ch = QChar( 0x0254 );
            break;
          case 0x201e:
            ch = QChar( 0x0259 );
            break;
          case 0x2039:
            textNode->text.push_back( QChar( 0x0064 ) );
            ch = QChar( 0x0292 );
            break;
        }
      }

      if ( escaped && ch == u' ' && mediaCount == 0 ) {
        ch = QChar( 0xA0 );
      }

      textNode->text.push_back( ch );
    }
  }
  catch ( eot & ) {
  }

  if ( textNode ) {
    stack.pop_back();
  }

  if ( !stack.empty() ) {
    auto it = std::find_if( stack.begin(), stack.end(), MustTagBeClosed() );
    if ( it == stack.end() ) {
      return;
    }
    const QByteArray firstTagName = ( *it )->tagName.toUtf8();
    ++it;
    const unsigned unclosedTagCount = 1 + std::count_if( it, stack.end(), MustTagBeClosed() );

    if ( dictName.empty() ) {
      qWarning( "Warning: %u tag(s) were unclosed, first tag name \"%s\".",
                unclosedTagCount,
                firstTagName.constData() );
    }
    else {
      qWarning( "Warning: %u tag(s) were unclosed in \"%s\", article \"%s\", first tag name \"%s\".",
                unclosedTagCount,
                dictName.c_str(),
                headword.toUtf8().constData(),
                firstTagName.constData() );
    }
  }
}

void ArticleDom::openTag( const QString & name, const QString & attrs, list< Node * > & stack )
{
  list< Node > nodesToReopen;

  if ( isAnyM( name ) ) {

    while ( !stack.empty() ) {
      nodesToReopen.emplace_back( Node::Tag(), stack.back()->tagName, stack.back()->tagAttrs );

      if ( stack.back()->empty() ) {

        stack.pop_back();

        Node * parent = !stack.empty() ? stack.back() : &root;

        parent->pop_back();
      }
      else {
        stack.pop_back();
      }
    }
  }

  Node node( Node::Tag(), name, attrs );

  if ( stack.empty() ) {
    root.push_back( node );
    stack.push_back( &root.back() );
  }
  else {
    stack.back()->push_back( node );
    stack.push_back( &stack.back()->back() );
  }

  while ( !nodesToReopen.empty() ) {
    if ( stack.empty() ) {
      root.push_back( nodesToReopen.back() );
      stack.push_back( &root.back() );
    }
    else {
      stack.back()->push_back( nodesToReopen.back() );
      stack.push_back( &stack.back()->back() );
    }

    nodesToReopen.pop_back();
  }
}

void ArticleDom::closeTag( const QString & name, list< Node * > & stack, bool warn )
{

  list< Node * >::reverse_iterator n;

  for ( n = stack.rbegin(); n != stack.rend(); ++n ) {
    if ( ( *n )->tagName == name || checkM( ( *n )->tagName, name ) ) {
      break;
    }
  }

  if ( n != stack.rend() ) {

    while ( !stack.empty() ) {
      bool found = stack.back()->tagName == name || checkM( stack.back()->tagName, name );

      if ( stack.back()->empty() && stack.back()->tagName != QStringLiteral( "br" ) ) {

        stack.pop_back();

        Node * parent = !stack.empty() ? stack.back() : &root;

        parent->pop_back();
      }
      else {
        stack.pop_back();
      }

      if ( found ) {
        break;
      }
    }
  }
  else if ( warn ) {
    if ( !dictionaryName.empty() ) {
      qWarning( R"(No corresponding opening tag for closing tag "%s" found in "%s", article "%s".)",
                name.toUtf8().constData(),
                dictionaryName.c_str(),
                headword.toUtf8().constData() );
    }
    else {
      qWarning( "No corresponding opening tag for closing tag \"%s\" found.", name.toUtf8().constData() );
    }
  }
}

void ArticleDom::nextChar()
{
  if ( stringPos >= source.size() ) {
    throw eot();
  }

  ch = source[ stringPos++ ];

  if ( ch == u'\\' ) {
    if ( stringPos >= source.size() ) {
      throw eot();
    }

    ch = source[ stringPos++ ];

    escaped = true;
  }
  else if ( ch == u'[' && peek() == u'[' ) {
    ++stringPos;
    escaped = true;
  }
  else if ( ch == u']' && peek() == u']' ) {
    ++stringPos;
    escaped = true;
  }
  else {
    escaped = false;
  }

  if ( ch == u'\n' || ch == u'\r' ) {
    lineStartPos = stringPos;
  }
}

bool ArticleDom::atSignFirstInLine()
{
  if ( stringPos <= lineStartPos ) {
    return true;
  }

  return isAtSignFirst( source.mid( lineStartPos ) );
}

DslScanner::DslScanner( const string & fileName ):
  encoding( Text::Encoding::Utf8 ),
  readBufferPtr( readBuffer ),
  readBufferLeft( 0 ),
  linesRead( 0 )
{

  f = gzopen( fileName.c_str(), "rb" );
  if ( !f ) {
    throw exCantOpen( fileName );
  }

  constexpr size_t firstBytesSize = 50;
  unsigned char firstBytes[ firstBytesSize ];

  if ( gzread( f, firstBytes, firstBytesSize ) != firstBytesSize ) {
    gzclose( f );
    throw exMalformedDslFile( fileName );
  }

  bool needExactEncoding = false;

  if ( auto guessedEncoding = QStringConverter::encodingForData( { firstBytes, firstBytesSize }, '#' );
       guessedEncoding.has_value() ) {
    switch ( guessedEncoding.value() ) {
      case QStringConverter::Utf8:
        encoding = Text::Encoding::Utf8;
        break;
      case QStringConverter::Utf16LE:
        encoding = Text::Encoding::Utf16LE;
        break;
      case QStringConverter::Utf16BE:
        encoding = Text::Encoding::Utf16BE;
        break;
      case QStringConverter::Utf32LE:
        encoding = Text::Encoding::Utf16LE;
        break;
      case QStringConverter::Utf32BE:
        encoding = Text::Encoding::Utf32BE;
        break;
      default:
        break;
    }
  }

  qDebug() << "DSL encoding:" << getEncodingNameFor( encoding );

  if ( gzrewind( f ) ) {
    gzclose( f );
    throw exCantOpen( fileName );
  }

  lineFeed = Text::initLineFeed( encoding );

  QString str;
  size_t offset;

  for ( ;; ) {
    if ( !readNextLine( str, offset ) ) {
      gzclose( f );
      throw exMalformedDslFile( fileName );
    }

    if ( str.isEmpty() || str[ 0 ] != u'#' ) {
      break;
    }

    bool isName      = false;
    bool isLangFrom  = false;
    bool isLangTo    = false;
    bool isSoundDict = false;

    if ( str.startsWith( u"#NAME" ) ) {
      isName = true;
    }
    else if ( str.startsWith( u"#INDEX_LANGUAGE" ) ) {
      isLangFrom = true;
    }
    else if ( str.startsWith( u"#CONTENTS_LANGUAGE" ) ) {
      isLangTo = true;
    }
    else if ( str.startsWith( u"#SOUND_DICTIONARY" ) ) {
      isSoundDict = true;
    }
    else if ( !str.startsWith( u"#SOURCE_CODE_PAGE" ) ) {
      continue;
    }

    const qsizetype beg = str.indexOf( u'"' );

    if ( beg < 0 ) {
      throw exMalformedDslFile( fileName );
    }

    const qsizetype end = str.lastIndexOf( u'"' );

    if ( end == beg ) {
      throw exMalformedDslFile( fileName );
    }

    QString arg = str.mid( beg + 1, end - beg - 1 );

    if ( isName ) {
      dictionaryName = arg;
    }
    else if ( isLangFrom ) {
      langFrom = arg;
    }
    else if ( isLangTo ) {
      langTo = arg;
    }
    else if ( isSoundDict ) {
      soundDictionary = arg;
    }
    else {
      if ( !needExactEncoding ) {
        qWarning( "Warning: encoding was specified in a Unicode file, ignoring." );
      }
      else if ( arg == u"Latin" ) {
        encoding = Text::Encoding::Windows1252;
      }
      else if ( arg == u"Cyrillic" ) {
        encoding = Text::Encoding::Windows1251;
      }
      else if ( arg == u"EasternEuropean" ) {
        encoding = Text::Encoding::Windows1250;
      }
      else {
        gzclose( f );
        throw exUnknownCodePage();
      }
    }
  }

  if ( gzdirect( f ) ) {
    gzrewind( f );
  }
  gzseek( f, offset, SEEK_SET );
  readBufferPtr  = readBuffer;
  readBufferLeft = 0;
}

DslScanner::~DslScanner() noexcept
{
  gzclose( f );
}

bool DslScanner::readNextLine( QString & out, size_t & offset, bool only_head_word )
{
  offset = gztell( f ) - readBufferLeft;

  for ( ;; ) {
    if ( readBufferLeft < 5000 ) {
      if ( !gzeof( f ) ) {
        memmove( readBuffer, readBufferPtr, readBufferLeft );

        const int result = gzread( f, readBuffer + readBufferLeft, sizeof( readBuffer ) - readBufferLeft );

        if ( result == -1 ) {
          throw exCantReadDslFile();
        }

        readBufferPtr = readBuffer;
        readBufferLeft += (size_t)result;
      }
    }
    if ( readBufferLeft <= 0 ) {
      return false;
    }

    int pos = Text::findFirstLinePosition( readBufferPtr, readBufferLeft, lineFeed.lineFeed, lineFeed.length );
    if ( pos == -1 ) {
      return false;
    }
    QString line = Iconv::toQString( getEncodingNameFor( encoding ), readBufferPtr, pos );
    line         = Utils::rstrip( line );

    if ( pos > readBufferLeft ) {
      pos = readBufferLeft;
    }
    readBufferLeft -= pos;
    readBufferPtr += pos;
    linesRead++;
    if ( only_head_word && ( line.isEmpty() || line.at( 0 ).isSpace() ) ) {
      continue;
    }
    out = line;
    return true;
  }
}

bool DslScanner::readNextLineWithoutComments( QString & out, size_t & offset, bool only_headword )
{
  QString str;
  bool commentToNextLine = false;
  size_t currentOffset;

  out.clear();
  offset = 0;

  do {
    bool b = readNextLine( str, currentOffset, only_headword );

    if ( offset == 0 ) {
      offset = currentOffset;
    }

    if ( !b ) {
      return false;
    }

    stripComments( str, commentToNextLine );

    out += str;
  } while ( commentToNextLine );

  return true;
}

void processUnsortedParts( QString & str, bool strip )
{
  int refCount = 0;

  qsizetype startPos = 0;

  for ( qsizetype x = 0; x < str.size(); ) {
    const QChar ch = str[ x ];

    if ( ch == u'\\' ) {
      x += 2;
      continue;
    }

    if ( ch == '{' ) {
      ++refCount;

      if ( !strip ) {
        str.remove( x, 1 );
        continue;
      }
      else if ( refCount == 1 ) {
        startPos = x;
      }
    }
    else if ( ch == '}' ) {
      --refCount;

      if ( refCount < 0 ) {
        qWarning( "Warning: an unmatched closing brace was encountered." );
        refCount = 0;
        str.remove( x, 1 );
        continue;
      }

      if ( !strip ) {
        str.remove( x, 1 );
        continue;
      }
      else if ( !refCount ) {
        str.remove( startPos, x - startPos + 1 );
        x = startPos;

        continue;
      }
    }

    ++x;
  }

  if ( strip && refCount ) {
    qWarning( "Warning: unclosed brace(s) encountered." );
    str.truncate( startPos );
  }
}

void expandOptionalParts( QString & str, list< QString > * result, qsizetype x, bool inside_recurse )
{
  if ( str.size() > 100 ) {
    result->push_back( str );
    return;
  }

  list< QString > expanded;
  list< QString > * headwords;
  headwords = inside_recurse ? result : &expanded;

  if ( headwords->size() >= 32 ) {
    return;
  }

  for ( ; x < str.size(); ) {
    const QChar ch = str[ x ];

    if ( ch == u'\\' ) {
      x += 2;
    }
    else if ( ch == u'(' ) {

      {
        int refCount = 1;

        for ( qsizetype y = x + 1; y < str.size(); ++y ) {
          const QChar ch = str[ y ];

          if ( ch == u'\\' ) {
            ++y;
          }
          else if ( ch == u'(' ) {
            ++refCount;
          }
          else if ( ch == u')' ) {
            if ( !--refCount ) {

              if ( y != x + 1 ) {
                QString removed = str.left( x );
                removed.append( str.mid( y + 1, str.size() - y - 1 ) );

                expandOptionalParts( removed, headwords, x, true );
              }

              break;
            }
          }
        }

        if ( refCount && x != str.size() - 1 ) {

          QString removed = str.left( x );

          if ( headwords->size() < 32 ) {
            headwords->push_back( removed );
          }
          else {
            if ( !inside_recurse ) {
              result->splice( result->end(), expanded );
            }
            return;
          }
        }
      }

      str.remove( x, 1 );
    }
    else if ( ch == u')' ) {
      str.remove( x, 1 );
    }
    else {
      ++x;
    }
  }

  if ( headwords->size() < 32 ) {
    headwords->push_back( str );
  }
  if ( !inside_recurse ) {
    result->splice( result->end(), expanded );
  }
}

static const QString openBraces( QStringLiteral( "{{" ) );
static const QString closeBraces( QStringLiteral( "}}" ) );

void stripComments( QString & str, bool & nextLine )
{
  qsizetype n = 0, n2 = 0;

  for ( ;; ) {
    if ( nextLine ) {
      n = str.indexOf( closeBraces, n2 );
      if ( n < 0 ) {
        str.remove( n2, n );
        return;
      }
      str.remove( n2, n - n2 + 2 );
      nextLine = false;
    }

    n = str.indexOf( openBraces, n2 );
    if ( n < 0 ) {
      return;
    }
    nextLine = true;
    n2       = n;
  }
}

void expandTildes( QString & str, const QString & tildeReplacement )
{
  QString tildeValue = Folding::trimWhitespace( tildeReplacement );
  for ( qsizetype x = 0; x < str.size(); ) {
    if ( str[ x ] == u'\\' ) {
      x += 2;
    }
    else if ( str[ x ] == u'~' ) {
      if ( x > 0 && str[ x - 1 ] == '^' && ( x < 2 || str[ x - 2 ] != '\\' ) ) {
        str.replace( x - 1, 2, tildeValue );
        str[ x - 1 ] = str[ x - 1 ].isUpper() ? str[ x - 1 ].toLower() : str[ x - 1 ].toUpper();
        x = x - 1 + tildeValue.size();
      }
      else {
        str.replace( x, 1, tildeValue );
        x += tildeValue.size();
      }
    }
    else {
      ++x;
    }
  }
}

void unescapeDsl( QString & str )
{
  for ( qsizetype x = 0; x < str.size(); ++x ) {
    if ( str[ x ] == u'\\' ) {
      str.remove( x, 1 );
    }
  }
}

void normalizeHeadword( QString & str )
{
  for ( qsizetype x = str.size(); x-- > 1; ) {
    if ( str[ x ] == u' ' ) {
      qsizetype y;
      for ( y = x; y && ( str[ y - 1 ] == u' ' ); --y ) {
        ;
      }

      if ( y != x ) {

        str.remove( y, x - y );

        x = y;
      }
    }
  }
  if ( !str.isEmpty() && str[ str.size() - 1 ] == u' ' ) {
    str.remove( str.size() - 1, 1 );
  }
  if ( !str.isEmpty() && str[ 0 ] == u' ' ) {
    str.remove( 0, 1 );
  }
}

namespace {
void cutEnding( QString & where, const QString & ending )
{
  if ( where.size() > ending.size() && where.endsWith( ending ) ) {
    where.truncate( where.size() - ending.size() );
  }
}
}

quint32 dslLanguageToId( const QString & name )
{
  static QString newSp( QStringLiteral( "newspelling" ) );
  static QString st( QStringLiteral( "standard" ) );
  static QString ms( QStringLiteral( "modernsort" ) );
  static QString ts( QStringLiteral( "traditionalsort" ) );
  static QString prc( QStringLiteral( "prc" ) );

  QString nameStripped = Folding::apply( name );

  cutEnding( nameStripped, newSp );
  cutEnding( nameStripped, st );
  cutEnding( nameStripped, ms );
  cutEnding( nameStripped, ts );
  cutEnding( nameStripped, prc );

  return LangCoder::findIdForLanguage( nameStripped );
}

}
}
