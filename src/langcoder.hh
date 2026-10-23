#pragma once

#include <QIcon>
#include "text.hh"

struct GDLangCode
{
  QString code2;
  std::string code3;
  int isRTL;
  std::string lang;
};

class LangCoder
{
public:

  static quint32 code2toInt( const char code[ 2 ] )
  {
    return ( ( (quint32)code[ 1 ] ) << 8 ) + (quint32)code[ 0 ];
  }

  static quint32 code2toInt( QString code )
  {
    if ( code.size() < 2 )
      return 0;
    auto c = code.toLatin1();

    return ( ( (quint32)c[ 1 ] ) << 8 ) + (quint32)c[ 0 ];
  }

  static QString intToCode2( quint32 );

  static quint32 findIdForLanguage( const QString & );

  static quint32 findIdForLanguageCode3( const std::string & );

  static std::pair< quint32, quint32 > findLangIdPairFromName( const QString & );
  static std::pair< quint32, quint32 > findLangIdPairFromPath( const std::string & );

  static quint32 guessId( const QString & lang );

  static QString decode( quint32 _code );

  static bool isLanguageRTL( quint32 code );

private:
  static QMap< QString, GDLangCode > LANG_CODE_MAP;
  static bool code2Exists( const QString & _code );
};

#define LangCodeRole Qt::UserRole
