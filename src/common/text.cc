/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "text.hh"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <QByteArray>
#include <QList>

namespace Text {

const char * getEncodingNameFor( Encoding e )
{
  switch ( e ) {
    case Encoding::Utf32LE:
      return utf32_le;
    case Encoding::Utf32BE:
      return utf32_be;
    case Encoding::Utf32:
      return utf32;
    case Encoding::Utf16LE:
      return utf16_le;
    case Encoding::Utf16BE:
      return utf16_be;
    case Encoding::Windows1252:
      return windows_1252;
    case Encoding::Windows1251:
      return windows_1251;
    case Encoding::Windows1250:
      return windows_1250;
    case Encoding::Utf8:
    default:
      return utf8;
  }
}

Encoding getEncodingForName( const QByteArray & name )
{
  const auto n = name.toUpper();
  if ( n == utf32_le ) {
    return Encoding::Utf32LE;
  }
  if ( n == utf32_be ) {
    return Encoding::Utf32BE;
  }
  if ( n == utf32 ) {
    return Encoding::Utf32;
  }
  if ( n == utf16_le ) {
    return Encoding::Utf16LE;
  }
  if ( n == utf16_be ) {
    return Encoding::Utf16BE;
  }
  if ( n == windows_1252 ) {
    return Encoding::Windows1252;
  }
  if ( n == windows_1251 ) {
    return Encoding::Windows1251;
  }
  if ( n == windows_1250 ) {
    return Encoding::Windows1250;
  }
  return Encoding::Utf8;
}

long decode( const char * in_, size_t inSize, uint32_t * out_ )
{
  const unsigned char * in = (const unsigned char *)in_;
  uint32_t * out           = out_;

  while ( inSize-- ) {
    char32_t result;

    if ( *in & 0x80 ) {
      if ( *in & 0x40 ) {
        if ( *in & 0x20 ) {
          if ( *in & 0x10 ) {
            if ( *in & 8 ) {
              return -1;
            }

            if ( inSize < 3 ) {
              return -1;
            }

            inSize -= 3;

            result = ( (char32_t)*in++ & 7 ) << 18;

            if ( ( *in & 0xC0 ) != 0x80 ) {
              return -1;
            }
            result |= ( (char32_t)*in++ & 0x3F ) << 12;

            if ( ( *in & 0xC0 ) != 0x80 ) {
              return -1;
            }
            result |= ( (char32_t)*in++ & 0x3F ) << 6;

            if ( ( *in & 0xC0 ) != 0x80 ) {
              return -1;
            }
            result |= (char32_t)*in++ & 0x3F;
          }
          else {

            if ( inSize < 2 ) {
              return -1;
            }

            inSize -= 2;

            result = ( (char32_t)*in++ & 0xF ) << 12;

            if ( ( *in & 0xC0 ) != 0x80 ) {
              return -1;
            }
            result |= ( (char32_t)*in++ & 0x3F ) << 6;

            if ( ( *in & 0xC0 ) != 0x80 ) {
              return -1;
            }
            result |= (char32_t)*in++ & 0x3F;
          }
        }
        else {
          if ( !inSize ) {
            return -1;
          }

          --inSize;

          result = ( (char32_t)*in++ & 0x1F ) << 6;

          if ( ( *in & 0xC0 ) != 0x80 ) {
            return -1;
          }
          result |= (char32_t)*in++ & 0x3F;
        }
      }
      else {
        return -1;
      }
    }
    else {
      result = *in++;
    }

    *out++ = result;
  }

  return out - out_;
}

std::string toUtf8( const QString & in ) noexcept
{
  return in.toUtf8().toStdString();
}

QString toQString( const std::string & in )
{
  if ( in.empty() ) {
    return {};
  }

  std::vector< uint32_t > buffer( in.size() );

  const long result = decode( in.data(), in.size(), &buffer.front() );

  if ( result < 0 ) {
    throw exCantDecode( in );
  }

  return QString::fromUcs4( buffer.data(), result );
}

bool isspace( int c )
{
  switch ( c ) {
    case ' ':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
    case '\v':
      return true;

    default:
      return false;
  }
}

int findFirstLinePosition( char * s1, int s1length, const char * s2, int s2length )
{
  char * pos = std::search( s1, s1 + s1length, s2, s2 + s2length );

  if ( pos == s1 + s1length ) {
    return pos - s1;
  }

  return pos - s1 + s2length;
}


LineFeed initLineFeed( const Encoding e )
{
  LineFeed lf{};
  switch ( e ) {
    case Encoding::Utf32LE:
      lf.lineFeed = new char[ 4 ]{ 0x0A, 0, 0, 0 };
      lf.length   = 4;
      break;
    case Encoding::Utf32BE:
      lf.lineFeed = new char[ 4 ]{ 0, 0, 0, 0x0A };
      lf.length   = 4;
      break;
    case Encoding::Utf16LE:
      lf.lineFeed = new char[ 2 ]{ 0x0A, 0 };
      lf.length   = 2;
      break;
    case Encoding::Utf16BE:
      lf.lineFeed = new char[ 2 ]{ 0, 0x0A };
      lf.length   = 2;
      break;
    case Encoding::Windows1252:
    case Encoding::Windows1251:
    case Encoding::Windows1250:
    case Encoding::Utf8:
    default:
      lf.length   = 1;
      lf.lineFeed = new char[ 1 ]{ 0x0A };
  }
  return lf;
}

QString removeTrailingZero( const QString & v )
{
  int n = v.size();
  while ( n > 0 && v[ n - 1 ].isNull() ) {
    n--;
  }
  return n == v.size() ? v : v.left( n );
}

QString normalize( const QString & str )
{
  return str.normalized( QString::NormalizationForm_C );
}


std::string detectEncodingFromBom( const char * data, size_t size )
{
  if ( !data || size == 0 ) {
    return {};
  }

  auto bytes = reinterpret_cast< const unsigned char * >( data );

  if ( size >= 4 ) {
    if ( bytes[ 0 ] == 0xFF && bytes[ 1 ] == 0xFE && bytes[ 2 ] == 0x00 && bytes[ 3 ] == 0x00 ) {
      return utf32_le;
    }
    if ( bytes[ 0 ] == 0x00 && bytes[ 1 ] == 0x00 && bytes[ 2 ] == 0xFE && bytes[ 3 ] == 0xFF ) {
      return utf32_be;
    }
  }

  if ( size >= 3 ) {
    if ( bytes[ 0 ] == 0xEF && bytes[ 1 ] == 0xBB && bytes[ 2 ] == 0xBF ) {
      return utf8;
    }
  }

  if ( size >= 2 ) {
    if ( bytes[ 0 ] == 0xFF && bytes[ 1 ] == 0xFE ) {
      return utf16_le;
    }
    if ( bytes[ 0 ] == 0xFE && bytes[ 1 ] == 0xFF ) {
      return utf16_be;
    }
  }

  return {};
}

bool isValidUtf8( const char * data, size_t size )
{
  auto bytes = reinterpret_cast< const unsigned char * >( data );
  size_t i   = 0;

  while ( i < size ) {
    unsigned char byte = bytes[ i ];

    if ( byte <= 0x7F ) {
      i++;
      continue;
    }

    int extraBytes = 0;

    if ( ( byte & 0xE0 ) == 0xC0 ) {
      extraBytes = 1;
    }
    else if ( ( byte & 0xF0 ) == 0xE0 ) {
      extraBytes = 2;
    }
    else if ( ( byte & 0xF8 ) == 0xF0 ) {
      extraBytes = 3;
    }
    else {
      return false;
    }

    if ( i + extraBytes >= size ) {
      return false;
    }

    for ( int j = 1; j <= extraBytes; j++ ) {
      if ( ( bytes[ i + j ] & 0xC0 ) != 0x80 ) {
        return false;
      }
    }

    i += extraBytes + 1;
  }

  return true;
}

}
