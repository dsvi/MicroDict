/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QAction>
#include <QMap>
#include <QSet>
#include <list>
#include "lite/lite_resources.hh"
#include "audio/audioplayerinterface.hh"
#include "instances.hh"
#include "groupcombobox.hh"
#include "globalbroadcaster.hh"
#include "base_type.hh"
#include "articlewebview.hh"
#include "articlewebpage.hh"
#include "ui/searchpanel.hh"
#include "ui/ftssearchpanel.hh"
#include "dictionary_group.hh"

class ResourceToSaveHandler;

class ArticleView: public QWidget
{
  Q_OBJECT

  ArticleResourceManager & articleResMgr;
  const AudioPlayerPtr & audioPlayer;
  std::unique_ptr< DictionaryGroup > dictionaryGroup;
  QString customTitle;
  const Config::Class & cfg;

  QAction pasteAction, articleUpAction, articleDownAction, goBackAction, goForwardAction, selectCurrentArticleAction,
    copyAsTextAction;

  bool expandOptionalParts;

  QSet< QString > desktopOpenedTempFiles;

  QSet< QString > audioTempFiles;

  QAction * dictionaryBarToggled;

  unsigned currentGroupId;
  const QLineEdit * translateLine;

  QString currentWord;

  QStringList currentActiveDictIds;

  bool historyMode = false;

  QString activeDictId;

  QString audioLink_;
  QByteArray lastAudioData;
  QString lastAudioUrl;

  QString firstAvailableText;
  QStringList uniqueMatches;

  QString delayedHighlightText;

  QString pendingScrollTo;

  void highlightFTSResults();
  void performFtsFindOperation( bool backwards );

public:
  ArticleView( QWidget * parent,
               ArticleResourceManager &,
               const Config::Class & cfg,
               const QLineEdit * translateLine,
               QAction * dictionaryBarToggled = nullptr,
               unsigned currentGroupId        = 0 );

  void setCurrentGroupId( unsigned groupId );
  unsigned getCurrentGroupId();

  void setAudioLink( QString audioLink );
  QString getAudioLink() const;

  QSize minimumSizeHint() const override;
  void clearContent();

  ~ArticleView();

  void load( const QUrl & url, const QString & customTitle = {} );

  void load( QString url, const QString & customTitle = {} );

  void setCustomTitle( const QString & customTitle );

  static QString scrollToFromDictionaryId( const QString & dictionaryId );

  void showDefinition( const QString & word,
                       unsigned group,
                       const QString & scrollTo  = QString(),
                       const Contexts & contexts = Contexts() );

  void showDefinition( const QString & word,
                       const QStringList & dictIDs,
                       const QString & searchRegExp,
                       unsigned group,
                       bool ignoreDiacritics );
  void showDefinition( const QString & word, const QStringList & dictIDs, unsigned group, bool ignoreDiacritics );

  void openLink( const QUrl & url,
                 const QUrl & referrer,
                 const QString & scrollTo  = QString(),
                 const Contexts & contexts = Contexts() );
  void playAudio( const QUrl & url );
  void audioDownloadFinished( const sptr< Dictionary::DataRequest > & req );

  void updateMutedContents();

  bool canGoBack();
  bool canGoForward();

  void setSelectionBySingleClick( bool set );

  void setDelayedHighlightText( const QString & text );

  void syncBackgroundColorWithCfgDarkReader() const;

  QString getCurrentWord();
  void setCurrentWord( const QString & word );

  bool isInternalPage() const
  {
    return webview->url().scheme() == "gdinternal";
  }

  QUrl url() const
  {
    return webview->url();
  }

  void applyFontPreferences();

private:
  ArticleWebView * webview;
  SearchPanel * searchPanel;
  FtsSearchPanel * ftsSearchPanel;

public slots:

  void back();

  void forward();

  void focus()
  {
    webview->setFocus();
  }


public:

  void reload();

  void applyColorSchemeAndReload();

  void stopSound();

  void hasSound( const std::function< void( bool has ) > & callback );

  void playSound();

  void setZoomFactor( qreal factor )
  {
    qreal existedFactor = webview->zoomFactor();
    if ( !qFuzzyCompare( existedFactor, factor ) ) {
      qDebug() << "zoom factor ,existed:" << existedFactor << "set:" << factor;
      webview->setZoomFactor( factor );
    }
  }

