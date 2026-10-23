/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "ex.hh"
#include "dictfile.hh"

#include <vector>
#include <stdint.h>

namespace ChunkedStorage {

using std::vector;

DEF_EX( Ex, "Chunked storage exception", std::exception )
DEF_EX( exFailedToCompressChunk, "Failed to compress a chunk", Ex )
DEF_EX( exAddressOutOfRange, "The given chunked address is out of range", Ex )
DEF_EX( exFailedToDecompressChunk, "Failed to decompress a chunk", Ex )
DEF_EX( mapFailed, "Failed to map/unmap the file", Ex )

class Writer
{
  vector< uint32_t > offsets;
  File::Index & file;
  size_t scratchPadOffset, scratchPadSize;

public:
  explicit Writer( File::Index & );

  uint32_t startNewBlock();

  void addToBlock( const void * data, size_t size );

  uint32_t finish();

private:

  bool chunkStarted;

  vector< unsigned char > buffer;

  vector< unsigned char > bufferCompressed;

  size_t bufferUsed;

  void saveCurrentChunk();
};

class Reader
{
  vector< uint32_t > offsets;
  File::Index & file;

public:
  Reader( File::Index &, uint32_t );

  char * getBlock( uint32_t address, vector< char > & );
};

}
