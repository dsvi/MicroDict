#include <QRegularExpression>
#include "wildcard.hh"

QString wildcardsToRegexp( const QString & wc_str )
{
  return QRegularExpression::wildcardToRegularExpression( wc_str, QRegularExpression::UnanchoredWildcardConversion );
}
