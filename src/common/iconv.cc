/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "iconv.hh"
#include <vector>
#include <errno.h>
#include <string.h>
#include <QDebug>

Iconv::Iconv( const char * from ):
  state( iconv_open( Text::utf8, from ) )
{
  if ( state == (iconv_t)-1 ) {
    throw exCantInit( strerror( errno ) );
  }
}

Iconv::~Iconv()
{
  iconv_close( state );
}

QByteArray Iconv::fromUnicode( const QString & input, const char * toEncoding )
{
  QByteArray utf8Data = input.toUtf8();
  const char * inBuf  = utf8Data.constData();
  size_t inBytesLeft  = utf8Data.size();

  iconv_t cd = iconv_open( toEncoding, "UTF-8" );
  if ( cd == (iconv_t)-1 ) {
    qDebug() << "iconv_open failed";
    return {};
  }

  size_t outBytesLeft = inBytesLeft * 4;
  std::vector< char > outBuf( outBytesLeft );
  char * outBufPtr = outBuf.data();

  while ( inBytesLeft > 0 ) {
    size_t result = iconv( cd, const_cast< char ** >( &inBuf ), &inBytesLeft, &outBufPtr, &outBytesLeft );
    if ( result == (size_t)-1 ) {
      if ( errno == E2BIG ) {
        size_t offset = outBufPtr - outBuf.data();
        outBuf.resize( outBuf.size() + inBytesLeft * 4 );
        outBufPtr = outBuf.data() + offset;
        outBytesLeft += inBytesLeft * 4;
      }
      else {
        iconv_close( cd );
        qDebug() << "iconv conversion failed";
        return {};
      }
    }
  }

  iconv_close( cd );

  outBuf.resize( outBuf.size() - outBytesLeft );
  return QByteArray( outBuf.data(), outBuf.size() );
}

QString Iconv::convert( const void *& inBuf, size_t & inBytesLeft )
{
  size_t dsz = inBytesLeft;
  std::vector< char > outBuf( dsz + 32 );

  void * outBufPtr = &outBuf.front();

  size_t outBufLeft = outBuf.size();
  size_t result     = (size_t)( -1 );
  while ( inBytesLeft > 0 ) {
    result = iconv( state, (char **)&inBuf, &inBytesLeft, (char **)&outBufPtr, &outBufLeft );
    if ( result == (size_t)-1 ) {
      if ( errno == E2BIG || outBufLeft == 0 ) {

        if ( inBytesLeft > 0 ) {
          size_t offset = (char *)outBufPtr - &outBuf.front();
          outBuf.resize( outBuf.size() + dsz );
          outBufPtr = &outBuf.front() + offset;
          outBufLeft += dsz;
          continue;
        }
      }
      break;
    }
  }

  if ( result != (size_t)( -1 ) ) {
    for ( ;; ) {
      result = iconv( state, nullptr, nullptr, (char **)&outBufPtr, &outBufLeft );

      if ( result != (size_t)( -1 ) ) {
        break;
      }

      if ( errno == E2BIG ) {
        size_t offset = (char *)outBufPtr - &outBuf.front();
        outBuf.resize( outBuf.size() + 256 );
        outBufPtr = &outBuf.front() + offset;
        outBufLeft += 256;
      }
      else {
        break;
      }
    }
  }

  size_t datasize = outBuf.size() - outBufLeft;
  return QString::fromUtf8( &outBuf.front(), datasize );
}

std::string Iconv::toUtf8( const char * fromEncoding, const void * fromData, size_t dataSize )

{

  if ( dataSize == 0 ) {
    return {};
  }

  Iconv ic( fromEncoding );

  const QString outStr = ic.convert( fromData, dataSize );
  return outStr.toStdString();
}

QString Iconv::toQString( const char * fromEncoding, const void * fromData, size_t dataSize )
{
  if ( dataSize == 0 ) {
    return {};
  }

  Iconv ic( fromEncoding );
  return ic.convert( fromData, dataSize );
}
QString Iconv::findValidEncoding( const QStringList & encodings )
{
  for ( const QString & encoding : encodings ) {
    iconv_t cd = iconv_open( "UTF-8", encoding.toUtf8().constData() );
    if ( cd != (iconv_t)-1 ) {
      iconv_close( cd );
      return encoding;
    }
  }
  return {};
}
