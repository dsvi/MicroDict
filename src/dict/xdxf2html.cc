/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "xdxf2html.hh"
#include <QtXml>
#include "text.hh"
#include "folding.hh"

#include "audiolink.hh"
#include "dictfile.hh"
#include "filetype.hh"
#include "htmlescape.hh"
#include "utils.hh"
#include "xdxf.hh"
#include "globalregex.hh"

namespace Xdxf2Html {

static void fixLink( QDomElement & el, const string & dictId, const char * attrName )
{
  QUrl url;
  url.setScheme( "bres" );
  url.setHost( QString::fromStdString( dictId ) );
  url.setPath( Utils::Url::ensureLeadingSlash( el.attribute( attrName ) ) );

  el.setAttribute( attrName, url.toEncoded().data() );
}

string convertToRoman( int input, int lower_case )
{
  string romanvalue = "";
  if ( input >= 4000 ) {
    int x      = ( input - input % 4000 ) / 1000;
    romanvalue = "(" + convertToRoman( x, lower_case ) + ")";
    input %= 4000;
  }

  const string roman[ 26 ] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I",
                               "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  const int decimal[ 13 ]  = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };

  for ( int i = 0; i < 13; i++ ) {
    while ( input >= decimal[ i ] ) {
      input -= decimal[ i ];
      if ( lower_case == 1 ) {
        romanvalue += roman[ i + 13 ];
      }
      else {
        romanvalue += roman[ i ];
      }
    }
  }
  return romanvalue;
}

QDomElement fakeElement( QDomDocument & dom )
{
  return dom.createElement( "b" );
}

string convert( const string & in,
                DICT_TYPE type,
                const map< string, string > * pAbrv,
                Dictionary::Class * dictPtr,
                bool isLogicalFormat,
                unsigned revisionNumber,
                QString * headword )
{

  string inConverted;

  inConverted.reserve( in.size() );

  bool afterEol = false;

  for ( char i : in ) {
    switch ( i ) {
      case '\n':
        afterEol = true;
        if ( !isLogicalFormat ) {
          inConverted.append( "<br/>" );
        }
        break;

      case '\r':
        break;

      case ' ':
        if ( afterEol ) {
          if ( !isLogicalFormat ) {
            inConverted.append( "&#160;" );
          }
          break;
        }
        [[fallthrough]];
      default:
        inConverted.push_back( i );
        afterEol = false;
    }
  }

  QDomDocument dd;

  QString errorStr;

  string in_data;
  if ( type == XDXF ) {
    in_data = "<div class=\"xdxf\"";
    if ( dictPtr->isToLanguageRTL() ) {
      in_data += " dir=\"rtl\"";
    }
    in_data += ">";
  }
  else {
    in_data = "<div class=\"sdct_x\">";
  }
  in_data += inConverted + "</div>";


#if ( QT_VERSION < QT_VERSION_CHECK( 6, 5, 0 ) )
  int errorLine, errorColumn;
  if ( !dd.setContent( QByteArray( in_data.c_str() ), false, &errorStr, &errorLine, &errorColumn ) ) {
    qWarning( "Xdxf2html error, xml parse failed: %s at %d,%d",
              errorStr.toLocal8Bit().constData(),
              errorLine,
              errorColumn );
    qWarning( "The input was: %s", in_data.c_str() );
    return in;
  }
#else
  auto setContentResult = dd.setContent( QByteArray::fromStdString( in_data ) );
  if ( !setContentResult ) {
    qWarning( "Xdxf2html error, xml parse failed: %s at %lld,%lld",
              setContentResult.errorMessage.toStdString().c_str(),
              setContentResult.errorLine,
              setContentResult.errorColumn );
    qWarning( "The input was: %s", in_data.c_str() );
    return in;
  }
#endif

  QDomNodeList nodes = dd.elementsByTagName( "ex" );

  while ( nodes.size() ) {
    QString author, source;
    QDomElement el = nodes.at( 0 ).toElement();

    author = el.attribute( "author", QString() );
    source = el.attribute( "source", QString() );

    if ( el.hasChildNodes() ) {
      QDomNodeList lst = el.childNodes();
      for ( int i = 0; i < lst.count(); ++i ) {
        QDomElement el2 = el.childNodes().at( i ).toElement();
        if ( el2.tagName().compare( "ex_orig", Qt::CaseInsensitive ) == 0 ) {
          el2.setTagName( "span" );
          el2.setAttribute( "class", "xdxf_ex_orig" );
        }
        else if ( el2.tagName().compare( "ex_tran", Qt::CaseInsensitive ) == 0 ) {
          el2.setTagName( "span" );
          el2.setAttribute( "class", "xdxf_ex_tran" );
          el2.insertBefore( dd.createTextNode( QStringLiteral( "— " ) ), el2.firstChild() );
        }
      }
    }
    if ( ( !author.isEmpty() || !source.isEmpty() ) && ( !el.text().isEmpty() || !el.childNodes().isEmpty() ) ) {
      QDomElement el2 = dd.createElement( "span" );
      el2.setAttribute( "class", "xdxf_ex_source" );
      QString text = author;
      if ( !source.isEmpty() ) {
        if ( !text.isEmpty() ) {
          text += ", ";
        }
        text += source;
      }
      QDomText txtNode = dd.createTextNode( QStringLiteral( "[" ) + text + QStringLiteral( "]" ) );
      el2.appendChild( txtNode );
      el.appendChild( el2 );
    }

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_ex" );
    }
    else {
      el.setAttribute( "class", "xdxf_ex_old" );
    }
  }

