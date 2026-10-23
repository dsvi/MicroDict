/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <string>
#include <list>
#include <vector>
#include <zlib.h>
#include "dictionary.hh"
#include "iconv.hh"
#include "text.hh"

namespace Dsl {
namespace Details {

using std::string;
using std::list;
using std::vector;
using Text::Encoding;
using Text::LineFeed;

string findCodeForDslId( int id );

bool isAtSignFirst( const QString & str );

struct ArticleDom
{
  struct Node: public list< Node >
  {
    bool isTag;
    QString tagName;
    QString tagAttrs;
    QString text;

    class Text
    {};
    class Tag
    {};

    Node( Tag, const QString & name, const QString & attrs ):
      isTag( true ),
      tagName( name ),
      tagAttrs( attrs )
    {
    }

    Node( Text, const QString & text_ ):
      isTag( false ),
      text( text_ )
    {
    }

    QString renderAsText( bool stripTrsTag = false ) const;
  };

  explicit ArticleDom( const QString &, const string & dictName = string(), const QString & headword_ = QString() );

  Node root;

private:

  void openTag( const QString & name, const QString & attr, list< Node * > & stack );

  void closeTag( const QString & name, list< Node * > & stack, bool warn = true );

  bool atSignFirstInLine();

  const QString & source;
  qsizetype stringPos, lineStartPos;

  class eot: std::exception
  {};

  QChar ch;
  bool escaped;
  unsigned transcriptionCount;
  unsigned mediaCount;

  void nextChar();

  QChar peek() const
  {
    return stringPos < source.size() ? source[ stringPos ] : QChar();
  }

  string dictionaryName;
  QString headword;
};

class DslScanner
{
  gzFile f;
  Encoding encoding;
  QString dictionaryName;
  QString langFrom, langTo;
  QString soundDictionary;
  char readBuffer[ 65536 ];
  char * readBufferPtr;
  LineFeed lineFeed;
  qsizetype readBufferLeft;
  unsigned linesRead;

public:

  DEF_EX( Ex, "Dsl scanner exception", Dictionary::Ex )
  DEF_EX_STR( exCantOpen, "Can't open .dsl file", Ex )
  DEF_EX( exCantReadDslFile, "Can't read .dsl file", Ex )
  DEF_EX_STR( exMalformedDslFile, "The .dsl file is malformed:", Ex )
  DEF_EX( exUnknownCodePage, "The .dsl file specified an unknown code page", Ex )
  DEF_EX( exEncodingError, "Encoding error", Ex )

  explicit DslScanner( const string & fileName );
  ~DslScanner() noexcept;

  Encoding getEncoding() const
  {
    return encoding;
  }

  const QString & getDictionaryName() const
  {
    return dictionaryName;
  }

  const QString & getLangFrom() const
  {
    return langFrom;
  }

  const QString & getLangTo() const
  {
    return langTo;
  }

  const QString & getSoundDictionaryName() const
  {
    return soundDictionary;
  }

  bool readNextLine( QString &, size_t & offset, bool only_head_word = false );

  bool readNextLineWithoutComments( QString &, size_t & offset, bool only_headword = false );

  unsigned getLinesRead() const
  {
    return linesRead;
  }

};

void processUnsortedParts( QString & str, bool strip );

void expandOptionalParts( QString & str, list< QString > * result, qsizetype x = 0, bool inside_recurse = false );

void expandTildes( QString & str, const QString & tildeReplacement );

void unescapeDsl( QString & str );

void normalizeHeadword( QString & );

void stripComments( QString &, bool & );

quint32 dslLanguageToId( const QString & name );

}
}
