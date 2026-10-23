/* Made up from data.c and other supplementary files of dictd-1.11.0 for the
 * this program.
 */

/* data.c --
 * Created: Tue Jul 16 12:45:41 1996 by faith@dict.org
 * Revised: Sat Mar 30 10:46:06 2002 by faith@dict.org
 * Copyright 1996, 1997, 1998, 2000, 2002 Rickard E. Faith (faith@dict.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 1, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Suite 500, Boston, MA 02110, USA.
 */

#include <stdlib.h>
#include <time.h>
#include "dictzip.hh"
#include <limits.h>
#include <errno.h>
#include <string.h>

#define BUFFERSIZE 10240

#define OUT_BUFFER_SIZE 0xffffL

#define IN_BUFFER_SIZE ( (unsigned long)( (double)( OUT_BUFFER_SIZE - 12 ) * 0.89 ) )

#define GZ_MAGIC1 0x1f
#define GZ_MAGIC2 0x8b

#define GZ_FTEXT   0x01
#define GZ_FHCRC   0x02
#define GZ_FEXTRA  0x04
#define GZ_FNAME   0x08
#define GZ_COMMENT 0x10
#define GZ_MAX     2
#define GZ_FAST    4

#define GZ_OS_FAT     0
#define GZ_OS_AMIGA   1
#define GZ_OS_VMS     2
#define GZ_OS_UNIX    3
#define GZ_OS_VMCMS   4
#define GZ_OS_ATARI   5
#define GZ_OS_HPFS    6
#define GZ_OS_MAC     7
#define GZ_OS_Z       8
#define GZ_OS_CPM     9
#define GZ_OS_TOPS20  10
#define GZ_OS_NTFS    11
#define GZ_OS_QDOS    12
#define GZ_OS_ACORN   13
#define GZ_OS_UNKNOWN 255

#define GZ_RND_S1 'R'
#define GZ_RND_S2 'A'

#define GZ_ID1          0
#define GZ_ID2          1
#define GZ_CM           2
#define GZ_FLG          3
#define GZ_MTIME        4
#define GZ_XFL          8
#define GZ_OS           9
#define GZ_XLEN         10
#define GZ_FEXTRA_START 12
#define GZ_SI1          12
#define GZ_SI2          13
#define GZ_SUBLEN       14
#define GZ_VERSION      16
#define GZ_CHUNKLEN     18
#define GZ_CHUNKCNT     20
#define GZ_RNDDATA      22

#define DBG_VERBOSE  ( 0 << 30 | 1 << 0 )
#define DBG_ZIP      ( 0 << 30 | 1 << 1 )
#define DBG_UNZIP    ( 0 << 30 | 1 << 2 )
#define DBG_SEARCH   ( 0 << 30 | 1 << 3 )
#define DBG_SCAN     ( 0 << 30 | 1 << 4 )
#define DBG_PARSE    ( 0 << 30 | 1 << 5 )
#define DBG_INIT     ( 0 << 30 | 1 << 6 )
#define DBG_PORT     ( 0 << 30 | 1 << 7 )
#define DBG_LEV      ( 0 << 30 | 1 << 8 )
#define DBG_AUTH     ( 0 << 30 | 1 << 9 )
#define DBG_NODETACH ( 0 << 30 | 1 << 10 )
#define DBG_NOFORK   ( 0 << 30 | 1 << 11 )
#define DBG_ALT      ( 0 << 30 | 1 << 12 )

#define LOG_SERVER    ( 0 << 30 | 1 << 0 )
#define LOG_CONNECT   ( 0 << 30 | 1 << 1 )
#define LOG_STATS     ( 0 << 30 | 1 << 2 )
#define LOG_COMMAND   ( 0 << 30 | 1 << 3 )
#define LOG_FOUND     ( 0 << 30 | 1 << 4 )
#define LOG_NOTFOUND  ( 0 << 30 | 1 << 5 )
#define LOG_CLIENT    ( 0 << 30 | 1 << 6 )
#define LOG_HOST      ( 0 << 30 | 1 << 7 )
#define LOG_TIMESTAMP ( 0 << 30 | 1 << 8 )
#define LOG_MIN       ( 0 << 30 | 1 << 9 )
#define LOG_AUTH      ( 0 << 30 | 1 << 10 )

