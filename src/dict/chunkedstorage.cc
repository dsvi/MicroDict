/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "chunkedstorage.hh"
#include <zlib.h>
#include <string.h>
#include <QDataStream>
#include <QScopeGuard>
#include <QMutexLocker>

namespace ChunkedStorage {

enum {
  ChunkMaxSize = 65536
};

Writer::Writer( File::Index & f ):
  file( f ),
  chunkStarted( false ),
  bufferUsed( 0 )
{

  char zero[ 4096 ];

  memset( zero, 0, sizeof( zero ) );

  scratchPadOffset = file.tell();
  scratchPadSize   = sizeof( zero );

  file.write( zero, sizeof( zero ) );
}

uint32_t Writer::startNewBlock()
{
  if ( bufferUsed >= ChunkMaxSize ) {
    saveCurrentChunk();
  }

  chunkStarted = true;

  return bufferUsed | ( (uint32_t)offsets.size() << 16 );
}

void Writer::addToBlock( const void * data, size_t size )
{
  if ( !size ) {
    return;
  }

  if ( buffer.size() - bufferUsed < size ) {
    buffer.resize( bufferUsed + size );
  }

  memcpy( &buffer.front() + bufferUsed, data, size );

  bufferUsed += size;

  chunkStarted = false;
}

void Writer::saveCurrentChunk()
{
  size_t maxCompressedSize = compressBound( bufferUsed );

  if ( bufferCompressed.size() < maxCompressedSize ) {
    bufferCompressed.resize( maxCompressedSize );
  }

  unsigned long compressedSize = bufferCompressed.size();

  if ( compress( &bufferCompressed.front(), &compressedSize, &buffer.front(), bufferUsed ) != Z_OK ) {
    throw exFailedToCompressChunk();
  }

  offsets.push_back( file.tell() );

  file.write( (uint32_t)bufferUsed );
  file.write( (uint32_t)compressedSize );
  file.write( &bufferCompressed.front(), compressedSize );

  bufferUsed = 0;

  chunkStarted = false;
}

uint32_t Writer::finish()
{
  if ( bufferUsed || chunkStarted ) {
    saveCurrentChunk();
  }

  bool useScratchPad   = false;
  uint32_t savedOffset = 0;

  if ( scratchPadSize >= offsets.size() * sizeof( uint32_t ) + sizeof( uint32_t ) ) {
    useScratchPad = true;
    savedOffset   = file.tell();
    file.seek( scratchPadOffset );
  }

  uint32_t offset = file.tell();

  file.write( (uint32_t)offsets.size() );

  if ( offsets.size() ) {
    file.write( &offsets.front(), offsets.size() * sizeof( uint32_t ) );
  }

  if ( useScratchPad ) {
    file.seek( savedOffset );
  }

  offsets.clear();
  chunkStarted = false;

  return offset;
}

Reader::Reader( File::Index & f, uint32_t offset ):
  file( f )
{
  file.seek( offset );

  uint32_t size = file.read< uint32_t >();
  if ( size == 0 ) {
    return;
  }
  offsets.resize( size );
  file.read( &offsets.front(), offsets.size() * sizeof( uint32_t ) );
}

char * Reader::getBlock( uint32_t address, vector< char > & chunk )
{
  size_t chunkIdx = address >> 16;

  if ( chunkIdx >= offsets.size() ) {
    throw exAddressOutOfRange();
  }

  {
    QMutexLocker _( &file.lock );
    auto bytes = file.map( offsets[ chunkIdx ], 8 );
    if ( bytes == nullptr ) {
      throw mapFailed();
    }
    auto qBytes = QByteArray::fromRawData( reinterpret_cast< char * >( bytes ), 8 );
    QDataStream in( qBytes );
    in.setByteOrder( QDataStream::LittleEndian );

    uint32_t uncompressedSize;
    uint32_t compressedSize;

    in >> uncompressedSize >> compressedSize;

    file.unmap( bytes );
    chunk.resize( uncompressedSize );

    auto chunkDataBytes = file.map( offsets[ chunkIdx ] + 8, compressedSize );
    if ( chunkDataBytes == nullptr ) {
      throw mapFailed();
    }
    [[maybe_unused]] auto autoUnmap = qScopeGuard( [ & ] {
      file.unmap( chunkDataBytes );
    } );
    unsigned long decompressedLength = chunk.size();

    if ( uncompress( (unsigned char *)&chunk.front(), &decompressedLength, chunkDataBytes, compressedSize ) != Z_OK
         || decompressedLength != chunk.size() ) {
      throw exFailedToDecompressChunk();
    }
  }

  size_t offsetInChunk = address & 0xffFF;

  if ( offsetInChunk > chunk.size() ) {
    throw exAddressOutOfRange();
  }

  return &chunk.front() + offsetInChunk;
}

}
