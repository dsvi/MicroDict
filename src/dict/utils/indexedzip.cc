/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "indexedzip.hh"
#include "zipfile.hh"
#include <zlib.h>
#include "text.hh"
#include "iconv.hh"
#include <QMutexLocker>

using namespace BtreeIndexing;
using std::vector;

bool IndexedZip::openZipFile( const QString & name )
{
  zip.setFileName( name );

  zipIsOpen = zip.open( QFile::ReadOnly );

  return zipIsOpen;
}

bool IndexedZip::hasFile( const QString & name )
{
  if ( !zipIsOpen ) {
    return false;
  }

  vector< WordArticleLink > links = findArticles( name );

  return !links.empty();
}

bool IndexedZip::loadFile( const QString & name, vector< char > & data )
{
  if ( !zipIsOpen ) {
    return false;
  }

  vector< WordArticleLink > links = findArticles( name );

  if ( links.empty() ) {
    return false;
  }

  return loadFile( links[ 0 ].articleOffset, data );
}

bool IndexedZip::loadFile( uint32_t offset, vector< char > & data )
{
  if ( !zipIsOpen ) {
    return false;
  }

  QMutexLocker _( &mutex );
  if ( !zip.seek( offset ) ) {
    return false;
  }

  ZipFile::LocalFileHeader header;

  if ( !ZipFile::readLocalHeaderFromCentral( zip, header ) ) {
    vector< string > zipFileNames;
    zip.getFilenames( zipFileNames );
    qDebug() << "Failed to load header";
    string filename;
    if ( zip.getCurrentFile() < (int)zipFileNames.size() ) {
      filename = zipFileNames.at( zip.getCurrentFile() );
    }

    qDebug() << "Current failed zip file:" << QString::fromStdString( filename );

    zip.seek( offset );
    const QByteArray preview = zip.read( 16 );
    qDebug() << "Header read failed: offset in index:" << offset << "| zip file size:" << zip.size()
             << "| expected signature: 0x02014b50"
             << "| actual first 16 bytes:" << QString::fromLatin1( preview.toHex( ' ' ) );
    return false;
  }

  zip.seek( header.offset );
  if ( !ZipFile::skipLocalHeader( zip ) ) {
    zip.seek( header.offset );
    const QByteArray preview = zip.read( 16 );
    qDebug() << "Failed to skip local header: local header offset:" << header.offset << "| zip file size:" << zip.size()
             << "| expected signature: 0x04034b50"
             << "| actual first 16 bytes:" << QString::fromLatin1( preview.toHex( ' ' ) );
    return false;
  }

  switch ( header.compressionMethod ) {
    case ZipFile::Uncompressed:
      qDebug() << "Uncompressed";
      data.resize( header.uncompressedSize );
      return (size_t)zip.read( &data.front(), data.size() ) == data.size();

    case ZipFile::Deflated: {
      if ( header.compressedSize > 100000000 ) {
        qDebug() << "Unusually large compressed size:" << header.compressedSize;
        return false;
      }
      if ( header.compressedSize == 0 ) {
        qDebug() << "compressed size is 0;";
        return false;
      }
      QByteArray compressedData = zip.read( header.compressedSize );

      if ( compressedData.size() != (int)header.compressedSize ) {
        return false;
      }

      data.resize( header.uncompressedSize );

      z_stream stream = {};

      stream.next_in   = (Bytef *)compressedData.data();
      stream.avail_in  = compressedData.size();
      stream.next_out  = (Bytef *)&data.front();
      stream.avail_out = data.size();

      if ( inflateInit2( &stream, -MAX_WBITS ) != Z_OK ) {
        data.clear();
        return false;
      }

      int ret = inflate( &stream, Z_FINISH );
      if ( ret != Z_STREAM_END ) {
        qDebug() << "Not zstream end! Stream total_in:" << stream.total_in << "total_out:" << stream.total_out
                 << "msg:" << ( stream.msg ? stream.msg : "none" );

        data.clear();

        int endRet = inflateEnd( &stream );
        if ( endRet != Z_OK ) {
          qDebug() << "inflateEnd failed after inflate! msg:" << ( stream.msg ? stream.msg : "none" );
        }

        return false;
      }

      ret = inflateEnd( &stream );
      if ( ret != Z_OK ) {
        qDebug() << "inflateEnd failed! msg:" << ( stream.msg ? stream.msg : "none" );

        data.clear();

        return false;
      }

      return true;
    }

    default:

      return false;
  }
}

bool IndexedZip::indexFile( BtreeIndexing::IndexedWords & zipFileNames, quint32 * filesCount )
{
  if ( !zipIsOpen ) {
    return false;
  }

  QMutexLocker _( &mutex );
  if ( !ZipFile::positionAtCentralDir( zip ) ) {
    return false;
  }

  ZipFile::CentralDirEntry entry;

  if ( filesCount ) {
    *filesCount = 0;
  }

  while ( ZipFile::readNextEntry( zip, entry ) ) {
    if ( entry.compressionMethod == ZipFile::Unsupported ) {
      qWarning() << "Zip warning: compression method unsupported -- skipping file" << entry.fileName.data();
      continue;
    }

    if ( entry.fileNameInUTF8 ) {
      zipFileNames.addSingleWord( Text::toQString( entry.fileName.data() ), entry.centralHeaderOffset );
      if ( filesCount ) {
        *filesCount += 1;
      }
    }
    else {
      try {
        auto encoding = Iconv::findValidEncoding( { "LOCAL", "IBM437", "CP866", "CP1251", "UTF-8" } );
        if ( encoding.isEmpty() ) {
          qWarning() << "Zip warning: failed to detect encoding -- skipping file" << entry.fileName.data();
          continue;
        }
        QString nameInSystemLocale =
          Iconv::toQString( encoding.toUtf8().constData(), entry.fileName.constData(), entry.fileName.size() );
        if ( !nameInSystemLocale.isEmpty() ) {
          zipFileNames.addSingleWord( nameInSystemLocale, entry.centralHeaderOffset );

          if ( filesCount != 0 ) {
            *filesCount += 1;
          }
        }
      }
      catch ( Iconv::Ex & ) {
      }
    }
  }
  return true;
}
