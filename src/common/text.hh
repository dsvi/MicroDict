/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */
#pragma once

#include "ex.hh"
#include <QByteArray>
#include <QString>
#include <string>

namespace Text {
DEF_EX_STR( exCantDecode, "Can't decode the given string from Utf8:", std::exception )

enum class Encoding {
  Utf16LE = 0,
  Utf16BE,
  Windows1252,
  Windows1251,
  Windows1250,
  Utf8,
  Utf32BE,
  Utf32LE,
  Utf32,
};

inline constexpr auto utf16_be     = "UTF-16BE";
inline constexpr auto utf16_le     = "UTF-16LE";
inline constexpr auto utf32        = "UTF-32";
inline constexpr auto utf32_be     = "UTF-32BE";
inline constexpr auto utf32_le     = "UTF-32LE";
inline constexpr auto utf8         = "UTF-8";
inline constexpr auto windows_1250 = "WINDOWS-1250";
inline constexpr auto windows_1251 = "WINDOWS-1251";
inline constexpr auto windows_1252 = "WINDOWS-1252";

const char * getEncodingNameFor( Encoding e );
Encoding getEncodingForName( const QByteArray & name );

std::string toUtf8( const QString & ) noexcept;

QString toQString( const std::string & );

bool isspace( int c );

int findFirstLinePosition( char * s1, int s1length, const char * s2, int s2length );

struct LineFeed
{
  int length;
  char * lineFeed;
};

LineFeed initLineFeed( Encoding e );

QString removeTrailingZero( const QString & v );

QString normalize( const QString & );

std::string detectEncodingFromBom( const char * data, size_t size );

bool isValidUtf8( const char * data, size_t size );
}