#define DICT_LOG_TERM    0
#define DICT_LOG_DEFINE  1
#define DICT_LOG_MATCH   2
#define DICT_LOG_NOMATCH 3
#define DICT_LOG_CLIENT  4
#define DICT_LOG_TRACE   5
#define DICT_LOG_COMMAND 6
#define DICT_LOG_AUTH    7
#define DICT_LOG_CONNECT 8

#define DICT_UNKNOWN 0
#define DICT_TEXT    1
#define DICT_GZIP    2
#define DICT_DZIP    3

#include <ctype.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/stat.h>

#define USE_CACHE 1

#define dict_data_filter( ... )
#define PRINTF( ... )

#define xmalloc malloc
#define xfree   free

static const char * _err_programName = "MicroDict";

#define log_error( ... )
#define log_error_va( ... )

static void err_fatal( const char * routine, const char * format, ... )
{
  va_list ap;

  fflush( stdout );
  if ( _err_programName ) {
    if ( routine ) {
      fprintf( stderr, "%s (%s): ", _err_programName, routine );
    }
    else {
      fprintf( stderr, "%s: ", _err_programName );
    }
  }
  else {
    if ( routine ) {
      fprintf( stderr, "%s: ", routine );
    }
  }

  va_start( ap, format );
  vfprintf( stderr, format, ap );
  log_error_va( routine, format, ap );
  va_end( ap );

  fflush( stderr );
  fflush( stdout );
}

static void err_fatal_errno( const char * routine, const char * format, ... )
{
  va_list ap;
  int errorno = errno;

  fflush( stdout );
  if ( _err_programName ) {
    if ( routine ) {
      fprintf( stderr, "%s (%s): ", _err_programName, routine );
    }
    else {
      fprintf( stderr, "%s: ", _err_programName );
    }
  }
  else {
    if ( routine ) {
      fprintf( stderr, "%s: ", routine );
    }
  }

  va_start( ap, format );
  vfprintf( stderr, format, ap );
  log_error_va( routine, format, ap );
  va_end( ap );

#if HAVE_STRERROR
  fprintf( stderr, "%s: %s\n", routine, strerror( errorno ) );
  log_error( routine, "%s: %s\n", routine, strerror( errorno ) );
#else
  errno = errorno;
  perror( routine );
  log_error( routine, "%s: errno = %d\n", routine, errorno );
#endif

  fflush( stderr );
  fflush( stdout );
}

static void err_internal( const char * routine, const char * format, ... )
{
  va_list ap;

  fflush( stdout );
  if ( _err_programName ) {
    if ( routine ) {
      fprintf( stderr, "%s (%s): Internal error\n     ", _err_programName, routine );
    }
    else {
      fprintf( stderr, "%s: Internal error\n     ", _err_programName );
    }
  }
  else {
    if ( routine ) {
      fprintf( stderr, "%s: Internal error\n     ", routine );
    }
    else {
      fprintf( stderr, "Internal error\n     " );
    }
  }

  va_start( ap, format );
  vfprintf( stderr, format, ap );
  log_error( routine, format, ap );
  va_end( ap );

  if ( _err_programName ) {
    fprintf( stderr, "Aborting %s...\n", _err_programName );
  }
  else {
    fprintf( stderr, "Aborting...\n" );
  }
  fflush( stderr );
  fflush( stdout );
}

#ifndef __func__
  #ifdef __FUNCTION__
    #define __func__ __FUNCTION__
  #else
    #define __func__ __FILE__
  #endif
#endif