  void toHtml( const std::function< void( QString & ) > & callback );

  void setHtml( const QString & content, const QUrl & baseUrl );
  ArticleWebPage * page();
  void setContent( const QByteArray & data, const QString & mimeType = QString(), const QUrl & baseUrl = QUrl() );

  QString getTitle();

  QString getWord() const;

  bool closeSearch();

  bool isSearchOpened();

  void jumpToDictionary( const QString &, bool force );

  QStringList getArticlesList();

  QString getActiveArticleId();
  void setActiveArticleId( const QString & );

  ResourceToSaveHandler * saveResource( const QUrl & url, const QString & fileName );

  void findText( QString & text,
                 const QTextDocument::FindFlags & f,
                 const std::function< void( bool match ) > & callback = nullptr );

signals:

  void titleChanged( ArticleView *, const QString & title );

  void pageLoaded( ArticleView * );

  void openLinkInNewTab( const QUrl &, const QUrl & referrer, const QString & fromArticle, const Contexts & contexts );
  void showDefinitionInNewTab( const QString & word,
                               unsigned group,
                               const QString & fromArticle,
                               const Contexts & contexts );

  void sendWordToHistory( const QString & word );

  void typingEvent( const QString & text );

  void statusBarMessage( const QString & message, int timeout = 0, const QPixmap & pixmap = QPixmap() );

  void showDictsPane();
  void updateFoundInDictsList();

  void activeArticleChanged( const ArticleView *, const QString & id );

  void forceAddWordToHistory( const QString & word );

  void closePopupMenu();

  void sendWordToInputLine( const QString & word );

  void storeResourceSavePath( const QString & );

  void zoomIn();
  void zoomOut();

  void saveBookmarkSignal( const QString & bookmark );

  void translateSelectedText( const QString & word, const QUrl & url, const QString & currentArticle );

public slots:

  void openSearch();

  void on_searchPrevious_clicked();
  void on_searchNext_clicked();

  bool handleF3( QObject * obj, QEvent * ev );

  void selectCurrentArticle();

  void linkClicked( const QUrl & );

private slots:
  void loadFinished( bool ok );
  void handleTitleChanged( const QString & title );

  void linkHovered( const QString & link );
  void contextMenuRequested( const QPoint & );

  bool isAudioLink( QUrl & targetUrl )
  {
    return ( targetUrl.scheme() == "gdau" || Utils::Url::isAudioUrl( targetUrl ) );
  }

  void resourceDownloadFinished( const sptr< Dictionary::DataRequest > & req, const QUrl & resourceDownloadUrl );

  void pasteTriggered();

  unsigned getCurrentGroup();

  void moveOneArticleUp();

  void moveOneArticleDown();

  void on_searchText_textEdited();
  void on_searchText_returnPressed();
  void on_searchCloseButton_clicked();
  void on_searchCaseSensitive_clicked( bool );

  void on_ftsSearchPrevious_clicked();
  void on_ftsSearchNext_clicked();

  void doubleClicked( QPoint pos );

  void audioPlayerError( const QString & message );

  void copyAsText();
  void copyResourceToClipboard( const QUrl & url );

  void setActiveDictIds( const ActiveDictIds & ad );

  void dictionaryClear( const ActiveDictIds & ad );

public:

  unsigned getGroup( const QUrl & );

private:
  QString getCurrentArticle();

  bool setCurrentArticle( const QString &, bool moveToIt = false );

  bool isDarkModeEnabled() const;

  void cleanupTemp();

  bool eventFilter( QObject * obj, QEvent * ev ) override;

  void performFindOperation( bool backwards );
};

class ResourceToSaveHandler: public QObject
{
  Q_OBJECT

public:
  explicit ResourceToSaveHandler( ArticleView * view, QString fileName );
  void addRequest( const sptr< Dictionary::DataRequest > & req );
  bool isEmpty()
  {
    return downloadRequests.empty();
  }

signals:
  void done();
  void statusBarMessage( const QString & message, int timeout = 0, const QPixmap & pixmap = QPixmap() );

public slots:
  void downloadFinished();

private:
  std::list< sptr< Dictionary::DataRequest > > downloadRequests;
  QString fileName;
  bool alreadyDone;
};
