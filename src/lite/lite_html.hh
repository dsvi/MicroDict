/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QString>
#include <QStringList>

namespace LiteHtml {

struct Options {
  bool expandOptionalParts = false;
  bool expandCollapsedArticles = true;
  bool darkMode = false;
};

QString prepare( const QString & html, const Options & opt );

QString extractTitle( const QString & html );

QString stripScripts( const QString & html );

bool isHiddenByDefault( const QString & tagName, const QString & attributes );

}
