/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "article_maker.hh"
#include "config.hh"
#include "folding.hh"
#include "globalbroadcaster.hh"
#include "globalregex.hh"
#include "htmlescape.hh"
#include "langcoder.hh"
#include "utils.hh"
#include <QTextDocumentFragment>
#include <QUrl>

#include <format>
#include <iterator>

using std::vector;
using std::string;
using std::set;
using std::list;

ArticleMaker::ArticleMaker( const vector< sptr< Dictionary::Class > > & dictionaries_,
                            const vector< Instances::Group > & groups_,
                            const Config::Preferences & cfg_ ):
  dictionaries( dictionaries_ ),
  groups( groups_ ),
  cfg( cfg_ )
{
}


std::string ArticleMaker::makeHtmlHeader( const QString & word, const QString & icon, bool expandOptionalParts ) const
{
  string result = R"(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
)";

  result += R"(<link href="qrc:///article-style.css"  media="all" rel="stylesheet" type="text/css">)";

  if ( expandOptionalParts ) {
    result += R"(<!-- Expand optional parts css -->
                 <style type="text/css" media="all">
                  .dsl_opt{
                    display: inline;
                   }
                  .hidden_expand_opt{  display: none;}
                  </style>)";
  }

  result += "<title>" + Html::escape( word.toStdString() ) + "</title>";

  if ( icon.size() ) {
    result +=
      R"(<link rel="icon" type="image/png" href="qrc:///flags/)" + Html::escape( icon.toUtf8().data() ) + "\" >\n";
  }

  result += "</head><body>";

  return result;
}

std::string ArticleMaker::makeNotFoundBody( const QString & word, const QString & group )
{
  string result( "<div class=\"gdnotfound\"><p>" );

  if ( word.size() ) {
    result += tr( "No translation for <b dir=\"%3\">%1</b> was found in group <b>%2</b>." )
                .arg( QString::fromUtf8( Html::escape( word.toUtf8().data() ).c_str() ),
                      QString::fromUtf8( Html::escape( group.toUtf8().data() ).c_str() ),
                      word.isRightToLeft() ? "rtl" : "ltr" )
                .toUtf8()
                .data();
  }
  else {
    result += tr( "No translation was found in group <b>%1</b>." )
                .arg( QString::fromUtf8( Html::escape( group.toUtf8().data() ).c_str() ) )
                .toUtf8()
                .data();
  }

  result += "</p></div>";

  return result;
}

string ArticleMaker::makeWelcomeHtml() const
{
  string result = makeHtmlHeader( welcomeWord(), QString(), cfg.alwaysExpandOptionalParts );

  result += tr( R"(
    <div class="welcome-container">
      <h3>Welcome to <strong>MicroDict</strong>!</h3>
      <section>
        <h4>To get started</h4>
        <ul>
          <li>Go to <span class="menu-path"><span>Edit</span><span class="menu-arrow"> → </span><span>Dictionaries</span></span> to add your dictionary files and organize them into groups.</li>
          <li>Press the global hotkey (F12 by default) to show this window and look up whatever the clipboard holds.</li>
          <li>To customize the program, explore the settings in <span class="menu-path"><span>Edit</span><span class="menu-arrow"> → </span><span>Preferences</span></span>. All settings have tooltips, so be sure to read them if anything is unclear.</li>
        </ul>
      </section>
      <footer class="welcome-footer">
        <small>Based on the original dictionary program by Konstantin Isakov (c) 2008-2013 and on its community of contributors. Licensed under GPLv3 or later.</small>
      </footer>
    </div>)" )
              .toUtf8()
              .data();

  result += "</body></html>";

  return result;
}

QString ArticleMaker::welcomeWord()
{
  return tr( "Welcome!" );
}

sptr< Dictionary::DataRequest > ArticleMaker::makeWelcomePage() const
{
  string welcome                           = makeWelcomeHtml();
  sptr< Dictionary::DataRequestInstant > r = std::make_shared< Dictionary::DataRequestInstant >( true );

  r->appendString( welcome );

  return r;
}