  nodes = dd.elementsByTagName( "mrkd" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    el.setAttribute( "class", "xdxf_ex_markd" );
  }

  nodes = dd.elementsByTagName( "k" );

  if ( headword ) {
    headword->clear();
  }

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    if ( type == STARDICT ) {
      el.setTagName( "span" );
      el.setAttribute( "class", "xdxf_k" );
    }
    else {
      if ( headword && headword->isEmpty() ) {
        *headword = el.text();
      }

      el.setTagName( "div" );
      el.setAttribute( "class", "xdxf_headwords" );
      bool isLanguageRtl = dictPtr->isFromLanguageRTL();
      if ( el.hasAttribute( "xml:lang" ) ) {
        QString lang = el.attribute( "xml:lang" );
        el.removeAttribute( "xml:lang" );
        el.setAttribute( "lang", lang );

        quint32 langID = Xdxf::getLanguageId( lang );
        if ( langID ) {
          isLanguageRtl = LangCoder::isLanguageRTL( langID );
        }
      }
      if ( isLanguageRtl != dictPtr->isToLanguageRTL() ) {
        el.setAttribute( "dir", isLanguageRtl ? "rtl" : "ltr" );
      }
    }
  }

  if ( isLogicalFormat ) {
    nodes = dd.elementsByTagName( "def" );

    int maxNestingDepth = 1;
    for ( int i = 0; i < nodes.size(); i++ ) {
      QDomElement el          = nodes.at( i ).toElement();
      QDomElement nestingNode = el;
      int nestingCount        = 0;
      while ( nestingNode.parentNode().toElement().tagName() == "def" ) {
        nestingCount++;
        nestingNode = nestingNode.parentNode().toElement();
      }
      if ( nestingCount > maxNestingDepth ) {
        maxNestingDepth = nestingCount;
      }
    }
    for ( int j = maxNestingDepth; j > 0; j-- ) {
      int siblingCount   = 0;
      QString numberText = "";
      for ( int i = 0; i < nodes.size(); i++ ) {
        QDomElement el          = nodes.at( i ).toElement();
        QDomElement nestingNode = el;
        int nestingDepth = 0;
        while ( nestingNode.parentNode().toElement().tagName() == "def" ) {
          nestingDepth++;
          nestingNode = nestingNode.parentNode().toElement();
        }
        if ( nestingDepth == j ) {
          siblingCount++;
          if ( maxNestingDepth == 1 ) {
            numberText = numberText.setNum( siblingCount ) + ". ";
          }
          else if ( maxNestingDepth == 2 ) {
            if ( nestingDepth == 1 ) {
              numberText = numberText.setNum( siblingCount ) + ". ";
            }
            if ( nestingDepth == 2 ) {
              numberText = numberText.setNum( siblingCount ) + ") ";
            }
          }
          else {
            if ( nestingDepth == 1 ) {
              numberText = QString::fromStdString( convertToRoman( siblingCount, 0 ) + ". " );
            }
            if ( nestingDepth == 2 ) {
              numberText = numberText.setNum( siblingCount ) + ". ";
            }
            if ( nestingDepth == 3 ) {
              numberText = numberText.setNum( siblingCount ) + ") ";
            }
            if ( nestingDepth == 4 ) {
              numberText = QString::fromStdString( convertToRoman( siblingCount, 1 ) + ") " );
            }
          }
          QDomElement numberNode = dd.createElement( "span" );
          numberNode.setAttribute( "class", "xdxf_num" );
          QDomText text_num = dd.createTextNode( numberText );
          numberNode.appendChild( text_num );
          el.insertBefore( numberNode, el.firstChild() );

          if ( el.hasAttribute( "cmt" ) ) {
            QDomElement cmtNode = dd.createElement( "span" );
            cmtNode.setAttribute( "class", "xdxf_co" );
            QDomText text_num =
              dd.createTextNode( QStringLiteral( "(" ) + el.attribute( "cmt" ) + QStringLiteral( ")" ) );
            cmtNode.appendChild( text_num );
            el.insertAfter( cmtNode, el.firstChild() );
          }
        }
        else if ( nestingDepth < j ) {
          siblingCount = 0;
        }
      }
    }
    while ( nodes.size() ) {
      QDomElement el = nodes.at( 0 ).toElement();
      el.setTagName( "span" );
      el.setAttribute( "class", "xdxf_def" );
      bool isLanguageRtl = dictPtr->isToLanguageRTL();
      if ( el.hasAttribute( "xml:lang" ) ) {
        QString lang = el.attribute( "xml:lang" );
        el.removeAttribute( "xml:lang" );
        el.setAttribute( "lang", lang );

        quint32 langID = Xdxf::getLanguageId( lang );
        if ( langID ) {
          isLanguageRtl = LangCoder::isLanguageRTL( langID );
        }
      }
      if ( isLanguageRtl != dictPtr->isToLanguageRTL() ) {
        el.setAttribute( "dir", isLanguageRtl ? "rtl" : "ltr" );
      }
    }
  }

  nodes = dd.elementsByTagName( "opt" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    el.setAttribute( "class", "xdxf_opt" );
  }

  nodes = dd.elementsByTagName( "kref" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "a" );
    el.setAttribute( "href", QString( "bword:" ) + el.text() );
    el.setAttribute( "class", "xdxf_kref" );
    if ( el.hasAttribute( "idref" ) ) {
      el.setAttribute( "href", QString( "bword:" ) + el.text() + "#" + el.attribute( "idref" ) );
    }
    if ( el.hasAttribute( "kcmt" ) ) {
      QDomText kcmtText = dd.createTextNode( " " + el.attribute( "kcmt" ) );
      el.parentNode().insertAfter( kcmtText, el );
    }
  }

  nodes = dd.elementsByTagName( "iref" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    QString ref = el.attribute( "href" );
    if ( ref.isEmpty() ) {
      ref = el.text();
    }

    el.setAttribute( "href", ref );
    el.setTagName( "a" );
  }

  if ( revisionNumber < 29 ) {
    nodes = dd.elementsByTagName( "abr" );
  }
  else {
    nodes = dd.elementsByTagName( "abbr" );
  }

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    el.setAttribute( "class", "xdxf_abbr" );
    if ( type == XDXF && pAbrv != nullptr ) {
      string val = Folding::trimWhitespace( el.text() ).toStdString();

      auto i = pAbrv->find( val );

      if ( i != pAbrv->end() ) {
        string title;

        if ( Text::toQString( i->second ).size() < 70 ) {
          title.reserve( i->second.size() );

          for ( const char * c = i->second.c_str(); *c; ++c ) {
            if ( *c == ' ' || *c == '\t' ) {
              title.push_back( 0xC2 );
              title.push_back( 0xA0 );
            }
            else if ( *c == '-' ) {
              title.push_back( 0xE2 );
              title.push_back( 0x80 );
              title.push_back( 0x91 );
            }
            else {
              title.push_back( *c );
            }
          }
        }
        else {
          title = i->second;
        }
        el.setAttribute( "title", Text::toQString( title ) );
      }
    }
  }

  nodes = dd.elementsByTagName( "dtrn" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    el.setAttribute( "class", "xdxf_dtrn" );
  }

  nodes = dd.elementsByTagName( "c" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );

    if ( el.hasAttribute( "c" ) ) {
      el.setAttribute( "style", "color:" + el.attribute( "c" ) );
      el.removeAttribute( "c" );
    }
    else {
      el.setAttribute( "class", "xdxf_c" );
    }
  }

  nodes = dd.elementsByTagName( "co" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_co" );
      el.insertBefore( dd.createTextNode( QStringLiteral( "(" ) ), el.firstChild() );
      el.appendChild( dd.createTextNode( QStringLiteral( ")" ) ) );
    }
    else {
      el.setAttribute( "class", "xdxf_co_old" );
    }
  }

  nodes = dd.elementsByTagName( "gr" );
  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_gr" );
    }
    else {
      el.setAttribute( "class", "xdxf_gr_old" );
    }
  }
  nodes = dd.elementsByTagName( "pos" );
  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_gr" );
    }
    else {
      el.setAttribute( "class", "xdxf_gr_old" );
    }
  }
  nodes = dd.elementsByTagName( "tense" );
  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_gr" );
    }
    else {
      el.setAttribute( "class", "xdxf_gr_old" );
    }
  }

  nodes = dd.elementsByTagName( "tr" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    el.setTagName( "span" );
    if ( isLogicalFormat ) {
      el.setAttribute( "class", "xdxf_tr" );
      el.insertBefore( dd.createTextNode( QStringLiteral( "[" ) ), el.firstChild() );
      el.appendChild( dd.createTextNode( QStringLiteral( "]" ) ) );
    }
    else {
      el.setAttribute( "class", "xdxf_tr_old" );
    }
  }

  nodes = dd.elementsByTagName( "img" );

  for ( int i = 0; i < nodes.size(); i++ ) {
    QDomElement el = nodes.at( i ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    if ( el.hasAttribute( "src" ) ) {
      fixLink( el, dictPtr->getId(), "src" );
    }

    if ( el.hasAttribute( "losrc" ) ) {
      fixLink( el, dictPtr->getId(), "losrc" );
    }

    if ( el.hasAttribute( "hisrc" ) ) {
      fixLink( el, dictPtr->getId(), "hisrc" );
    }
  }

  nodes = dd.elementsByTagName( "rref" );

  while ( nodes.size() ) {
    QDomElement el = nodes.at( 0 ).toElement();

    if ( el.text().isEmpty() && el.childNodes().isEmpty() ) {
      el.appendChild( fakeElement( dd ) );
    }

    if ( dictPtr != nullptr && !el.hasAttribute( "start" ) ) {
      string filename = Text::toUtf8( el.text() );

      if ( Filetype::isNameOfPicture( filename ) ) {
        QUrl url;
        url.setScheme( "bres" );
        url.setHost( QString::fromUtf8( dictPtr->getId().c_str() ) );
        url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

        QDomElement newEl = dd.createElement( "img" );
        newEl.setAttribute( "src", url.toEncoded().data() );
        newEl.setAttribute( "alt", Html::escape( filename ).c_str() );

        QDomNode parent = el.parentNode();
        if ( !parent.isNull() ) {
          parent.replaceChild( newEl, el );
          continue;
        }
      }
      else if ( Filetype::isNameOfSound( filename ) ) {

        QDomElement el_script = dd.createElement( "script" );
        QDomNode parent       = el.parentNode();
        if ( !parent.isNull() ) {
          QUrl url;
          url.setScheme( "gdau" );
          url.setHost( QString::fromUtf8( dictPtr->getId().c_str() ) );
          url.setPath( Utils::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

          el_script.setAttribute( "type", "text/javascript" );
          parent.replaceChild( el_script, el );

          el_script.appendChild( dd.createTextNode( "" ) );

          addAudioLink( string( "\"" ) + url.toEncoded().data() + "\"", dictPtr->getId() );

          QDomElement el_span = dd.createElement( "span" );
          el_span.setAttribute( "class", "xdxf_wav" );
          parent.insertAfter( el_span, el_script );

          QDomElement el_a = dd.createElement( "a" );
          el_a.setAttribute( "href", url.toEncoded().data() );
          el_span.appendChild( el_a );

          QDomElement el_img = dd.createElement( "img" );
          el_img.setAttribute( "src", "qrc:///icons/playsound.svg" );
          el_img.setAttribute( "border", "0" );
          el_img.setAttribute( "align", "absmiddle" );
          el_img.setAttribute( "alt", "Play" );
          el_a.appendChild( el_img );

          continue;
        }
      }
    }

    el.setTagName( "span" );
    el.setAttribute( "class", "xdxf_rref" );
  }

  return dd.toString().remove( RX::Html::emptyXmlTag ).toUtf8().data();
}

}
