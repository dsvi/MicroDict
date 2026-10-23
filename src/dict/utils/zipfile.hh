/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QDateTime>
#include "splitfile.hh"

namespace ZipFile {

class SplitZipFile: public SplitFile::SplitFile
{
public:
  SplitZipFile() {}
  SplitZipFile( const QString & name );

  virtual void setFileName( const QString & name );

  QDateTime lastModified() const;

  qint64 calcAbsoluteOffset( qint64 offset, quint16 partNo );
};

enum CompressionMethod {
  Uncompressed,
  Deflated,
  Unsupported
};

struct CentralDirEntry
{
  QByteArray fileName;
  quint32 centralHeaderOffset;
  quint32 localHeaderOffset, compressedSize, uncompressedSize;
  CompressionMethod compressionMethod;
  bool fileNameInUTF8;
};

struct LocalFileHeader
{
  QByteArray fileName;

  quint32 compressedSize, uncompressedSize;
  CompressionMethod compressionMethod;
  quint32 offset;
};

bool positionAtCentralDir( SplitZipFile & );

bool readNextEntry( SplitZipFile &, CentralDirEntry & );

bool skipLocalHeader( SplitZipFile & zip );
bool readLocalHeaderFromCentral( SplitZipFile &, LocalFileHeader & );

}