sptr< Dictionary::DataRequest > ArticleMaker::makeDefinitionFor( const QString & word,
                                                                 unsigned groupId,
                                                                 const QMap< QString, QString > & contexts,
                                                                 const QSet< QString > & mutedDicts,
                                                                 const QStringList & dictIDs,
                                                                 bool ignoreDiacritics ) const
{
  if ( !dictIDs.isEmpty() ) {
    std::vector< sptr< Dictionary::Class > > dicts;

    for ( const auto & dictId : dictIDs ) {
      for ( unsigned x = 0; x < dictionaries.size(); x++ ) {
        if ( dictId == QString::fromStdString( dictionaries[ x ]->getId() ) ) {
          dicts.push_back( dictionaries[ x ] );
          break;
        }
      }
    }

    string header = makeHtmlHeader( word, QString(), true );

    return std::make_shared< ArticleRequest >( word,
                                               Instances::Group{ groupId, "" },
                                               contexts,
                                               dicts,
                                               header,
                                               -1,
                                               true );
  }

  const Instances::Group * activeGroup = 0;

  for ( const auto & group : groups ) {
    if ( group.id == groupId ) {
      activeGroup = &group;
      break;
    }
  }

  const std::vector< sptr< Dictionary::Class > > & activeDicts = activeGroup ? activeGroup->dictionaries : dictionaries;

  string header = makeHtmlHeader( word,
                                  activeGroup && activeGroup->icon.size() ? activeGroup->icon : QString(),
                                  cfg.alwaysExpandOptionalParts );

  if ( mutedDicts.size() ) {
    std::vector< sptr< Dictionary::Class > > unmutedDicts;

    unmutedDicts.reserve( activeDicts.size() );

    for ( unsigned x = 0; x < activeDicts.size(); ++x ) {
      if ( !mutedDicts.contains( QString::fromStdString( activeDicts[ x ]->getId() ) ) ) {
        unmutedDicts.push_back( activeDicts[ x ] );
      }
    }

    return std::make_shared< ArticleRequest >(
      word,
      Instances::Group{ activeGroup ? activeGroup->id : 0, activeGroup ? activeGroup->name : "" },
      contexts,
      unmutedDicts,
      header,
      cfg.collapseBigArticles ? cfg.articleSizeLimit : -1,
      cfg.alwaysExpandOptionalParts,
      ignoreDiacritics );
  }
  else {
    return std::make_shared< ArticleRequest >(
      word,
      Instances::Group{ activeGroup ? activeGroup->id : 0, activeGroup ? activeGroup->name : "" },
      contexts,
      activeDicts,
      header,
      cfg.collapseBigArticles ? cfg.articleSizeLimit : -1,
      cfg.alwaysExpandOptionalParts,
      ignoreDiacritics );
  }
}


sptr< Dictionary::DataRequest > ArticleMaker::makeNotFoundTextFor( const QString & word, const QString & group ) const
{
  string result = makeHtmlHeader( word, QString(), true ) + makeNotFoundBody( word, group ) + "</body></html>";

  sptr< Dictionary::DataRequestInstant > r = std::make_shared< Dictionary::DataRequestInstant >( true );

  r->appendString( result );
  return r;
}

sptr< Dictionary::DataRequest > ArticleMaker::makeEmptyPage() const
{
  string result                            = makeUntitleHtml();
  sptr< Dictionary::DataRequestInstant > r = std::make_shared< Dictionary::DataRequestInstant >( true );

  r->appendString( result );
  return r;
}

string ArticleMaker::makeUntitleHtml() const
{
  return makeHtmlHeader( tr( "(untitled)" ), QString(), true ) + "</body></html>";
}

sptr< Dictionary::DataRequest > ArticleMaker::makePicturePage( const string & url ) const
{
  const string result =
    makeHtmlHeader( tr( "(picture)" ), QString(), true ) + R"(<img src=")" + url + R"(" />)" + "</body></html>";

  sptr< Dictionary::DataRequestInstant > r = std::make_shared< Dictionary::DataRequestInstant >( true );

  r->appendString( result );
  return r;
}


bool ArticleMaker::adjustFilePath( QString & fileName )
{
  QFileInfo info( fileName );
  if ( !info.isFile() ) {
    QString dir = Config::getConfigDir();
    dir.chop( 1 );
    info.setFile( dir + fileName );
    if ( info.isFile() ) {
      fileName = info.canonicalFilePath();
      return true;
    }
  }
  return false;
}
string ArticleMaker::makeBlankHtml() const
{
  return makeHtmlHeader( "", QString(), true ) + "</body></html>";
}

