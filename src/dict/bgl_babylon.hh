/***************************************************************************
 *   Copyright (C) 2007 by Raul Fernandes and Karl Grill                   *
 *   rgbr@yahoo.com.br                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 ***************************************************************************/

#pragma once

#include <zlib.h>
#include <string>
#include <vector>
#include <qglobal.h>
#include <stdint.h>
#include <array>

#ifndef blgCode2Int
  #define blgCode2Int( index, code0, code1 ) \
    ( ( (uint32_t)index ) << 16 ) + ( ( (uint32_t)code1 ) << 8 ) + (uint32_t)code0
#endif
const std::array< quint32, 60 > bgl_language = {
  blgCode2Int( 0, 'e', 'n' ),  blgCode2Int( 0, 'f', 'r' ),  blgCode2Int( 0, 'i', 't' ),  blgCode2Int( 0, 'e', 's' ),
  blgCode2Int( 0, 'n', 'l' ),  blgCode2Int( 0, 'p', 't' ),  blgCode2Int( 0, 'd', 'e' ),  blgCode2Int( 0, 'r', 'u' ),
  blgCode2Int( 0, 'j', 'a' ),  blgCode2Int( 1, 'z', 'h' ),  blgCode2Int( 2, 'z', 'h' ),  blgCode2Int( 0, 'e', 'l' ),
  blgCode2Int( 0, 'k', 'o' ),  blgCode2Int( 0, 't', 'r' ),  blgCode2Int( 0, 'h', 'e' ),  blgCode2Int( 0, 'a', 'r' ),
  blgCode2Int( 0, 't', 'h' ),  blgCode2Int( 3, 0, 0 ),      blgCode2Int( 4, 'z', 'h' ),  blgCode2Int( 5, 'z', 'h' ),
  blgCode2Int( 6, 0, 0 ),      blgCode2Int( 7, 0, 0 ),      blgCode2Int( 8, 'r', 'u' ),  blgCode2Int( 9, 'j', 'a' ),
  blgCode2Int( 10, 0, 0 ),     blgCode2Int( 11, 'e', 'l' ), blgCode2Int( 12, 'k', 'o' ), blgCode2Int( 13, 't', 'r' ),
  blgCode2Int( 14, 't', 'h' ), blgCode2Int( 0, 'p', 'l' ),  blgCode2Int( 0, 'h', 'u' ),  blgCode2Int( 0, 'c', 's' ),
  blgCode2Int( 0, 'l', 't' ),  blgCode2Int( 0, 'l', 'v' ),  blgCode2Int( 0, 'c', 'a' ),  blgCode2Int( 0, 'h', 'r' ),
  blgCode2Int( 0, 's', 'r' ),  blgCode2Int( 0, 's', 'k' ),  blgCode2Int( 0, 's', 'q' ),  blgCode2Int( 0, 'u', 'r' ),
  blgCode2Int( 0, 's', 'l' ),  blgCode2Int( 0, 'e', 't' ),  blgCode2Int( 0, 'b', 'g' ),  blgCode2Int( 0, 'd', 'a' ),
  blgCode2Int( 0, 'f', 'i' ),  blgCode2Int( 0, 'i', 's' ),  blgCode2Int( 0, 'n', 'o' ),  blgCode2Int( 0, 'r', 'o' ),
  blgCode2Int( 0, 's', 'v' ),  blgCode2Int( 0, 'u', 'k' ),  blgCode2Int( 0, 'b', 'e' ),  blgCode2Int( 0, 'f', 'a' ),
  blgCode2Int( 0, 'e', 'u' ),  blgCode2Int( 0, 'm', 'k' ),  blgCode2Int( 0, 'a', 'f' ),  blgCode2Int( 0, 'f', 'o' ),
  blgCode2Int( 0, 'l', 'a' ),  blgCode2Int( 0, 'e', 'o' ),  blgCode2Int( 15, 0, 0 ),     blgCode2Int( 0, 'h', 'y' ) };

const std::vector< std::string > bgl_charset = { "WINDOWS-1252",
                                                 "WINDOWS-1252",
                                                 "WINDOWS-1250",
                                                 "WINDOWS-1251",
                                                 "CP932",
                                                 "BIG5",
                                                 "GB18030",
                                                 "CP1257",
                                                 "CP1253",
                                                 "EUC-KR",
                                                 "ISO-8859-9",
                                                 "WINDOWS-1255",
                                                 "CP1256",
                                                 "CP874" };

const std::array< std::string, 11 > partOfSpeech = {
  "n.", "adj.", "v.", "adv.", "interj.", "pron.", "prep.", "conj.", "suff.", "pref.", "art." };

struct bgl_block
{
  unsigned type = 0;
  unsigned length = 0;
  std::vector<char> data;
};

struct bgl_entry
{
  std::string headword;
  std::string definition;
  std::string displayedHeadword;
  std::vector< std::string > alternates;
};

class Babylon
{
public:
  Babylon( const std::string & );
  ~Babylon();

  class ResourceHandler
  {
  public:

    virtual void handleBabylonResource( const std::string & filename, const char * data, size_t size ) = 0;

    virtual ~ResourceHandler() {}
  };

  void setResourcePrefix( const std::string & prefix )
  {
    m_resourcePrefix = prefix;
  }

  bool open();
  void close();
  bool readBlock( bgl_block & );
  bool read( const std::string & source_charset, const std::string & target_charset );
  bgl_entry readEntry( ResourceHandler * = 0 );

  inline std::string title() const
  {
    return m_title;
  }
  inline std::string author() const
  {
    return m_author;
  }
  inline std::string email() const
  {
    return m_email;
  }
  inline std::string description() const
  {
    return m_description;
  }
  inline std::string copyright() const
  {
    return m_copyright;
  }
  inline quint32 sourceLang() const
  {
    return m_sourceLang;
  }
  inline quint32 targetLang() const
  {
    return m_targetLang;
  }

  inline std::string filename() const
  {
    return m_filename;
  }

  const std::vector< char > & getIcon() const
  {
    return icon;
  }

  enum {
    ParserVersion = 17
  };

private:
  unsigned int bgl_readnum( int );
  void convertToUtf8( std::string &, unsigned int = 0 );

  std::string m_filename;
  gzFile file;

  std::string m_title;
  std::string m_author;
  std::string m_email;
  std::string m_description;
  std::string m_copyright;
  quint32 m_sourceLang;
  quint32 m_targetLang;
  unsigned int m_numEntries;
  std::string m_defaultCharset;
  std::string m_sourceCharset;
  std::string m_targetCharset;
  std::vector< char > icon;

  std::string m_resourcePrefix;

  enum CHARSET {
    BGL_DEFAULT_CHARSET,
    BGL_SOURCE_CHARSET,
    BGL_TARGET_CHARSET
  };
};