static enum DZ_ERRORS dict_read_header( const char * filename, dictData * header, int computeCRC )
{
  FILE * str;
  int id1, id2, si1, si2;
  char buffer[ BUFFERSIZE ];
  int extraLength, subLength;
  int i;
  char * pt;
  int c;
  struct stat sb;
  unsigned long crc = crc32( 0L, Z_NULL, 0 );
  int count;
  unsigned long offset;

  if ( !( str = fopen( filename, "rb" ) ) ) {
    err_fatal_errno( __func__, "Cannot open data file \"%s\" for read\n", filename );
    return DZ_ERR_OPENFILE;
  }

  header->filename     = NULL;
  header->headerLength = GZ_XLEN - 1;
  header->type         = DICT_UNKNOWN;

  id1 = getc( str );
  id2 = getc( str );

  if ( id1 != GZ_MAGIC1 || id2 != GZ_MAGIC2 ) {
    header->type = DICT_TEXT;
    fstat( fileno( str ), &sb );
    header->compressedLength = header->length = sb.st_size;
    header->origFilename                      = NULL;
    header->mtime                             = sb.st_mtime;
    if ( computeCRC ) {
      rewind( str );
      while ( !feof( str ) ) {
        if ( ( count = fread( buffer, 1, BUFFERSIZE, str ) ) ) {
          crc = crc32( crc, (Bytef *)buffer, count );
        }
      }
    }
    header->crc = crc;
    fclose( str );
    return DZ_NOERROR;
  }
  header->type = DICT_GZIP;

  header->method = getc( str );
  header->flags  = getc( str );
  header->mtime  = getc( str ) << 0;
  header->mtime |= getc( str ) << 8;
  header->mtime |= getc( str ) << 16;
  header->mtime |= getc( str ) << 24;
  header->extraFlags = getc( str );
  header->os         = getc( str );

  if ( header->flags & GZ_FEXTRA ) {
    extraLength = getc( str ) << 0;
    extraLength |= getc( str ) << 8;
    header->headerLength += extraLength + 2;
    si1 = getc( str );
    si2 = getc( str );

    if ( si1 == GZ_RND_S1 && si2 == GZ_RND_S2 ) {
      subLength = getc( str ) << 0;
      subLength |= getc( str ) << 8;
      header->version = getc( str ) << 0;
      header->version |= getc( str ) << 8;

      if ( header->version != 1 ) {
        err_internal( __func__, "dzip header version %d not supported\n", header->version );
        fclose( str );
        return DZ_ERR_UNSUPPORTED_FORMAT;
      }

      header->chunkLength = getc( str ) << 0;
      header->chunkLength |= getc( str ) << 8;
      header->chunkCount = getc( str ) << 0;
      header->chunkCount |= getc( str ) << 8;

      if ( header->chunkCount <= 0 ) {
        fclose( str );
        return DZ_ERR_INVALID_FORMAT;
      }
      header->chunks = xmalloc( sizeof( header->chunks[ 0 ] ) * header->chunkCount );
      if ( header->chunks == 0 ) {
        fclose( str );
        return DZ_ERR_NOMEMORY;
      }

      for ( i = 0; i < header->chunkCount; i++ ) {
        header->chunks[ i ] = getc( str ) << 0;
        header->chunks[ i ] |= getc( str ) << 8;
      }
      header->type = DICT_DZIP;
    }
    else {
      fseek( str, header->headerLength, SEEK_SET );
    }
  }

  if ( header->flags & GZ_FNAME ) {
    pt = buffer;
    while ( ( c = getc( str ) ) && c != EOF ) {
      *pt++ = c;

      if ( pt == buffer + sizeof( buffer ) ) {
        err_fatal( __func__, "too long FNAME field in dzip file \"%s\"\n", filename );
        fclose( str );
        if ( header->chunks ) {
          free( header->chunks );
          header->chunks = NULL;
        }
        return DZ_ERR_INVALID_FORMAT;
      }
    }

    *pt                  = '\0';
    header->origFilename = NULL;
    header->headerLength += strlen( buffer ) + 1;
  }
  else {
    header->origFilename = NULL;
  }