ArticleRequest::ArticleRequest( const QString & word,
                                const Instances::Group & group_,
                                const QMap< QString, QString > & contexts_,
                                const vector< sptr< Dictionary::Class > > & activeDicts_,
                                const string & header,
                                int sizeLimit,
                                bool needExpandOptionalParts_,
                                bool ignoreDiacritics_ ):
  word( word ),
  group( group_ ),
  contexts( contexts_ ),
  activeDicts( activeDicts_ ),
  articleSizeLimit( sizeLimit ),
  needExpandOptionalParts( needExpandOptionalParts_ ),
  ignoreDiacritics( ignoreDiacritics_ )
{

  hasAnyData = true;

  appendString( header );

  emit GlobalBroadcaster::instance() -> dictionaryClear( ActiveDictIds{ group.id, word } );

  for ( const auto & activeDict : activeDicts ) {
    const auto s = activeDict->findHeadwordsForSynonym( Text::removeTrailingZero( word ) );

    connect( s.get(), &Dictionary::Request::finished, this, &ArticleRequest::altSearchFinished, Qt::QueuedConnection );

    altSearches.push_back( s );
  }

  altSearchFinished();
}

void ArticleRequest::altSearchFinished()
{
  if ( altsDone ) {
    return;
  }

  for ( auto i = altSearches.begin(); i != altSearches.end(); ) {
    if ( ( *i )->isFinished() ) {
      for ( size_t count = ( *i )->matchesCount(), x = 0; x < count; ++x ) {
        alts.insert( ( **i )[ x ].word );
      }

      altSearches.erase( i++ );
    }
    else {
      ++i;
    }
  }

  if ( altSearches.empty() ) {

    altsDone = true;

    vector< QString > altsVector( alts.begin(), alts.end() );

    QString wordStd = word;

    if ( activeDicts.size() <= 1 ) {
      articleSizeLimit = -1;
    }

    for ( const auto & activeDict : activeDicts ) {
      try {
        if ( word == ":about" ) {
          QString description = activeDict->getDescription();
          auto r              = std::make_shared< Dictionary::DataRequestInstant >( true );
          r->appendString( description.toStdString() );
          bodyRequests.push_back( r );
        }
        else {
          sptr< Dictionary::DataRequest > r = activeDict->getArticle(
            wordStd,
            altsVector,
            Text::removeTrailingZero( contexts.value( QString::fromStdString( activeDict->getId() ) ) ),
            ignoreDiacritics );

          connect( r.get(), &Dictionary::Request::finished, this, &ArticleRequest::bodyFinished, Qt::QueuedConnection );

          bodyRequests.push_back( r );
        }
      }
      catch ( const std::exception & e ) {
        qWarning() << "getArticle request error:" << e.what() << "in"
                   << QString::fromStdString( activeDict->getName() );
      }
    }

    bodyFinished();
  }
}

int ArticleRequest::findEndOfCloseDiv( const QString & str, int pos )
{
  for ( ;; ) {
    const int n1 = str.indexOf( "</div>", pos );
    if ( n1 <= 0 ) {
      return n1;
    }

    const int n2 = str.indexOf( RX::Html::startDivTag, pos );
    if ( n2 <= 0 || n2 > n1 ) {
      return n1 + 6;
    }

    pos = findEndOfCloseDiv( str, n2 + 1 );
    if ( pos <= 0 ) {
      return pos;
    }
  }
}

bool ArticleRequest::isCollapsable( Dictionary::DataRequest & req, const QString & dictId )
{
  if ( GlobalBroadcaster::instance()->collapsedDicts.contains( dictId ) ) {
    return true;
  }

  bool collapse = false;

  if ( articleSizeLimit >= 0 ) {
    try {
      QMutexLocker _( &dataMutex );
      QString text = QString::fromUtf8( req.getFullData().data(), req.getFullData().size() );

      if ( !needExpandOptionalParts ) {
        for ( ;; ) {
          const int pos = text.indexOf( "<div class=\"dsl_opt\"" );
          if ( pos > 0 ) {
            const int endPos = findEndOfCloseDiv( text, pos + 1 );
            if ( endPos > pos ) {
              text.remove( pos, endPos - pos );
            }
            else {
              break;
            }
          }
          else {
            break;
          }
        }
      }

      int size = htmlTextSize( text );
      if ( size > articleSizeLimit ) {
        collapse = true;
      }
    }
    catch ( ... ) {
    }
  }
  return collapse;
}

