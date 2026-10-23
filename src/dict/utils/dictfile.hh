/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "ex.hh"

#include <QFileInfo>
#include <cstdio>
#include <string>
#include <vector>
#include <QMutex>
#include <type_traits>

namespace File {

DEF_EX( Ex, "File exception", std::exception )
DEF_EX_STR( exCantOpen, "Can't open", Ex )
DEF_EX( exReadError, "Error reading from the file", Ex )
DEF_EX( exWriteError, "Error writing to the file", Ex )
DEF_EX( exSeekError, "File seek error", Ex )
DEF_EX( exAllocation, "Memory allocation error", Ex )

bool tryPossibleName( const std::string & name, std::string & copyTo );

bool tryPossibleZipName( const std::string & name, std::string & copyTo );

void loadFromFile( const std::string & filename, std::vector< char > & data );

class Index
{
  QFile f;

public:
  QMutex lock;

  Index( std::string_view filename, QIODevice::OpenMode mode );

  void read( void * buf, qint64 size );
  void write( const void * buf, qint64 size );

  template< typename T >
  T read()
  {
    T value;
    readType( value );
    return value;
  }

  template< typename T >
  void write( const T & value )
  {
    write( &value, sizeof( value ) );
  }

  size_t readRecords( void * buf, qint64 size, qint64 count );

  size_t writeRecords( const void * buf, qint64 size, qint64 count );

  char * gets( char * s, int size, bool stripNl = false );

  std::string gets( bool stripNl = true );

  template< typename T >
  void readU32SizeAndData( T & container )
  {
    static_assert( std::is_same< T, std::vector< unsigned char > >::value
                     || std::is_same< T, std::vector< char > >::value || std::is_same< T, std::string >::value,
                   "T must be either std::vector<char> or std::string" );
    uint32_t size = 0;
    read( &size, sizeof( uint32_t ) );
    if ( size > 0 ) {
      container.resize( size );
      read( container.data(), size );
    }
  };

  QByteArray readall();

  void seek( qint64 offset );

  void seekEnd();

  void rewind();

  qint64 tell();

  qint64 size();

  bool eof() const;

  uchar * map( qint64 offset, qint64 size );
  bool unmap( uchar * address );

  QFile & file();

  void close();

  ~Index() noexcept;

private:

  template< typename T >
  void readType( T & value )
  {
    read( &value, sizeof( value ) );
  }
};

}