  if ( header->flags & GZ_COMMENT ) {
    pt = buffer;
    while ( ( c = getc( str ) ) && c != EOF ) {
      *pt++ = c;

      if ( pt == buffer + sizeof( buffer ) ) {
        err_fatal( __func__, "too long COMMENT field in dzip file \"%s\"\n", filename );
        fclose( str );
        if ( header->chunks ) {
          free( header->chunks );
          header->chunks = NULL;
        }
        return DZ_ERR_INVALID_FORMAT;
      }
    }

    *pt             = '\0';
    header->comment = NULL;
    header->headerLength += strlen( buffer ) + 1;
  }
  else {
    header->comment = NULL;
  }

  if ( header->flags & GZ_FHCRC ) {
    getc( str );
    getc( str );
    header->headerLength += 2;
  }

  if ( ftell( str ) != header->headerLength + 1 ) {
    err_internal( __func__, "File position (%lu) != header length + 1 (%d)\n", ftell( str ), header->headerLength + 1 );
    fclose( str );
    if ( header->chunks ) {
      free( header->chunks );
      header->chunks = NULL;
    }
    return DZ_ERR_INVALID_FORMAT;
  }

  fseek( str, -8, SEEK_END );
  header->crc = getc( str ) << 0;
  header->crc |= getc( str ) << 8;
  header->crc |= getc( str ) << 16;
  header->crc |= getc( str ) << 24;
  header->length = getc( str ) << 0;
  header->length |= getc( str ) << 8;
  header->length |= getc( str ) << 16;
  header->length |= getc( str ) << 24;
  header->compressedLength = ftell( str );

  header->offsets = xmalloc( sizeof( header->offsets[ 0 ] ) * header->chunkCount );
  if ( header->offsets == 0 ) {
    if ( header->chunks ) {
      free( header->chunks );
      header->chunks = NULL;
    }
    fclose( str );
    return DZ_ERR_NOMEMORY;
  }

  for ( offset = header->headerLength + 1, i = 0; i < header->chunkCount; i++ ) {
    header->offsets[ i ] = offset;
    offset += header->chunks[ i ];
  }

  fclose( str );
  return DZ_NOERROR;
}

dictData * dict_data_open( const char * filename, enum DZ_ERRORS * error, int computeCRC )
{
  dictData * h = NULL;
  int j;

  if ( !filename ) {
    *error = DZ_ERR_OPENFILE;
    return NULL;
  }

  h = xmalloc( sizeof( struct dictData ) );
  if ( h == 0 ) {
    *error = DZ_ERR_NOMEMORY;
    return 0;
  }

  memset( h, 0, sizeof( struct dictData ) );
  h->initialized = 0;

  for ( ;; ) {
    *error = dict_read_header( filename, h, computeCRC );
    if ( *error != DZ_NOERROR ) {
      break;
    }

    h->fd = fopen( filename, "rb" );

    if ( !h->fd ) {
      *error = DZ_ERR_OPENFILE;
      break;
    }

    fseek( h->fd, 0, SEEK_END );

    h->size = ftell( h->fd );

    for ( j = 0; j < DICT_CACHE_SIZE; j++ ) {
      h->cache[ j ].chunk    = -1;
      h->cache[ j ].stamp    = -1;
      h->cache[ j ].inBuffer = NULL;
      h->cache[ j ].count    = 0;
    }

    *error = DZ_NOERROR;
    return h;
  }
  dict_data_close( h );
  return ( 0 );
}

void dict_data_close( dictData * header )
{
  int i;

  if ( !header ) {
    return;
  }

  if ( header->fd ) {
    fclose( header->fd );
  }

  if ( header->chunks ) {
    xfree( header->chunks );
  }
  if ( header->offsets ) {
    xfree( header->offsets );
  }

  if ( header->initialized ) {
    if ( inflateEnd( &header->zStream ) ) {
      err_internal( __func__, "Cannot shut down inflation engine: %s\n", header->zStream.msg );
    }
  }

  for ( i = 0; i < DICT_CACHE_SIZE; ++i ) {
    if ( header->cache[ i ].inBuffer ) {
      xfree( header->cache[ i ].inBuffer );
    }
  }

  xfree( header );
}