void ArticleRequest::bodyFinished()
{
  if ( bodyDone ) {
    return;
  }

  bool wasUpdated = false;

  QStringList dictIds;
  while ( bodyRequests.size() ) {
    if ( bodyRequests.front()->isFinished() ) {

      Dictionary::DataRequest & req = *bodyRequests.front();

      QString errorString = req.getErrorString();

      if ( req.dataSize() >= 0 || errorString.size() ) {
        const sptr< Dictionary::Class > & activeDict = activeDicts[ activeDicts.size() - bodyRequests.size() ];

        string dictId = activeDict->getId();

        qDebug() << "dict:" << activeDict->getName().c_str() << " finished.";

        const bool firstArticle = dictIds.isEmpty();

        dictIds << QString::fromStdString( dictId );
        string head;

        string gdFrom = "gdfrom-" + Html::escape( dictId );

        bool collapse = isCollapsable( req, QString::fromStdString( dictId ) );

        string jsVal = Html::escapeForJavaScript( dictId );

        std::format_to( std::back_inserter( head ),
                       R"( <article class="gdarticle {0} {1}" id="{2}"
                           data-gd-id="{3}"
                           >)",
                       firstArticle ? "gdactivearticle" : "",
                       collapse ? " gdcollapsedarticle" : "",
                       gdFrom,
                       jsVal );

        std::format_to(
          std::back_inserter( head ),
          R"(<gd-dict-header class="gddictname" {1}  id="gddictname-{0}" title="{2}" role="button" aria-expanded="{5}">
                      <span class="gddicticon"><img src="gico://{0}/dicticon.png" alt=""></span>
                      <span class="gdfromprefix">{3}</span>
                      <span class="gddicttitle">{4}</span>
                     </gd-dict-header>)",
          dictId,
          collapse ? R"(style="cursor:pointer;")" : "",
          "",
          Html::escape( tr( "From " ).toStdString() ),
          Html::escape( activeDict->getName() ),
          collapse ? "false" : "true" );


        std::format_to(
          std::back_inserter( head ),
          R"(<section class="gdarticlebody gdlangfrom-{}" lang="{}" style="display:{}" id="gd-{}">)",
          LangCoder::intToCode2( activeDict->getLangFrom() ).toStdString(),
          LangCoder::intToCode2( activeDict->getLangTo() ).toStdString(),
          collapse ? "none" : "block",
          dictId );

        if ( errorString.size() ) {
          head += "<div class=\"gderrordesc\">"
            + Html::escape( tr( "Query error: %1" ).arg( errorString ).toUtf8().data() ) + "</div>";
        }

        appendString( head );

        try {
          if ( req.dataSize() > 0 ) {
            auto & d = req.getFullData();
            appendDataSlice( &d.front(), d.size() );
          }
        }
        catch ( const std::exception & e ) {
          qWarning() << "Failed to append the article body of" << activeDict->getName().c_str() << ":" << e.what();
        }

        auto separator = R"(</section></article><div style="clear:both;"></div><span class="gdarticleseparator"></span>)";
        appendString( separator );

        wasUpdated = true;

        foundAnyDefinitions = true;

        GlobalBroadcaster::instance()->pronounce_engine.finishDictionary( dictId );
      }
      bodyRequests.pop_front();
    }
    else {
      break;
    }
  }

  ActiveDictIds hittedWord{ group.id, word, dictIds };

  if ( bodyRequests.empty() ) {

    bodyDone = true;

    string footer;

    if ( !foundAnyDefinitions ) {

      footer += ArticleMaker::makeNotFoundBody( word.size() < 40 ? word : word.left( 40 ) + "...", group.name );

      stemmedWordFinder = std::make_shared< WordFinder >( this );

      connect( stemmedWordFinder.get(),
               &WordFinder::finished,
               this,
               &ArticleRequest::stemmedSearchFinished,
               Qt::QueuedConnection );

      stemmedWordFinder->stemmedMatch( word, activeDicts );
    }
    else {
      footer += R"(<div class="empty-space"></div>)";
      footer += "</body></html>";
    }

    appendString( footer );

    if ( stemmedWordFinder.get() ) {
      update();
      qDebug() << "send dicts(stemmed):" << hittedWord;
      emit GlobalBroadcaster::instance() -> dictionaryChanges( hittedWord );
      dictIds.clear();
    }
    else {
      finish();
      qDebug() << "send dicts(finished):" << hittedWord;
      emit GlobalBroadcaster::instance() -> dictionaryChanges( hittedWord );
      dictIds.clear();
    }
  }
  else if ( wasUpdated ) {
    update();
    qDebug() << "send dicts(updated):" << hittedWord;
    emit GlobalBroadcaster::instance() -> dictionaryChanges( hittedWord );
    dictIds.clear();
  }
}

