/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <set>
#include <list>
#include "config.hh"
#include "dict/dictionary.hh"
#include "instances.hh"
#include "wordfinder.hh"

class ArticleMaker: public QObject
{
  Q_OBJECT

  const std::vector< sptr< Dictionary::Class > > & dictionaries;
  const std::vector< Instances::Group > & groups;
  const Config::Preferences & cfg;

public:

  ArticleMaker( const std::vector< sptr< Dictionary::Class > > & dictionaries,
                const std::vector< Instances::Group > & groups,
                const Config::Preferences & cfg );

  sptr< Dictionary::DataRequest > makeDefinitionFor( const QString & word,
                                                     unsigned groupId,
                                                     const QMap< QString, QString > & contexts,
                                                     const QSet< QString > & mutedDicts = QSet< QString >(),
                                                     const QStringList & dictIDs        = QStringList(),
                                                     bool ignoreDiacritics              = false ) const;

  sptr< Dictionary::DataRequest > makeWelcomePage() const;

  sptr< Dictionary::DataRequest > makeNotFoundTextFor( const QString & word, const QString & group ) const;

  sptr< Dictionary::DataRequest > makeEmptyPage() const;

  sptr< Dictionary::DataRequest > makePicturePage( const std::string & url ) const;

  static bool adjustFilePath( QString & fileName );
  static QString welcomeWord();
  string makeUntitleHtml() const;
  string makeWelcomeHtml() const;
  string makeBlankHtml() const;

private:
  std::string makeHtmlHeader( const QString & word, const QString & icon, bool expandOptionalParts ) const;

  static std::string makeNotFoundBody( const QString & word, const QString & group );

  friend class ArticleRequest;
};

class ArticleRequest: public Dictionary::DataRequest
{
  Q_OBJECT

  QString word;
  Instances::Group group;
  QMap< QString, QString > contexts;
  std::vector< sptr< Dictionary::Class > > activeDicts;

  std::set< QString, std::less<> > alts;
  std::list< sptr< Dictionary::WordSearchRequest > > altSearches;
  std::list< sptr< Dictionary::DataRequest > > bodyRequests;
  bool altsDone{ false };
  bool bodyDone{ false };
  bool foundAnyDefinitions{ false };
  sptr< WordFinder > stemmedWordFinder;

  using Words    = QList< QString >;
  using Spacings = QList< QString >;

  std::pair< Words, Spacings > splitIntoWords( const QString & );

  std::pair< Words, Spacings > splittedWords;
  int currentSplittedWordStart;
  int currentSplittedWordEnd;
  QString currentSplittedWordCompound;
  QString lastGoodCompoundResult;
  bool firstCompoundWasFound;
  int articleSizeLimit;
  bool needExpandOptionalParts;
  bool ignoreDiacritics;

public:

  ArticleRequest( const QString & phrase,
                  const Instances::Group & group,
                  const QMap< QString, QString > & contexts,
                  const std::vector< sptr< Dictionary::Class > > & activeDicts,
                  const std::string & header,
                  int sizeLimit,
                  bool needExpandOptionalParts_,
                  bool ignoreDiacritics = false );

  virtual void cancel();

private slots:

  void altSearchFinished();
  void bodyFinished();
  void stemmedSearchFinished();
  void individualWordFinished();

private:
  int htmlTextSize( QString html );

  void compoundSearchNextStep( bool lastSearchSucceeded );

  QString makeSplittedWordCompound();

  std::string linkWord( const QString & );

  std::string escapeSpacing( const QString & );

  int findEndOfCloseDiv( const QString &, int pos );
  bool isCollapsable( Dictionary::DataRequest & req, const QString & dictId );
};
