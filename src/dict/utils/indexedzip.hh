/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "btreeidx.hh"
#include "zipfile.hh"
#include <QMutex>

class IndexedZip: public BtreeIndexing::BtreeIndex
{
  ZipFile::SplitZipFile zip;
  bool zipIsOpen;
  QMutex mutex;

public:

  IndexedZip():
    zipIsOpen( false )
  {
  }

  using BtreeIndexing::BtreeIndex::openIndex;

  bool openZipFile( const QString & );

  bool isOpen() const
  {
    return zipIsOpen;
  }

  bool hasFile( const QString & name );

  bool loadFile( const QString & name, std::vector< char > & );
  bool loadFile( uint32_t offset, std::vector< char > & );

  bool indexFile( BtreeIndexing::IndexedWords & zipFileNames, quint32 * filesCount = 0 );
};
