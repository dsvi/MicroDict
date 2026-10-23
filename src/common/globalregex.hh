#pragma once

#include <QRegularExpression>
#include <QStringList>

namespace RX {
class Ftx
{
public:
  static QRegularExpression regBrackets;
  static QRegularExpression regSplit;
  static QRegularExpression spacesRegExp;
  static QRegularExpression wordRegExp;
  static QRegularExpression setsRegExp;
  static QRegularExpression regexRegExp;
  static QRegularExpression handleRoundBracket;
  static QRegularExpression noRoundBracket;

  static QRegularExpression tokenBoundary;

  static QRegularExpression token;

  static QStringList processSearchStringForHighlight( const QString & searchString );
  static QString serializeKeywordsToJson( const QStringList & keywords );
};


class Mdx
{
public:
  static QRegularExpression allLinksRe;
  static QRegularExpression wordCrossLink;
  static QRegularExpression anchorIdRe;
  static QRegularExpression anchorIdReWord;
  static QRegularExpression anchorIdRe2;
  static QRegularExpression anchorLinkRe;
  const static QRegularExpression audioRe;
  const static QRegularExpression stylesRe;
  static QRegularExpression stylesRe2;
  static QRegularExpression inlineScriptRe;
  static QRegularExpression closeScriptTagRe;
  static QRegularExpression srcRe;
  static QRegularExpression srcRe2;
  static QRegularExpression srcset;
  static QRegularExpression objectdata;

  static QRegularExpression links;
  static QRegularExpression fontFace;
  static QRegularExpression styleElement;

  static QRegularExpression protocolRelativeUrlQuoted;
  static QRegularExpression protocolRelativeUrlUnquoted;
  static QRegularExpression htmlTagStart;
  static QRegularExpression htmlTagEnd;
  static QRegularExpression bodyTagStart;
  static QRegularExpression bodyTagEnd;
  static QRegularExpression headTagStart;
  static QRegularExpression headTagEnd;
};

namespace Html {
const static QRegularExpression startDivTag( R"(<div[\s>])" );
const static QRegularExpression htmlEntity( R"(&(?:#\d+|#[xX][\da-fA-F]+|[0-9a-zA-Z]+);)" );

const static QRegularExpression emptyXmlTag( R"(<(?!(br|hr)\b)([^/ >]*)\s*/>)" );

bool containHtmlEntity( const std::string & text );
}

const static QRegularExpression accentMark( R"(\p{M})", QRegularExpression::UseUnicodePropertiesOption );
const static QRegularExpression accentPunc( R"(\p{M}\p{P})", QRegularExpression::UseUnicodePropertiesOption );
const static QRegularExpression markSpace( R"([\p{M}\p{Z}])", QRegularExpression::UseUnicodePropertiesOption );

}
