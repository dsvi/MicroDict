/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "ex.hh"
#include "text.hh"
#include <QStringList>
#include <iconv.h>

class Iconv
{
  iconv_t state;

public:

  DEF_EX( Ex, "Iconv exception", std::exception )
  DEF_EX_STR( exCantInit, "Can't initialize iconv conversion:", Ex )

  explicit Iconv( const char * from );

  ~Iconv();
  static QByteArray fromUnicode( const QString & input, const char * toEncoding );

  QString convert( const void *& inBuf, size_t & inBytesLeft );

  static std::string toUtf8( const char * fromEncoding, const void * fromData, size_t dataSize );

  static QString toQString( const char * fromEncoding, const void * fromData, size_t dataSize );
  static QString findValidEncoding( const QStringList & encodings );
  Iconv( const Iconv & ) = delete;
  Iconv & operator=( const Iconv & ) = delete;
  Iconv( Iconv && ) = delete;
  Iconv & operator=( Iconv && ) = delete;
};