int ArticleRequest::htmlTextSize( QString html )
{
  if ( html.contains( QRegularExpression( "<iframe\\s*[^>]*>", QRegularExpression::CaseInsensitiveOption ) ) ) {
    return 1000;
  }

  QString stripStyleSheet =
    html.remove( QRegularExpression( "<link\\s*[^>]*>", QRegularExpression::CaseInsensitiveOption ) )
      .remove( QRegularExpression( R"(<script[\s\S]*?>[\s\S]*?<\/script>)",
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption ) );
  int size = QTextDocumentFragment::fromHtml( stripStyleSheet ).toPlainText().length();

  return size;
}

void ArticleRequest::stemmedSearchFinished()
{

  WordFinder::SearchResults sr = stemmedWordFinder->getResults();

  string footer;

  bool continueMatching = false;

  if ( sr.size() ) {
    footer += R"(<div class="gdstemmedsuggestion"><span class="gdstemmedsuggestion_head">)"
      + Html::escape( tr( "Close words: " ).toUtf8().data() ) + "</span><span class=\"gdstemmedsuggestion_body\">";

    for ( unsigned x = 0; x < sr.size(); ++x ) {
      footer += linkWord( sr[ x ].first );

      if ( x != sr.size() - 1 ) {
        footer += ", ";
      }
    }

    footer += "</span></div>";
  }

  splittedWords = splitIntoWords( word );

  if ( splittedWords.first.size() > 1 ) {
    disconnect( stemmedWordFinder.get(), &WordFinder::finished, this, &ArticleRequest::stemmedSearchFinished );

    connect( stemmedWordFinder.get(),
             &WordFinder::finished,
             this,
             &ArticleRequest::individualWordFinished,
             Qt::QueuedConnection );

    currentSplittedWordStart = -1;
    currentSplittedWordEnd   = currentSplittedWordStart;

    firstCompoundWasFound = false;

    compoundSearchNextStep( false );

    continueMatching = true;
  }

  if ( !continueMatching ) {
    footer += "</body></html>";
  }

  appendString( footer );

  if ( continueMatching ) {
    update();
  }
  else {
    finish();
  }
}

void ArticleRequest::compoundSearchNextStep( bool lastSearchSucceeded )
{
  if ( !lastSearchSucceeded ) {

    string footer;

    if ( lastGoodCompoundResult.size() ) {
      if ( !firstCompoundWasFound ) {
        footer += R"(<div class="gdstemmedsuggestion"><span class="gdstemmedsuggestion_head">)"
          + Html::escape( tr( "Compound expressions: " ).toUtf8().data() )
          + "</span><div class=\"gdstemmedsuggestion_body\">";

        firstCompoundWasFound = true;
      }
      else {
        footer += " / ";
      }

      footer += linkWord( lastGoodCompoundResult );

      lastGoodCompoundResult.clear();
    }

    if ( currentSplittedWordStart >= splittedWords.first.size() - 2 ) {

      if ( firstCompoundWasFound ) {
        footer += "</div>";
      }

      footer += R"(<div class="gdstemmedsuggestion"><span class="gdstemmedsuggestion_head">)"
        + Html::escape( tr( "Individual words: " ).toUtf8().data() ) + "</span><div class=\"gdstemmedsuggestion_body\"";
      if ( splittedWords.first[ 0 ].isRightToLeft() ) {
        footer += " dir=\"rtl\"";
      }
      footer += ">";

      footer += escapeSpacing( splittedWords.second[ 0 ] );

      for ( int x = 0; x < splittedWords.first.size(); ++x ) {
        footer += linkWord( splittedWords.first[ x ] );
        footer += escapeSpacing( splittedWords.second[ x + 1 ] );
      }

      footer += "</div>";

      footer += "</body></html>";

      appendString( footer );

      finish();

      return;
    }

    if ( footer.size() ) {
      appendString( footer );
      update();
    }

    ++currentSplittedWordStart;
    currentSplittedWordEnd = currentSplittedWordStart + 1;
  }
  else {

    if ( currentSplittedWordEnd < splittedWords.first.size() - 1 ) {
      ++currentSplittedWordEnd;
    }
    else {

      ++currentSplittedWordEnd;

      compoundSearchNextStep( false );

      return;
    }
  }

  currentSplittedWordCompound = makeSplittedWordCompound();

  stemmedWordFinder->expressionMatch( currentSplittedWordCompound,
                                      activeDicts,
                                      40,
                                      Dictionary::SuitableForCompoundSearching );
}

