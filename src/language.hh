/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QString>
namespace Language {

using Id = quint32;

enum {
  Unknown = 0,
  Any = 0xFFffFFff
};

QString englishNameForId( Id );

QString localizedNameForId( Id );

QString countryCodeForId( Id );

QString localizedStringForId( Id );

QMap< QString, QString > translationLangMap();
QString translationNameFromLangCode( const QString & locale );

struct BabylonLang
{
  Id id;
  const QString contryCode;
  const QString englishName;
  const char * localizedName;
};
BabylonLang getBabylonLangByIndex( int index );
quint32 findBlgLangIDByEnglishName( const QString & lang );
}