char * dict_data_read_(
  dictData * h, unsigned long start, unsigned long size, const char * preFilter, const char * postFilter )
{
  char * buffer;
  char * pt;
  unsigned long end;
  int count;
  char * inBuffer;
  char outBuffer[ OUT_BUFFER_SIZE ];
  int firstChunk, lastChunk;
  int firstOffset, lastOffset;
  int i, j;
  int found, target, lastStamp;
  (void)preFilter;
  (void)postFilter;

  end = start + size;

  buffer = xmalloc( size + 1 );
  if ( !buffer ) {
    strcpy( h->errorString, dz_error_str( DZ_ERR_NOMEMORY ) );
    return 0;
  }

  if ( !size ) {
    *buffer = 0;
    return buffer;
  }

  PRINTF( DBG_UNZIP, ( "dict_data_read( %p, %lu, %lu, %s, %s )\n", h, start, size, preFilter, postFilter ) );

  assert( h != NULL );
  switch ( h->type ) {
    case DICT_GZIP:
      strcpy( h->errorString, "Cannot seek on pure gzip format files" );
      xfree( buffer );
      return 0;
    case DICT_TEXT: {
      if ( fseek( h->fd, start, SEEK_SET ) != 0 || fread( buffer, size, 1, h->fd ) != 1 )
      {
        strcpy( h->errorString, dz_error_str( DZ_ERR_READFILE ) );
        xfree( buffer );
        return 0;
      }

      buffer[ size ] = '\0';
    } break;
    case DICT_DZIP:
      if ( !h->initialized ) {
        h->zStream.zalloc    = NULL;
        h->zStream.zfree     = NULL;
        h->zStream.opaque    = NULL;
        h->zStream.next_in   = 0;
        h->zStream.avail_in  = 0;
        h->zStream.next_out  = NULL;
        h->zStream.avail_out = 0;
        if ( inflateInit2( &h->zStream, -15 ) != Z_OK )
        {
          sprintf( h->errorString, "Cannot initialize inflation engine: %s", h->zStream.msg );
          xfree( buffer );
          return 0;
        }
        ++h->initialized;
      }
      firstChunk  = start / h->chunkLength;
      firstOffset = start % h->chunkLength;
      lastChunk   = end / h->chunkLength;
      lastOffset  = end % h->chunkLength;
      PRINTF( DBG_UNZIP,
              ( "   start = %lu, end = %lu\n"
                "firstChunk = %d, firstOffset = %d,"
                " lastChunk = %d, lastOffset = %d\n",
                start,
                end,
                firstChunk,
                firstOffset,
                lastChunk,
                lastOffset ) );
      for ( pt = buffer, i = firstChunk; i <= lastChunk; i++ ) {

        found     = 0;
        target    = 0;
        lastStamp = INT_MAX;
        for ( j = 0; j < DICT_CACHE_SIZE; j++ ) {
#if USE_CACHE
          if ( h->cache[ j ].chunk == i ) {
            found  = 1;
            target = j;
            break;
          }
#endif
          if ( h->cache[ j ].stamp < lastStamp ) {
            lastStamp = h->cache[ j ].stamp;
            target    = j;
          }
        }

        h->cache[ target ].stamp = ++h->stamp;
        if ( h->stamp < 0 ) {
          h->stamp = 0;
          for ( j = 0; j < DICT_CACHE_SIZE; j++ ) {
            h->cache[ j ].stamp = -1;
          }
        }
        if ( found ) {
          count    = h->cache[ target ].count;
          inBuffer = h->cache[ target ].inBuffer;
        }
        else {
          h->cache[ target ].chunk = -1;
          if ( !h->cache[ target ].inBuffer ) {
            h->cache[ target ].inBuffer = xmalloc( h->chunkLength );
          }
          inBuffer = h->cache[ target ].inBuffer;
          if ( !inBuffer ) {
            strcpy( h->errorString, dz_error_str( DZ_ERR_NOMEMORY ) );
            xfree( buffer );
            return 0;
          }

          if ( h->chunks[ i ] >= OUT_BUFFER_SIZE ) {
            sprintf( h->errorString,
                     "h->chunks[%d] = %d >= %ld (OUT_BUFFER_SIZE)\n",
                     i,
                     h->chunks[ i ],
                     OUT_BUFFER_SIZE );
            xfree( buffer );
            return 0;
          }

          if ( fseek( h->fd, h->offsets[ i ], SEEK_SET ) != 0 || fread( outBuffer, h->chunks[ i ], 1, h->fd ) != 1 )
          {
            strcpy( h->errorString, dz_error_str( DZ_ERR_READFILE ) );
            xfree( buffer );
            return 0;
          }

          dict_data_filter( outBuffer, &count, OUT_BUFFER_SIZE, preFilter );

          h->zStream.next_in   = (Bytef *)outBuffer;
          h->zStream.avail_in  = h->chunks[ i ];
          h->zStream.next_out  = (Bytef *)inBuffer;
          h->zStream.avail_out = h->chunkLength;
          if ( inflate( &h->zStream, Z_PARTIAL_FLUSH ) != Z_OK ) {
            sprintf( h->errorString, "inflate: %s\n", h->zStream.msg );
            xfree( buffer );
            return 0;
          }
          if ( h->zStream.avail_in )
          {
            sprintf( h->errorString,
                     "inflate did not flush (%d pending, %d avail)\n",
                     h->zStream.avail_in,
                     h->zStream.avail_out );
            xfree( buffer );
            return 0;
          }

          count = h->chunkLength - h->zStream.avail_out;
          dict_data_filter( inBuffer, &count, h->chunkLength, postFilter );

          h->cache[ target ].count = count;
          h->cache[ target ].chunk = i;
        }

        if ( i == firstChunk ) {
          if ( i == lastChunk ) {
            memcpy( pt, inBuffer + firstOffset, lastOffset - firstOffset );
            pt += lastOffset - firstOffset;
          }
          else {
            if ( count != h->chunkLength )
            {
              sprintf( h->errorString, "Length = %d instead of %d\n", count, h->chunkLength );
              xfree( buffer );
              return 0;
            }
            memcpy( pt, inBuffer + firstOffset, h->chunkLength - firstOffset );
            pt += h->chunkLength - firstOffset;
          }
        }
        else if ( i == lastChunk ) {
          memcpy( pt, inBuffer, lastOffset );
          pt += lastOffset;
        }
        else {
          assert( count == h->chunkLength );
          memcpy( pt, inBuffer, h->chunkLength );
          pt += h->chunkLength;
        }
      }
      *pt = '\0';
      break;
    case DICT_UNKNOWN:
      strcpy( h->errorString, "Cannot read unknown file type" );
      xfree( buffer );
      return 0;
  }
  h->errorString[ 0 ] = 0;
  return buffer;
}

char * dict_error_str( dictData * data )
{
  return data->errorString;
}

const char * dz_error_str( enum DZ_ERRORS error )
{
  switch ( error ) {
    case DZ_NOERROR:
      return "No error";
    case DZ_ERR_OPENFILE:
      return "Open file error";
    case DZ_ERR_READFILE:
      return "Read file error";
    case DZ_ERR_INVALID_FORMAT:
      return "Invalid file format";
    case DZ_ERR_UNSUPPORTED_FORMAT:
      return "Unsupported file format";
    case DZ_ERR_NOMEMORY:
      return "Memory allocation error";
    case DZ_ERR_INTERNAL:
      return "Internal error";
  }
  return "Unknown error";
}