QString ArticleRequest::makeSplittedWordCompound()
{
  QString result;

  result.clear();

  for ( int x = currentSplittedWordStart; x <= currentSplittedWordEnd; ++x ) {
    result.append( splittedWords.first[ x ] );

    if ( x < currentSplittedWordEnd ) {
      result.append( splittedWords.second[ x + 1 ].simplified() );
    }
  }

  return result;
}

void ArticleRequest::individualWordFinished()
{
  const WordFinder::SearchResults & results = stemmedWordFinder->getResults();

  if ( results.size() ) {
    QString source = Folding::applySimpleCaseOnly( currentSplittedWordCompound );

    bool hadSomething = false;

    for ( unsigned x = 0; x < results.size(); ++x ) {
      if ( results[ x ].second ) {
        hadSomething           = true;
        lastGoodCompoundResult = currentSplittedWordCompound;
        break;
      }

      QString result( Folding::applySimpleCaseOnly( results[ x ].first ) );

      if ( source.size() <= result.size() && result.startsWith( source ) ) {

        hadSomething = true;

        if ( source.size() == result.size() ) {
          lastGoodCompoundResult = currentSplittedWordCompound;
          break;
        }
      }
    }

    if ( hadSomething ) {
      compoundSearchNextStep( true );
      return;
    }
  }

  compoundSearchNextStep( false );
}

std::pair< ArticleRequest::Words, ArticleRequest::Spacings > ArticleRequest::splitIntoWords( const QString & input )
{
  std::pair< Words, Spacings > result;

  const QChar * ptr = input.data();

  for ( ;; ) {
    QString spacing;

    for ( ; ptr->unicode() && ( Folding::isPunct( ptr->unicode() ) || Folding::isWhitespace( ptr->unicode() ) );
          ++ptr ) {
      spacing.append( *ptr );
    }

    result.second.append( spacing );

    QString word;

    for ( ; ptr->unicode() && !( Folding::isPunct( ptr->unicode() ) || Folding::isWhitespace( ptr->unicode() ) );
          ++ptr ) {
      word.append( *ptr );
    }

    if ( word.isEmpty() ) {
      break;
    }

    result.first.append( word );
  }

  return result;
}

string ArticleRequest::linkWord( const QString & str )
{
  QUrl url;

  url.setScheme( "gdlookup" );
  url.setHost( "localhost" );
  url.setPath( Utils::Url::ensureLeadingSlash( str ) );

  string escapedResult = Html::escape( str.toUtf8().data() );
  return string( "<a href=\"" ) + url.toEncoded().data() + "\">" + escapedResult + "</a>";
}

std::string ArticleRequest::escapeSpacing( const QString & str )
{
  QByteArray spacing = Html::escape( str.toUtf8().data() ).c_str();

  spacing.replace( "\n", "<br>" );

  return spacing.data();
}

void ArticleRequest::cancel()
{
  if ( isFinished() ) {
    return;
  }
  if ( !altSearches.empty() ) {
    for ( list< sptr< Dictionary::WordSearchRequest > >::iterator i = altSearches.begin(); i != altSearches.end();
          ++i ) {
      ( *i )->cancel();
    }
  }
  if ( !bodyRequests.empty() ) {
    for ( list< sptr< Dictionary::DataRequest > >::iterator i = bodyRequests.begin(); i != bodyRequests.end(); ++i ) {
      ( *i )->cancel();
    }
  }
  if ( stemmedWordFinder.get() ) {
    stemmedWordFinder->cancel();
  }
  finish();
}
