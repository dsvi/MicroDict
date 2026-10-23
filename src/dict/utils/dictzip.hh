/* Made up from data.h and other supplementary files of dictd-1.0.11 for the
 * this program.
 */

/* data.h --
 * Created: Sat Mar 15 18:04:25 2003 by Aleksey Cheusov <vle@gmx.net>
 * Copyright 1994-2003 Rickard E. Faith (faith@dict.org)
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

#pragma once

#include <stdio.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DICT_CACHE_SIZE 5

typedef struct dictCache
{
  int chunk;
  char * inBuffer;
  int stamp;
  int count;
} dictCache;

enum DZ_ERRORS {
  DZ_NOERROR = 0,
  DZ_ERR_INTERNAL,
  DZ_ERR_OPENFILE,
  DZ_ERR_READFILE,
  DZ_ERR_UNSUPPORTED_FORMAT,
  DZ_ERR_INVALID_FORMAT,
  DZ_ERR_NOMEMORY
};

typedef struct dictData
{
  FILE * fd;

  unsigned long size;

  int type;
  const char * filename;
  z_stream zStream;
  int initialized;

  int headerLength;
  int method;
  int flags;
  time_t mtime;
  int extraFlags;
  int os;
  int version;
  int chunkLength;
  int chunkCount;
  int * chunks;
  unsigned long * offsets;
  const char * origFilename;
  const char * comment;
  unsigned long crc;
  unsigned long length;
  unsigned long compressedLength;
  int stamp;
  dictCache cache[ DICT_CACHE_SIZE ];
  char errorString[ 512 ];
} dictData;

extern dictData * dict_data_open( const char * filename, enum DZ_ERRORS * error, int computeCRC );
extern void dict_data_close( dictData * data );

extern char * dict_data_read_(
  dictData * data, unsigned long start, unsigned long end, const char * preFilter, const char * postFilter );

extern char * dict_error_str( dictData * data );

extern const char * dz_error_str( enum DZ_ERRORS error );

extern int mmap_mode;

#ifdef __cplusplus
}
#endif
