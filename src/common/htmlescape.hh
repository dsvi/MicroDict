/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QString>
#include <string>

namespace Html {
enum class HtmlOption {
  Strip,
  Keep
};
using std::string;

string escape( const string & );

string preformat( const string &, bool baseRightToLeft = false );

string escapeForJavaScript( const string & );
QString stripHtml( QString tmp );
QString unescape( QString str, HtmlOption option = HtmlOption::Strip );

QString fromHtmlEscaped( const QString & str );
string unescapeUtf8( const string & str, HtmlOption option = HtmlOption::Strip );

}
