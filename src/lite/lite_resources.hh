/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QMimeDatabase>
#include <QUrl>
#include <string>
#include <utility>

#include "article_maker.hh"
#include "base_type.hh"
#include "dict/dictionary.hh"
#include "sptr.hh"

using std::vector;

QByteArray getMimeTypeWithFallback( const QUrl & url, const QByteArray & data = QByteArray() );

enum class ResourceType {
  UNTITLE,
  WELCOME,
  BLANK
};

class ArticleResourceManager: public QObject
{
  Q_OBJECT

  const vector< sptr< Dictionary::Class > > & dictionaries;
  const ArticleMaker & articleMaker;
  QMimeDatabase db;

public:
  ArticleResourceManager( QObject * parent,
                          const vector< sptr< Dictionary::Class > > & dictionaries_,
                          const ArticleMaker & articleMaker_ );

  sptr< Dictionary::DataRequest > getResource( const QUrl & url, QString & contentType );

  sptr< Dictionary::DataRequest > getResource( const QUrl & url );

  std::string getHtml( ResourceType resourceType );

private:
  sptr< Dictionary::DataRequest > handleInternalScheme( const QUrl & url, QString & contentType );

  sptr< Dictionary::DataRequest > handleLookupScheme( const QUrl & url, QString & contentType );

  sptr< Dictionary::DataRequest > handleResourceScheme( const QUrl & url, QString & contentType );
};
