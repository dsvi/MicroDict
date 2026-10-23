/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "articleview.hh"
#include "dict/programs.hh"
#include "folding.hh"
#include "gestures.hh"
#include "globalbroadcaster.hh"
#include "globalregex.hh"
#include "lite/lite_theme.hh"
#include "globalregex.hh"
#include "utils.hh"
#include "htmlescape.hh"
#include "article_maker.hh"
#include <QBuffer>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMimeDatabase>
#include <QMimeData>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUuid>
#include <QTimer>
#include <QVariant>
#include <QDateTime>
#include <map>
#include <QApplication>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <QStyleHints>
#include <utility>
using std::map;
using std::list;


namespace {

const char * const scrollToPrefix = "gdfrom-";

bool isScrollTo( const QString & id )
{
  return id.startsWith( scrollToPrefix );
}

QString dictionaryIdFromScrollTo( const QString & scrollTo )
{
  Q_ASSERT( isScrollTo( scrollTo ) );
  constexpr int scrollToPrefixLength = 7;
  return scrollTo.mid( scrollToPrefixLength );
}

QString searchStatusMessageNoMatches()
{
  return ArticleView::tr( "Phrase not found" );
}

QString searchStatusMessage( int activeMatch, int matchCount )
{
  Q_ASSERT( matchCount > 0 );
  Q_ASSERT( activeMatch > 0 );
  Q_ASSERT( activeMatch <= matchCount );
  return ArticleView::tr( "%1 of %2 matches" ).arg( activeMatch ).arg( matchCount );
}

}

QString ArticleView::scrollToFromDictionaryId( const QString & dictionaryId )
{
  Q_ASSERT( !isScrollTo( dictionaryId ) );
  return scrollToPrefix + dictionaryId;
}

ArticleView::ArticleView( QWidget * parent,
                          ArticleResourceManager & nm,
                          const Config::Class & cfg_,
                          const QLineEdit * translateLine_,
                          QAction * dictionaryBarToggled_,
                          unsigned int currentGroupId_ ):
  QWidget( parent ),
  articleResMgr( nm ),
  audioPlayer( *GlobalBroadcaster::instance()->getAudioPlayer() ),
  dictionaryGroup( std::make_unique< DictionaryGroup >( *GlobalBroadcaster::instance()->getAllDictionaries(),
                                                        *GlobalBroadcaster::instance()->getGroups() ) ),
  cfg( cfg_ ),
  pasteAction( this ),
  articleUpAction( this ),
  articleDownAction( this ),
  goBackAction( this ),
  goForwardAction( this ),
  selectCurrentArticleAction( this ),
  copyAsTextAction( this ),
  dictionaryBarToggled( dictionaryBarToggled_ ),
  currentGroupId( currentGroupId_ ),
  translateLine( translateLine_ )
{
  webview = new ArticleWebView( this, articleResMgr );
  ftsSearchPanel = new FtsSearchPanel( this );
  searchPanel    = new SearchPanel( this );
  searchPanel->hide();
  ftsSearchPanel->hide();
  auto * mainLayout = new QVBoxLayout( this );
  mainLayout->addWidget( webview );
  mainLayout->addWidget( ftsSearchPanel );
  mainLayout->addWidget( searchPanel );

  webview->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
  ftsSearchPanel->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
  searchPanel->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

  mainLayout->setContentsMargins( 0, 0, 0, 0 );

  connect( searchPanel->previous, &QPushButton::clicked, this, &ArticleView::on_searchPrevious_clicked );
  connect( searchPanel->next, &QPushButton::clicked, this, &ArticleView::on_searchNext_clicked );
  connect( searchPanel->close, &QPushButton::clicked, this, &ArticleView::on_searchCloseButton_clicked );
  connect( searchPanel->caseSensitive, &QCheckBox::toggled, this, &ArticleView::on_searchCaseSensitive_clicked );
  connect( searchPanel->lineEdit, &QLineEdit::textEdited, this, &ArticleView::on_searchText_textEdited );
  connect( searchPanel->lineEdit, &QLineEdit::returnPressed, this, &ArticleView::on_searchText_returnPressed );
  connect( ftsSearchPanel->next, &QPushButton::clicked, this, &ArticleView::on_ftsSearchNext_clicked );
  connect( ftsSearchPanel->previous, &QPushButton::clicked, this, &ArticleView::on_ftsSearchPrevious_clicked );

  webview->setUp( const_cast< Config::Class * >( &cfg ) );

  syncBackgroundColorWithCfgDarkReader();

  if ( followsDesktopTheme( cfg.preferences.darkReaderMode ) ) {
    connect( &SystemAppearance::instance(),
             &SystemAppearance::changed,
             this,
             &ArticleView::applyColorSchemeAndReload );
  }
  syncBackgroundColorWithCfgDarkReader();

  goBackAction.setShortcut( QKeySequence( "Alt+Left" ) );
  webview->addAction( &goBackAction );
  connect( &goBackAction, &QAction::triggered, this, &ArticleView::back );

  goForwardAction.setShortcut( QKeySequence( "Alt+Right" ) );
  webview->addAction( &goForwardAction );
  connect( &goForwardAction, &QAction::triggered, this, &ArticleView::forward );

  QAction * copyAction = new QAction( this );
  copyAction->setShortcut( QKeySequence::Copy );
  webview->addAction( copyAction );
  connect( copyAction, &QAction::triggered, webview, [ this ]() {
    webview->copy();
  } );

  QAction * selectAll = new QAction( this );
  selectAll->setShortcut( QKeySequence::SelectAll );
  selectAll->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  webview->addAction( selectAll );
  connect( selectAll, &QAction::triggered, webview, [ this ]() {
    webview->selectAll();
  } );

  webview->setContextMenuPolicy( Qt::CustomContextMenu );

  connect( webview, &ArticleWebView::loadFinished, this, &ArticleView::loadFinished );

  connect( webview, &ArticleWebView::linkClicked, this, &ArticleView::linkClicked );

  connect( webview->page(), &ArticleWebPage::titleChanged, this, &ArticleView::handleTitleChanged );

  connect( webview, &QWidget::customContextMenuRequested, this, &ArticleView::contextMenuRequested );

  connect( webview, &ArticleWebView::linkHovered, this, &ArticleView::linkHovered );

  connect( webview, &ArticleWebView::doubleClicked, this, &ArticleView::doubleClicked );

  pasteAction.setShortcut( QKeySequence::Paste );
  webview->addAction( &pasteAction );
  connect( &pasteAction, &QAction::triggered, this, &ArticleView::pasteTriggered );

  articleUpAction.setShortcut( QKeySequence( "Alt+Up" ) );
  webview->addAction( &articleUpAction );
  connect( &articleUpAction, &QAction::triggered, this, &ArticleView::moveOneArticleUp );

  articleDownAction.setShortcut( QKeySequence( "Alt+Down" ) );
  webview->addAction( &articleDownAction );
  connect( &articleDownAction, &QAction::triggered, this, &ArticleView::moveOneArticleDown );

  selectCurrentArticleAction.setShortcut( QKeySequence( "Ctrl+Shift+A" ) );
  selectCurrentArticleAction.setText( tr( "Select Current Article" ) );
  webview->addAction( &selectCurrentArticleAction );
  connect( &selectCurrentArticleAction, &QAction::triggered, this, &ArticleView::selectCurrentArticle );

  copyAsTextAction.setShortcut( QKeySequence( "Ctrl+Shift+C" ) );
  copyAsTextAction.setText( tr( "Copy as text" ) );
  webview->addAction( &copyAsTextAction );
  connect( &copyAsTextAction, &QAction::triggered, this, &ArticleView::copyAsText );

  webview->installEventFilter( this );
  searchPanel->installEventFilter( this );
  ftsSearchPanel->installEventFilter( this );

  expandOptionalParts = cfg.preferences.alwaysExpandOptionalParts;
#ifndef Q_OS_MACOS
  webview->grabGesture( Gestures::GDPinchGestureType );
  webview->grabGesture( Gestures::GDSwipeGestureType );
#endif

  connect( GlobalBroadcaster::instance(), &GlobalBroadcaster::dictionaryChanges, this, &ArticleView::setActiveDictIds );
  connect( GlobalBroadcaster::instance(), &GlobalBroadcaster::dictionaryClear, this, &ArticleView::dictionaryClear );
}

QSize ArticleView::minimumSizeHint() const
{
  return searchPanel->minimumSizeHint();
}

void ArticleView::setCurrentGroupId( unsigned currentGrgId )
{
  currentGroupId = currentGrgId;
}
unsigned ArticleView::getCurrentGroupId()
{
  return currentGroupId;
}

void ArticleView::setAudioLink( QString audioLink )
{
  if ( audioLink_ != audioLink ) {
    audioLink_ = audioLink;
    lastAudioData.clear();
    lastAudioUrl.clear();
  }
}

QString ArticleView::getAudioLink() const
{
  return audioLink_;
}

ArticleView::~ArticleView()
{
  cleanupTemp();
  audioPlayer->stop();
#ifndef Q_OS_MACOS
  webview->ungrabGesture( Gestures::GDPinchGestureType );
  webview->ungrabGesture( Gestures::GDSwipeGestureType );
#endif
}

void ArticleView::showDefinition( const QString & word,
                                  unsigned group,
                                  const QString & scrollTo,
                                  const Contexts & contexts_ )
{
  GlobalBroadcaster::instance()->pronounce_engine.reset();
  audioLink_.clear();
  lastAudioData.clear();
  lastAudioUrl.clear();
  currentWord = word.trimmed();
  if ( currentWord.isEmpty() ) {
    return;
  }
  historyMode = false;
  currentActiveDictIds.clear();
  audioPlayer->stop();
  audioLink_.clear();

  QUrl req;
  QUrlQuery reqQuery;
  Contexts contexts( contexts_ );

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );
  reqQuery.addQueryItem( "word", word );
  reqQuery.addQueryItem( "group", QString::number( group ) );
  if ( cfg.preferences.ignoreDiacritics ) {
    reqQuery.addQueryItem( "ignore_diacritics", "1" );
  }

  if ( scrollTo.size() ) {
    reqQuery.addQueryItem( "scrollto", scrollTo );
  }

  if ( delayedHighlightText.size() ) {
    reqQuery.addQueryItem( "regexp", delayedHighlightText );
    delayedHighlightText.clear();
  }

  if ( Contexts::Iterator pos = contexts.find( "gdanchor" ); pos != contexts.end() ) {
    reqQuery.addQueryItem( "gdanchor", contexts[ "gdanchor" ] );
    contexts.erase( pos );
  }

  if ( contexts.size() ) {
    QBuffer buf;
    buf.open( QIODevice::WriteOnly );
    QDataStream stream( &buf );
    stream << contexts;
    buf.close();

    reqQuery.addQueryItem( "contexts", QString::fromLatin1( buf.buffer().toBase64() ) );
  }


  req.setQuery( reqQuery );

  closeSearch();

  qDebug() << "req url:" << req.toString();

  webview->setCursor( Qt::WaitCursor );
  load( req );

  emit sendWordToHistory( word );
}

void ArticleView::showDefinition( const QString & word,
                                  const QStringList & dictIDs,
                                  const QString & searchRegExp,
                                  unsigned group,
                                  bool ignoreDiacritics )
{
  GlobalBroadcaster::instance()->pronounce_engine.reset();
  if ( dictIDs.isEmpty() ) {
    return;
  }
  currentWord = word.trimmed();
  if ( currentWord.isEmpty() ) {
    return;
  }
  historyMode = false;
  currentActiveDictIds.clear();
  audioPlayer->stop();
  audioLink_.clear();
  lastAudioData.clear();
  lastAudioUrl.clear();

  QUrl req;
  QUrlQuery reqQuery;

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );

  reqQuery.addQueryItem( "word", word );
  reqQuery.addQueryItem( "dictionaries", dictIDs.join( "," ) );
  reqQuery.addQueryItem( "regexp", searchRegExp );
  reqQuery.addQueryItem( "group", QString::number( group ) );
  if ( ignoreDiacritics ) {
    reqQuery.addQueryItem( "ignore_diacritics", "1" );
  }

  req.setQuery( reqQuery );

  closeSearch();

  webview->setCursor( Qt::WaitCursor );

  load( req );

  emit sendWordToHistory( word );
}

void ArticleView::showDefinition( const QString & word,
                                  const QStringList & dictIDs,
                                  unsigned group,
                                  bool ignoreDiacritics )
{
  showDefinition( word, dictIDs, {}, group, ignoreDiacritics );
}

void ArticleView::loadFinished( bool result )
{
  setZoomFactor( cfg.preferences.zoomFactor );
  webview->unsetCursor();
  if ( !result ) {
    qWarning() << "article loaded unsuccessful:" << webview->url().toString();
    return;
  }
  QUrl url = webview->url();
  if ( url.url() == "about:blank" ) {
    return;
  }
  const QString urlString = url.url();
  qDebug() << "article view loaded url:" << ( urlString.length() > 50 ? urlString.left( 50 ) + "..." : urlString )
           << result;

  {
    if ( cfg.preferences.autoScrollToTargetArticle ) {
      const QString scrollTo = Utils::Url::queryItemValue( url, "scrollto" );
      if ( isScrollTo( scrollTo ) ) {
        setCurrentArticle( scrollTo, true );
      }
      else {
        setActiveArticleId( "" );
      }
    }
    else {
      setActiveArticleId( "" );
    }

    if ( !Utils::Url::queryItemValue( url, "gdanchor" ).isEmpty() ) {
      const QString anchor = QUrl::fromPercentEncoding( Utils::Url::encodedQueryItemValue( url, "gdanchor" ) );
      webview->scrollToAnchor( anchor );
    }

    if ( Utils::Url::hasQueryItem( webview->url(), "regexp" ) ) {
      highlightFTSResults();
    }
  }

  if ( result ) {
    emit pageLoaded( this );
  }
}

void ArticleView::handleTitleChanged( const QString & title )
{
  QString displayTitle = customTitle.isEmpty() ? title : customTitle;

  if ( !displayTitle.isEmpty() && !displayTitle.contains( "://" ) ) {
    emit titleChanged( this, displayTitle );
  }
}

unsigned ArticleView::getGroup( const QUrl & url )
{
  if ( url.scheme() == "gdlookup" && Utils::Url::hasQueryItem( url, "group" ) ) {
    return Utils::Url::queryItemValue( url, "group" ).toUInt();
  }

  return 0;
}

QStringList ArticleView::getArticlesList()
{
  return currentActiveDictIds;
}

QString ArticleView::getActiveArticleId()
{
  return activeDictId;
}

void ArticleView::setActiveArticleId( const QString & dictId )
{
  this->activeDictId = dictId;
}

QString ArticleView::getCurrentArticle()
{
  const QString dictId = getActiveArticleId();
  return scrollToFromDictionaryId( dictId );
}

void ArticleView::jumpToDictionary( const QString & id, bool force )
{
  if ( const QString targetArticle = scrollToFromDictionaryId( id ); force && targetArticle != getCurrentArticle() ) {
    setCurrentArticle( targetArticle, true );
  }
}

bool ArticleView::setCurrentArticle( const QString & id, bool moveToIt )
{
  if ( !isScrollTo( id ) ) {
    return false;
  }

  if ( !webview->isVisible() ) {
    return false;
  }

  if ( moveToIt ) {
    const QString dictId = id.mid( 7 );
    if ( dictId.isEmpty() ) {
      return false;
    }
    if ( !webview->scrollToAnchor( id ) ) {
      pendingScrollTo = id;
      return false;
    }
    setActiveArticleId( dictId );
  }
  return true;
}

void ArticleView::selectCurrentArticle()
{
  const QString id = getCurrentArticle();
  if ( !webview->selectAnchorRange( id ) )
    qWarning() << "lite: could not select article" << id;
}


void ArticleView::load( const QUrl & url, const QString & customTitle )
{
  this->customTitle = customTitle;
  webview->load( url );
}

void ArticleView::cleanupTemp()
{
  auto it = desktopOpenedTempFiles.begin();
  while ( it != desktopOpenedTempFiles.end() ) {
    if ( QFile::remove( *it ) ) {
      it = desktopOpenedTempFiles.erase( it );
    }
    else {
      ++it;
    }
  }

  it = audioTempFiles.begin();
  while ( it != audioTempFiles.end() ) {
    if ( QFile::remove( *it ) ) {
      it = audioTempFiles.erase( it );
    }
    else {
      ++it;
    }
  }
}

bool ArticleView::isDarkModeEnabled() const
{
  return GlobalBroadcaster::instance()->isDarkReaderModeEnabled();
}

bool ArticleView::handleF3( QObject *, QEvent * ev )
{
  if ( ev->type() == QEvent::ShortcutOverride || ev->type() == QEvent::KeyPress ) {
    QKeyEvent * ke = static_cast< QKeyEvent * >( ev );
    if ( ke->key() == Qt::Key_F3 && isSearchOpened() ) {
      if ( !ke->modifiers() ) {
        if ( ev->type() == QEvent::KeyPress ) {
          on_searchNext_clicked();
        }

        ev->accept();
        return true;
      }

      if ( ke->modifiers() == Qt::ShiftModifier ) {
        if ( ev->type() == QEvent::KeyPress ) {
          on_searchPrevious_clicked();
        }

        ev->accept();
        return true;
      }
    }
    if ( ke->key() == Qt::Key_F3 && ftsSearchPanel->isVisible() ) {
      if ( !ke->modifiers() ) {
        if ( ev->type() == QEvent::KeyPress ) {
          on_ftsSearchNext_clicked();
        }

        ev->accept();
        return true;
      }

      if ( ke->modifiers() == Qt::ShiftModifier ) {
        if ( ev->type() == QEvent::KeyPress ) {
          on_ftsSearchPrevious_clicked();
        }

        ev->accept();
        return true;
      }
    }
  }

  return false;
}

bool ArticleView::eventFilter( QObject * obj, QEvent * ev )
{
#ifdef Q_OS_MAC

  if ( ev->type() == QEvent::NativeGesture ) {
    qDebug() << "it's a Native Gesture!";
  }

#else
  if ( ev->type() == QEvent::Gesture ) {
    Gestures::GestureResult result;
    QPoint pt;

    bool handled = Gestures::handleGestureEvent( obj, ev, result, pt );

    if ( handled ) {
      if ( result == Gestures::ZOOM_IN )
        emit zoomIn();
      else if ( result == Gestures::ZOOM_OUT )
        emit zoomOut();
      else if ( result == Gestures::SWIPE_LEFT )
        back();
      else if ( result == Gestures::SWIPE_RIGHT )
        forward();
      else if ( result == Gestures::SWIPE_UP || result == Gestures::SWIPE_DOWN ) {
        int delta        = result == Gestures::SWIPE_UP ? -120 : 120;
        QWidget * widget = static_cast< QWidget * >( obj );

        QPoint angleDelta( 0, delta );
        QPoint pixelDetal;
        QWidget * child = widget->childAt( widget->mapFromGlobal( pt ) );
        if ( child ) {
          QWheelEvent whev( child->mapFromGlobal( pt ),
                            pt,
                            pixelDetal,
                            angleDelta,
                            Qt::NoButton,
                            Qt::NoModifier,
                            Qt::NoScrollPhase,
                            false );
          qApp->sendEvent( child, &whev );
        }
        else {
          QWheelEvent whev( widget->mapFromGlobal( pt ),
                            pt,
                            pixelDetal,
                            angleDelta,
                            Qt::NoButton,
                            Qt::NoModifier,
                            Qt::NoScrollPhase,
                            false );
          qApp->sendEvent( widget, &whev );
        }
      }
    }

    return handled;
  }

  if ( ev->type() == QEvent::MouseMove ) {
    if ( Gestures::isFewTouchPointsPresented() ) {
      ev->accept();
      return true;
    }
  }
#endif

  if ( handleF3( obj, ev ) ) {
    return true;
  }

  if ( obj == webview ) {

    if ( ev->type() == QEvent::KeyPress ) {
      auto keyEvent = static_cast< QKeyEvent * >( ev );

      if ( keyEvent->modifiers() & ( Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier ) ) {
        return false;
      }

      if ( Utils::ignoreKeyEvent( keyEvent ) || keyEvent->key() == Qt::Key_Return
           || keyEvent->key() == Qt::Key_Enter ) {
        return false;
      }

      QString text = keyEvent->text();

      if ( text.size() ) {
        emit typingEvent( text );
        return true;
      }
    }
    else if ( ev->type() == QEvent::Wheel ) {
      QWheelEvent * pe = static_cast< QWheelEvent * >( ev );
      if ( pe->modifiers().testFlag( Qt::ControlModifier ) ) {
        if ( pe->angleDelta().y() > 0 ) {
          zoomIn();
        }
        else {
          zoomOut();
        }
      }
    }
  }
  else {
    return QWidget::eventFilter( obj, ev );
  }

  return false;
}

void ArticleView::linkHovered( const QString & link )
{
  QString msg;
  QUrl url( link );

  if ( url.scheme() == "bres" ) {
    msg = tr( "Resource" );
  }
  else if ( url.scheme() == "gdtts" ) {
    msg = tr( "TTS Voice" );
  }
  else if ( url.scheme() == "gdau" || Utils::Url::isAudioUrl( url ) ) {
    msg = tr( "Audio" );
  }

  else if ( url.scheme() == "gdvideo" ) {
    if ( url.path().isEmpty() ) {
      msg = tr( "Video" );
    }
    else {
      QString path = url.path();
      if ( path.startsWith( '/' ) ) {
        path = path.mid( 1 );
      }
      msg = tr( "Video: %1" ).arg( path );
    }
  }
  else if ( url.scheme() == "gdlookup" || url.scheme().compare( "bword" ) == 0 ) {
    QString def = url.path();
    if ( def.startsWith( "/" ) ) {
      def = def.mid( 1 );
    }

    if ( Utils::Url::hasQueryItem( url, "dict" ) ) {
      QString dictName( Utils::Url::queryItemValue( url, "dict" ) );
      if ( !dictName.isEmpty() ) {
        msg = tr( "Definition from dictionary \"%1\": %2" ).arg( dictName, def );
      }
    }

    if ( msg.isEmpty() ) {
      if ( def.isEmpty() && url.hasFragment() ) {
        msg = '#' + url.fragment();
      }
      else {
        msg = tr( "Definition: %1" ).arg( def );
      }
    }
  }
  else {
    msg = link;
  }

  emit statusBarMessage( msg );
}

void ArticleView::linkClicked( const QUrl & url_ )
{
  Qt::KeyboardModifiers kmod = QApplication::keyboardModifiers();

  if ( kmod & Qt::AltModifier ) {
    return;
  }

  QUrl url( url_ );
  Contexts contexts;

  if ( ( webview->isMidButtonPressed() || ( kmod & ( Qt::ControlModifier | Qt::ShiftModifier ) ) )
       && !isAudioLink( url ) ) {
    webview->resetMidButtonPressed();
    emit openLinkInNewTab( url, webview->url(), getCurrentArticle(), contexts );
  }
  else {
    openLink( url, webview->url(), getCurrentArticle(), contexts );
  }
}

void ArticleView::openLink( const QUrl & url, const QUrl & ref, const QString & scrollTo, const Contexts & contexts_ )
{
  audioPlayer->stop();
  qDebug() << "open link url:" << url;

  auto [ valid, word ] = Utils::Url::getQueryWord( url );
  if ( valid && word.isEmpty() ) {
    return;
  }

  Contexts contexts( contexts_ );

  if ( url.scheme().compare( "bword" ) == 0 || url.scheme().compare( "entry" ) == 0 ) {
    if ( Utils::Url::hasQueryItem( ref, "dictionaries" ) ) {
      QStringList dictsList = Utils::Url::queryItemValue( ref, "dictionaries" ).split( ",", Qt::SkipEmptyParts );

      showDefinition( word, dictsList, getGroup( ref ), false );
    }
    else {
      showDefinition( word, getGroup( ref ), scrollTo, contexts );
    }
  }
  else if ( url.scheme() == "gdlookup" ) {
    if ( url.hasFragment() ) {
      webview->scrollToAnchor( url.fragment() );
    }
    else {
      if ( Utils::Url::hasQueryItem( url, "dictionaries" ) ) {
        QStringList dictsList = Utils::Url::queryItemValue( url, "dictionaries" ).split( ",", Qt::SkipEmptyParts );

        showDefinition( word, dictsList, getGroup( url ), false );
        return;
      }

      QString newScrollTo( scrollTo );
      if ( Utils::Url::hasQueryItem( url, "dict" ) ) {
        QString dictName( Utils::Url::queryItemValue( url, "dict" ) );
        auto dict = dictionaryGroup->getDictionaryByName( dictName );
        if ( dict ) {
          newScrollTo = scrollToFromDictionaryId( QString::fromUtf8( dict->getId().c_str() ) );
        }
      }

      if ( Utils::Url::hasQueryItem( url, "gdanchor" ) ) {
        contexts[ "gdanchor" ] = Utils::Url::queryItemValue( url, "gdanchor" );
      }

      showDefinition( word, getGroup( ref ), newScrollTo, contexts );
    }
  }
  else if ( url.scheme() == "bres" || url.scheme() == "gdau" || url.scheme() == "gdvideo"
            || Utils::Url::isAudioUrl( url ) ) {
    playAudio( url );
  }
  else if ( Utils::isExternalLink( url ) ) {
    QDesktopServices::openUrl( url );
  }
}

void ArticleView::playAudio( const QUrl & url )
{
  audioPlayer->stop();
  qDebug() << "play audio [url]:" << url;

  if ( url.scheme() == "bres" || url.scheme() == "gdau" || url.scheme() == "gdvideo"
       || Utils::Url::isAudioUrl( url ) ) {

    if ( url.scheme() == "gdau" ) {

      sptr< Dictionary::Class > dict = dictionaryGroup->getDictionaryById( url.host().toStdString() );

      if ( dict ) {
        try {
          QString requestUrlPath = url.path();
          if ( url.hasFragment() ) {
            requestUrlPath = url.path() + "#" + url.fragment();
          }
          sptr< Dictionary::DataRequest > req = dict->getResource( requestUrlPath.mid( 1 ).toUtf8().data() );

          if ( !req->isFinished() ) {
            connect( req.get(), &Dictionary::Request::finished, this, [ req, this ]() {
              audioDownloadFinished( req );
            } );
          }
          else {
            audioDownloadFinished( req );
          }
        }
        catch ( std::exception & e ) {
          emit statusBarMessage( tr( "ERROR: %1" ).arg( e.what() ), 10000, QPixmap( ":/icons/error.svg" ) );
        }
      }
    }
    else if ( url.scheme() == "gdprg" ) {
      QString id( url.host() );

      for ( const auto & program : cfg.programs ) {
        if ( program.id == id ) {
          Programs::RunInstance * req = new Programs::RunInstance;

          connect( req, &Programs::RunInstance::finished, req, &QObject::deleteLater );

          QString error;

          if ( !req->start( program, url.path().mid( 1 ), GlobalBroadcaster::instance()->translateLineText, error ) ) {
            delete req;

            QMessageBox::critical( this, "MicroDict", error );
          }

          return;
        }
      }

      QMessageBox::critical( this, "MicroDict", tr( "The referenced audio program doesn't exist." ) );
    }
  }
}

ResourceToSaveHandler * ArticleView::saveResource( const QUrl & url, const QString & fileName )
{
  ResourceToSaveHandler * handler = new ResourceToSaveHandler( this, fileName );
  sptr< Dictionary::DataRequest > req;

  if ( url.scheme() == "bres" || url.scheme() == "gico" || url.scheme() == "gdau" || url.scheme() == "gdvideo" ) {
    QString contentType;
    req = articleResMgr.getResource( url, contentType );

    if ( req.get() ) {
      handler->addRequest( req );
    }
  }
  else {
    QString contentType;
    req = articleResMgr.getResource( url, contentType );

    if ( req.get() ) {
      handler->addRequest( req );
    }
  }

  if ( handler->isEmpty() ) {
    emit statusBarMessage( tr( "ERROR: %1" ).arg( tr( "The referenced resource doesn't exist." ) ),
                           10000,
                           QPixmap( ":/icons/error.svg" ) );
  }

  handler->downloadFinished();

  return handler;
}

void ArticleView::updateMutedContents()
{
  QUrl currentUrl = webview->url();

  if ( currentUrl.scheme() != "gdlookup" ) {
    return;
  }

  unsigned group = getGroup( currentUrl );

  if ( !group ) {
    return;
  }

  load( currentUrl );
  webview->setCursor( Qt::WaitCursor );
}

bool ArticleView::canGoBack()
{
  return webview->history()->canGoBack();
}

bool ArticleView::canGoForward()
{
  return webview->history()->canGoForward();
}

void ArticleView::setSelectionBySingleClick( bool set )
{
  webview->setSelectionBySingleClick( set );
}

void ArticleView::setDelayedHighlightText( const QString & text )
{
  delayedHighlightText = text;
}

void ArticleView::applyFontPreferences()
{
  webview->applyFontPreferences();
}

void ArticleView::syncBackgroundColorWithCfgDarkReader() const
{
  const SystemAppearance & appearance = SystemAppearance::instance();

  webview->applyColorScheme( appearance.publishesColors() ? appearance.isDark()
                                                          : isDarkPalette( qApp->palette() ) );
}
QString ArticleView::getCurrentWord()
{
  return currentWord;
}

void ArticleView::setCurrentWord( const QString & word )
{
  currentWord = word;
}

void ArticleView::back()
{
  if ( canGoBack() ) {
    currentActiveDictIds.clear();
    historyMode = true;
    GlobalBroadcaster::instance()->pronounce_engine.reset();
    webview->back();
  }
}

void ArticleView::forward()
{
  currentActiveDictIds.clear();
  historyMode = true;
  GlobalBroadcaster::instance()->pronounce_engine.reset();
  webview->forward();
}

void ArticleView::reload()
{
  webview->reload();
}

void ArticleView::applyColorSchemeAndReload()
{
  syncBackgroundColorWithCfgDarkReader();
  reload();
}

void ArticleView::hasSound( const std::function< void( bool ) > & callback )
{
  callback( !audioLink_.isEmpty() );
}

void ArticleView::playSound()
{
  if ( !audioLink_.isEmpty() ) {
    playAudio( QUrl::fromEncoded( audioLink_.toUtf8() ) );
  }
}

void ArticleView::stopSound()
{
  audioPlayer->stop();
}

void ArticleView::toHtml( const std::function< void( QString & ) > & callback )
{
  webview->page()->toHtml( [ = ]( const QString & content ) {
    QString html = content;
    callback( html );
  } );
}

ArticleWebPage * ArticleView::page()
{
  return webview->page();
}

void ArticleView::setHtml( const QString & content, const QUrl & baseUrl )
{
  webview->page()->setHtml( content, baseUrl );
}

void ArticleView::setContent( const QByteArray & data, const QString & mimeType, const QUrl & baseUrl )
{
  webview->page()->setContent( data, mimeType, baseUrl );
}

QString ArticleView::getTitle()
{
  if ( !customTitle.isEmpty() ) {
    return customTitle;
  }

  auto title = webview->title();
  if ( title.contains( "://" ) ) {
    return {};
  }
  return webview->title();
}

QString ArticleView::getWord() const
{
  const QString t = webview->title();
  if ( !t.isEmpty() )
    return t;
  return currentWord;
}


void ArticleView::contextMenuRequested( const QPoint & pos )
{
  ArticleWebPage * r = webview->page();
  QMenu menu( this );

  QAction * followLink                = nullptr;
  QAction * followLinkExternal        = nullptr;
  QAction * followLinkNewTab          = nullptr;
  QAction * lookupSelection           = nullptr;
  QAction * lookupSelectionGr         = nullptr;
  QAction * lookupSelectionNewTab     = nullptr;
  QAction * lookupSelectionNewTabGr   = nullptr;
  QAction * maxDictionaryRefsAction   = nullptr;
  QAction * addWordToHistoryAction    = nullptr;
  QAction * addHeaderToHistoryAction  = nullptr;
  QAction * sendWordToInputLineAction = nullptr;
  QAction * saveImageAction           = nullptr;
  QAction * openImageAction           = nullptr;
  QAction * saveSoundAction           = nullptr;
  QAction * copySoundAction           = nullptr;
  QAction * saveBookmark              = nullptr;

  QUrl targetUrl = webview->linkAt( pos );
  const QUrl imageUrl = webview->imageAt( pos );
  Contexts contexts;

  if ( !targetUrl.isEmpty() ) {
    if ( !Utils::isExternalLink( targetUrl ) ) {
      followLink = new QAction( tr( "Op&en Link" ), &menu );
      menu.addAction( followLink );

      if ( !isAudioLink( targetUrl ) ) {
        followLinkNewTab = new QAction( QIcon( ":/icons/addtab.svg" ), tr( "Open Link in New &Tab" ), &menu );
        menu.addAction( followLinkNewTab );
      }
    }

    if ( Utils::isExternalLink( targetUrl ) ) {
      followLinkExternal = new QAction( tr( "Open Link in &External Browser" ), &menu );
      menu.addAction( followLinkExternal );
      QAction * copyLinkAction = new QAction( tr( "&Copy Link Location" ), &menu );
      menu.addAction( copyLinkAction );
      connect( copyLinkAction, &QAction::triggered, this, [ targetUrl ]() {
        QApplication::clipboard()->setText( targetUrl.toString() );
      } );
    }
  }

  if ( !imageUrl.isEmpty() ) {
    QAction * copyImageAction = new QAction( tr( "&Copy Image" ), &menu );
    menu.addAction( copyImageAction );
    connect( copyImageAction, &QAction::triggered, this, [ this ]() {
      webview->copy();
    } );
    saveImageAction = new QAction( tr( "Save &image..." ), &menu );
    menu.addAction( saveImageAction );

    openImageAction = new QAction( tr( "Open image in system viewer..." ), &menu );
    menu.addAction( openImageAction );
  }

  if ( isAudioLink( targetUrl ) ) {
    saveSoundAction = new QAction( tr( "Save s&ound..." ), &menu );
    menu.addAction( saveSoundAction );

    copySoundAction = new QAction( tr( "&Copy sound to clipboard" ), &menu );
    menu.addAction( copySoundAction );
  }

  const QString selectedText = webview->selectedText();
  QString text               = Utils::trimNonChar( selectedText );

  if ( text.size() && text.size() < 60 ) {

    lookupSelection = new QAction( tr( "&Look up \"%1\"" ).arg( text ), &menu );
    menu.addAction( lookupSelection );

    lookupSelectionNewTab =
      new QAction( QIcon( ":/icons/addtab.svg" ), tr( "Look up \"%1\" in &New Tab" ).arg( text ), &menu );
    menu.addAction( lookupSelectionNewTab );

    sendWordToInputLineAction = new QAction( tr( "Send \"%1\" to input line" ).arg( text ), &menu );
    menu.addAction( sendWordToInputLineAction );

    addWordToHistoryAction = new QAction( tr( "&Add \"%1\" to history" ).arg( text ), &menu );
    menu.addAction( addWordToHistoryAction );

    const Instances::Group * altGroup =
      ( currentGroupId != getGroup( webview->url() ) ) ? dictionaryGroup->getGroupById( currentGroupId ) : nullptr;

    if ( altGroup ) {
      QIcon icon = altGroup->icon.size() ? QIcon( ":/flags/" + altGroup->icon ) : QIcon();

      lookupSelectionGr = new QAction( icon, tr( "Look up \"%1\" in %2" ).arg( text ).arg( altGroup->name ), &menu );
      menu.addAction( lookupSelectionGr );

      lookupSelectionNewTabGr = new QAction( QIcon( ":/icons/addtab.svg" ),
                                             tr( "Look up \"%1\" in %2 in &New Tab" ).arg( text ).arg( altGroup->name ),
                                             &menu );
      menu.addAction( lookupSelectionNewTabGr );
    }
  }

  if ( text.size() ) {
    saveBookmark = new QAction( tr( "Save &Bookmark \"%1...\"" ).arg( text.left( 30 ) ), &menu );
    menu.addAction( saveBookmark );
  }

  if ( text.isEmpty() && !cfg.preferences.storeHistory ) {
    QString txt = webview->title();
    if ( txt.size() > 60 ) {
      txt = txt.left( 60 ) + "...";
    }

    addHeaderToHistoryAction = new QAction( tr( R"(&Add "%1" to history)" ).arg( txt ), &menu );
    menu.addAction( addHeaderToHistoryAction );
  }

  if ( selectedText.size() ) {
    menu.addAction( new QAction( tr( "&Copy" ), &menu ) );
    connect( menu.actions().back(), &QAction::triggered, webview, [ this ]() {
      webview->copy();
    } );
    menu.addAction( &copyAsTextAction );
  }
  else {
    menu.addAction( &selectCurrentArticleAction );
    menu.addAction( new QAction( tr( "Select &All" ), &menu ) );
    connect( menu.actions().back(), &QAction::triggered, webview, [ this ]() {
      webview->selectAll();
    } );
  }

  map< QAction *, QString > tableOfContents;

  QStringList ids = getArticlesList();

  if ( !menu.isEmpty() && !ids.empty() ) {
    menu.addSeparator();
  }

  unsigned refsAdded            = 0;
  bool maxDictionaryRefsReached = false;

  if ( cfg.preferences.maxDictionaryRefsInContextMenu > 0 ) {
    for ( QStringList::const_iterator i = ids.constBegin(); i != ids.constEnd(); ++i, ++refsAdded ) {

      auto dictionary = dictionaryGroup->getDictionaryById( i->toUtf8().data() );
      if ( dictionary ) {
        QAction * action = nullptr;
        if ( refsAdded == cfg.preferences.maxDictionaryRefsInContextMenu ) {
          maxDictionaryRefsAction  = new QAction( ".........", &menu );
          action                   = maxDictionaryRefsAction;
          maxDictionaryRefsReached = true;
        }
        else {
          action = new QAction( dictionary->getIcon(), QString::fromUtf8( dictionary->getName().c_str() ), &menu );
          action->setIconVisibleInMenu( true );
        }
        menu.addAction( action );

        tableOfContents[ action ] = *i;
      }

      if ( maxDictionaryRefsReached ) {
        break;
      }
    }
  }


  if ( !menu.isEmpty() ) {
    connect( this, &ArticleView::closePopupMenu, &menu, &QWidget::close );
    QAction * result = menu.exec( webview->mapToGlobal( pos ) );

    if ( !result ) {
      return;
    }

    if ( result == followLink ) {
      openLink( targetUrl, webview->url(), getCurrentArticle(), contexts );
    }
    else if ( result == followLinkExternal ) {
      QDesktopServices::openUrl( targetUrl );
    }
    else if ( result == lookupSelection ) {
      showDefinition( text, getGroup( webview->url() ), getCurrentArticle() );
    }
    else if ( result == saveBookmark ) {
      emit saveBookmarkSignal( text.left( 60 ) );
    }
    else if ( result == lookupSelectionGr && currentGroupId ) {
      showDefinition( selectedText, currentGroupId, QString() );
    }
    else if ( result == addWordToHistoryAction ) {
      emit forceAddWordToHistory( selectedText );
    }
    else if ( result == addHeaderToHistoryAction ) {
      emit forceAddWordToHistory( webview->title() );
    }
    else if ( result == sendWordToInputLineAction ) {
      emit sendWordToInputLine( selectedText );
    }
    else if ( result == followLinkNewTab ) {
      emit openLinkInNewTab( targetUrl, webview->url(), getCurrentArticle(), contexts );
    }
    else if ( result == lookupSelectionNewTab ) {
      emit showDefinitionInNewTab( selectedText, getGroup( webview->url() ), getCurrentArticle(), Contexts() );
    }
    else if ( result == lookupSelectionNewTabGr && currentGroupId ) {
      emit showDefinitionInNewTab( selectedText, currentGroupId, QString(), Contexts() );
    }
    else if ( result == copySoundAction ) {
      QUrl url = ( result == saveImageAction ) ? imageUrl : targetUrl;

      copyResourceToClipboard( url );
    }
    else if ( result == saveImageAction || result == saveSoundAction ) {
      QUrl url = ( result == saveImageAction ) ? imageUrl : targetUrl;
      QString savePath;
      QString fileName;

      if ( cfg.resourceSavePath.isEmpty() ) {
        savePath = QDir::homePath();
      }
      else {
        savePath = QDir::fromNativeSeparators( cfg.resourceSavePath );
        if ( !QDir( savePath ).exists() ) {
          savePath = QDir::homePath();
        }
      }

      QString name = Utils::Url::path( url ).section( '/', -1 );

      if ( result == saveSoundAction ) {

        fileName = savePath + "/" + name;
        fileName = QFileDialog::getSaveFileName(
          parentWidget(),
          tr( "Save sound" ),
          fileName,
          tr(
            "Sound files (*.wav *.opus *.ogg *.oga *.mp3 *.mp4 *.aac *.flac *.mid *.wv *.ape *.spx);;All files (*.*)" ) );
      }
      else {

        if ( name[ 0 ] == '\x1E' ) {
          name.remove( 0, 1 );
        }
        if ( name.length() && name[ name.length() - 1 ] == '\x1F' ) {
          name.chop( 1 );
        }

        fileName = savePath + "/" + name;
        fileName = QFileDialog::getSaveFileName( parentWidget(),
                                                 tr( "Save image" ),
                                                 fileName,
                                                 tr( "Image files (*.bmp *.jpg *.png *.tif);;All files (*.*)" ) );
      }

      if ( !fileName.isEmpty() ) {
        QFileInfo fileInfo( fileName );
        emit storeResourceSavePath( QDir::toNativeSeparators( fileInfo.absoluteDir().absolutePath() ) );
        saveResource( url, fileName );
      }
    }
    else if ( result == openImageAction ) {
      QUrl url = imageUrl;
      QString fileName;


      QString name = Utils::Url::path( url ).section( '/', -1 );

      if ( name[ 0 ] == '\x1E' ) {
        name.remove( 0, 1 );
      }
      if ( name.length() && name[ name.length() - 1 ] == '\x1F' ) {
        name.chop( 1 );
      }

      fileName = QDir::temp().filePath( QString::number( QRandomGenerator::global()->generate() ) + name );

      if ( !fileName.isEmpty() ) {
        QFileInfo fileInfo( fileName );
        auto handler = saveResource( url, fileName );

        if ( !handler->isEmpty() ) {
          connect( handler, &ResourceToSaveHandler::done, this, [ fileName ]() {
            QDesktopServices::openUrl( QUrl::fromLocalFile( fileName ) );
          } );
        }
        else {
          QDesktopServices::openUrl( QUrl::fromLocalFile( fileName ) );
        }
      }
    }
    else {
      if ( result == maxDictionaryRefsAction ) {
        emit showDictsPane();
      }

      QString id = tableOfContents[ result ];

      if ( id.size() ) {
        setCurrentArticle( scrollToFromDictionaryId( id ), true );
      }
    }
  }

  qDebug() << "title = " << r->title();
}

void ArticleView::resourceDownloadFinished( const sptr< Dictionary::DataRequest > & req,
                                            const QUrl & resourceDownloadUrl )
{
  if ( !req->isFinished() ) {
    return;
  }
  if ( req->dataSize() >= 0 ) {
    const vector< char > & data = req->getFullData();

    if ( resourceDownloadUrl.scheme() == "gdau" ) {
      lastAudioData = QByteArray( data.data(), data.size() );
      lastAudioUrl  = resourceDownloadUrl.toString();

      audioPlayer->stop();
      connect( audioPlayer.get(),
               &AudioPlayerInterface::error,
               this,
               &ArticleView::audioPlayerError,
               Qt::UniqueConnection );
      QString errorMessage = audioPlayer->play( data.data(), data.size() );
      if ( !errorMessage.isEmpty() ) {
        QMessageBox::critical( this, "MicroDict", tr( "Failed to play sound file: %1" ).arg( errorMessage ) );
      }
    }
    else {
      QString fileName;

      QTemporaryFile tmp( QDir::temp().filePath( "XXXXXX-" + resourceDownloadUrl.path().section( '/', -1 ) ), this );

      if ( !tmp.open() || (size_t)tmp.write( &data.front(), data.size() ) != data.size() ) {
        QMessageBox::critical( this, "MicroDict", tr( "Failed to create temporary file." ) );
        return;
      }

      tmp.setAutoRemove( false );

      desktopOpenedTempFiles.insert( fileName = tmp.fileName() );

      if ( !QDesktopServices::openUrl( QUrl::fromLocalFile( fileName ) ) ) {
        QMessageBox::critical( this,
                               "MicroDict",
                               tr( "Failed to auto-open resource file, try opening manually: %1." ).arg( fileName ) );
      }
    }
    return;
  }
}


void ArticleView::audioDownloadFinished( const sptr< Dictionary::DataRequest > & req )
{
  if ( req->dataSize() >= 0 ) {
    qDebug() << "audio download finished. Playing...";
    const vector< char > & data = req->getFullData();
    lastAudioData               = QByteArray( data.data(), data.size() );
    lastAudioUrl                = "internal";

    audioPlayer->stop();
    connect( audioPlayer.get(),
             &AudioPlayerInterface::error,
             this,
             &ArticleView::audioPlayerError,
             Qt::UniqueConnection );
    QString errorMessage = audioPlayer->play( data.data(), data.size() );
    if ( !errorMessage.isEmpty() ) {
      QMessageBox::critical( this, "MicroDict", tr( "Failed to play sound file: %1" ).arg( errorMessage ) );
    }
  }
}

void ArticleView::audioPlayerError( const QString & message )
{
  emit statusBarMessage( tr( "WARNING: Audio Player: %1" ).arg( message ), 10000, QPixmap( ":/icons/error.svg" ) );
}

void ArticleView::pasteTriggered()
{
  QString word = cfg.preferences.sanitizeInputPhrase( QApplication::clipboard()->text() );

  if ( !word.isEmpty() ) {
    unsigned groupId = getGroup( webview->url() );

    if ( groupId == GroupId::NoGroupId ) {
      groupId = currentGroupId;
    }
    showDefinition( word, groupId, getCurrentArticle() );
  }
}

unsigned ArticleView::getCurrentGroup()
{
  return currentGroupId;
}

void ArticleView::moveOneArticleUp()
{
  QString current = getCurrentArticle();

  if ( current.size() ) {
    QStringList lst = getArticlesList();

    int idx = lst.indexOf( dictionaryIdFromScrollTo( current ) );

    if ( idx != -1 ) {
      --idx;

      if ( idx < 0 ) {
        idx = lst.size() - 1;
      }

      setCurrentArticle( scrollToFromDictionaryId( lst[ idx ] ), true );
    }
  }
}

void ArticleView::moveOneArticleDown()
{
  QString currentDictId = getActiveArticleId();
  QStringList lst       = getArticlesList();
  if ( currentDictId.isEmpty() && !lst.isEmpty() ) {
    currentDictId = lst[ 0 ];
  }

  int idx = lst.indexOf( currentDictId );

  if ( idx != -1 ) {
    idx = ( idx + 1 ) % lst.size();

    setCurrentArticle( scrollToFromDictionaryId( lst[ idx ] ), true );
  }
}

void ArticleView::openSearch()
{
  if ( !isVisible() ) {
    return;
  }

  if ( ftsSearchPanel->isVisible() ) {
    closeSearch();
  }

  if ( !searchPanel->isVisible() ) {
    searchPanel->show();
    searchPanel->lineEdit->setText( getTitle() );
  }

  searchPanel->lineEdit->setFocus();
  searchPanel->lineEdit->selectAll();

  if ( webview->hasSelection() ) {
    webview->clearSearchHighlight();
  }

  Utils::Widget::setNoResultColor( searchPanel->lineEdit, false );
}

void ArticleView::on_searchPrevious_clicked()
{
  if ( searchPanel->isVisible() ) {
    performFindOperation( true );
  }
}

void ArticleView::on_searchNext_clicked()
{
  if ( searchPanel->isVisible() ) {
    performFindOperation( false );
  }
}

void ArticleView::on_searchText_textEdited()
{
  performFindOperation( false );
}

void ArticleView::on_searchText_returnPressed()
{
  on_searchNext_clicked();
}

void ArticleView::on_searchCloseButton_clicked()
{
  closeSearch();
}

void ArticleView::on_searchCaseSensitive_clicked( bool checked )
{
  webview->clearSearchHighlight();
  performFindOperation( false );
}

void ArticleView::doubleClicked( QPoint pos )
{
  audioPlayer->stop();
  if ( cfg.preferences.doubleClickTranslates ) {
    QString selectedText = webview->selectedText();

    if ( selectedText.isEmpty() ) {
      return;
    }

    emit sendWordToInputLine( selectedText );

    if ( !Folding::applyWhitespaceOnly( selectedText ).isEmpty() && selectedText.size() < 60 ) {
      emit translateSelectedText( selectedText, webview->url(), getCurrentArticle() );
    }
  }
}


void ArticleView::performFindOperation( bool backwards )
{
  QString text = searchPanel->lineEdit->text();

  QTextDocument::FindFlags f;

  if ( searchPanel->caseSensitive->isChecked() ) {
    f |= QTextDocument::FindCaseSensitively;
  }

  if ( backwards ) {
    f |= QTextDocument::FindBackward;
  }

  findText( text, f, [ text, this ]( bool match ) {
    bool nomatch = !text.isEmpty() && !match;
    Utils::Widget::setNoResultColor( searchPanel->lineEdit, nomatch );
  } );
}

void ArticleView::findText( QString & text,
                            const QTextDocument::FindFlags & f,
                            const std::function< void( bool match ) > & callback )
{
  const bool match = webview->findText( text,
                                        f.testFlag( QTextDocument::FindCaseSensitively ),
                                        f.testFlag( QTextDocument::FindBackward ) );
  if ( callback )
    callback( match );
}

bool ArticleView::closeSearch()
{
  webview->clearSearchHighlight();
  if ( searchPanel->isVisible() ) {
    searchPanel->hide();
    webview->setFocus();

    return true;
  }
  if ( ftsSearchPanel->isVisible() ) {
    firstAvailableText.clear();
    uniqueMatches.clear();

    ftsSearchPanel->hide();
    webview->setFocus();

    webview->clearSearchHighlight();
    return true;
  }
  return false;
}

bool ArticleView::isSearchOpened()
{
  return searchPanel->isVisible();
}

void ArticleView::copyAsText()
{
  QString text = webview->selectedText();
  if ( !text.isEmpty() ) {
    QApplication::clipboard()->setText( text );
  }
}

void ArticleView::copyResourceToClipboard( const QUrl & url )
{
  QString contentType;
  auto req = articleResMgr.getResource( url, contentType );
  if ( req ) {
    connect( req.get(), &Dictionary::DataRequest::finished, this, [ req, this, url ]() {
      if ( req->dataSize() < 0 ) {
        emit statusBarMessage( tr( "Failed to copy sound" ) );
        return;
      }

      QString path = url.path();
      QString extension;
      QString mimeType = Utils::getAudioMimeType( path, extension );

      const auto & data = req->getFullData();

      QTemporaryFile tempFile;
      tempFile.setFileTemplate( QDir::tempPath() + "/audio_XXXXXX" + extension );
      tempFile.setAutoRemove( false );

      if ( tempFile.open() ) {
        QString tempFileName = tempFile.fileName();
        qint64 bytesWritten  = tempFile.write( data.data(), data.size() );
        tempFile.close();

        if ( bytesWritten != static_cast< qint64 >( data.size() ) ) {
          emit statusBarMessage( tr( "Failed to write complete audio data" ) );
          QFile::remove( tempFileName );
          return;
        }

        auto * mimeData = new QMimeData;
        QByteArray audioData( data.data(), data.size() );

        QList< QUrl > urls;
        urls << QUrl::fromLocalFile( tempFileName );
        mimeData->setUrls( urls );

        mimeData->setData( mimeType, audioData );

        mimeData->setData( "audio/*", audioData );

        if ( extension == ".mp3" ) {
          mimeData->setData( "audio/x-mpeg", audioData );
        }

        QApplication::clipboard()->setMimeData( mimeData );

        audioTempFiles.insert( tempFileName );

        emit statusBarMessage( tr( "Sound copied to clipboard" ), 2000 );
      }
    } );
  }
  else {
    emit statusBarMessage( tr( "Failed to download sound" ) );
  }
}

void ArticleView::highlightFTSResults()
{
  closeSearch();
  webview->clearSearchHighlight();
  QString regString = Utils::Url::queryItemValue( webview->url(), "regexp" );
  if ( regString.isEmpty() ) {
    return;
  }

  QStringList highlightKeywords = RX::Ftx::processSearchStringForHighlight( regString );

  if ( highlightKeywords.isEmpty() ) {
    return;
  }

  QString accuracy = "exactly";
  for ( const QString & keyword : highlightKeywords ) {
    for ( const QChar & ch : keyword ) {
      auto script = ch.script();
      if ( script == QChar::Script_Han || script == QChar::Script_Hiragana || script == QChar::Script_Katakana
           || script == QChar::Script_Hangul || script == QChar::Script_Thai || script == QChar::Script_Lao
           || script == QChar::Script_Khmer || script == QChar::Script_Myanmar ) {
        accuracy = "partially";
        break;
      }
    }
    if ( accuracy == "partially" ) {
      break;
    }
  }

  const int highlighted = webview->highlightAll( highlightKeywords, false );
  if ( highlighted == 0 )
    qDebug() << "lite: no full-text-search matches to highlight";

  firstAvailableText = highlightKeywords.first();

  ftsSearchPanel->show();
  performFtsFindOperation( true );
}

void ArticleView::setActiveDictIds( const ActiveDictIds & ad )
{
  auto groupId = ad.groupId;
  if ( groupId == 0 ) {
    groupId = GroupId::AllGroupId;
  }
  if ( ( ad.word == currentWord && groupId == getCurrentGroup() ) || historyMode ) {
    qDebug() << "receive dicts, current word:" << currentWord << ad.word << ":" << ad.dictIds;
    currentActiveDictIds << ad.dictIds;
    currentActiveDictIds.removeDuplicates();
    emit updateFoundInDictsList();
  }
}

void ArticleView::dictionaryClear( const ActiveDictIds & ad )
{
  auto groupId = ad.groupId;
  if ( groupId == 0 ) {
    groupId = GroupId::AllGroupId;
  }
  if ( ad.word == currentWord && groupId == getCurrentGroup() ) {
    qDebug() << "clear current dictionaries:" << currentWord;
    currentActiveDictIds.clear();
  }
}

void ArticleView::performFtsFindOperation( bool backwards )
{
  if ( !ftsSearchPanel->isVisible() ) {
    return;
  }

  if ( firstAvailableText.isEmpty() ) {
    ftsSearchPanel->statusLabel->setText( searchStatusMessageNoMatches() );
    ftsSearchPanel->next->setEnabled( false );
    ftsSearchPanel->previous->setEnabled( false );
    return;
  }

  const bool found = webview->findText( firstAvailableText, false, backwards );

  if ( !found ) {
    ftsSearchPanel->statusLabel->setText( searchStatusMessageNoMatches() );
    return;
  }

  if ( backwards ) {
    ftsSearchPanel->previous->setEnabled( true );
    if ( !ftsSearchPanel->next->isEnabled() )
      ftsSearchPanel->next->setEnabled( true );
  }
  else {
    ftsSearchPanel->next->setEnabled( true );
    if ( !ftsSearchPanel->previous->isEnabled() )
      ftsSearchPanel->previous->setEnabled( true );
  }

  ftsSearchPanel->statusLabel->setText( searchStatusMessage( 1, uniqueMatches.size() ) );
}

void ArticleView::on_ftsSearchPrevious_clicked()
{
  performFtsFindOperation( true );
}

void ArticleView::on_ftsSearchNext_clicked()
{
  performFtsFindOperation( false );
}
void ArticleView::clearContent()
{
  auto html = articleResMgr.getHtml( ResourceType::BLANK );

  webview->setHtml( QString::fromStdString( html ) );
}

void ArticleView::setCustomTitle( const QString & customTitle )
{
  this->customTitle = customTitle;
  if ( webview->url().isValid() ) {
    emit titleChanged( this, getTitle() );
  }
}

void ArticleView::load( QString url, const QString & customTitle )
{
  this->customTitle = customTitle;

  QUrl qurl( url );
  webview->load( qurl );
}


ResourceToSaveHandler::ResourceToSaveHandler( ArticleView * view, QString fileName ):
  QObject( view ),
  fileName( std::move( fileName ) ),
  alreadyDone( false )
{
  connect( this, &ResourceToSaveHandler::statusBarMessage, view, &ArticleView::statusBarMessage );
}

void ResourceToSaveHandler::addRequest( const sptr< Dictionary::DataRequest > & req )
{
  if ( !alreadyDone ) {
    downloadRequests.push_back( req );

    connect( req.get(), &Dictionary::Request::finished, this, &ResourceToSaveHandler::downloadFinished );
  }
}

void ResourceToSaveHandler::downloadFinished()
{
  if ( downloadRequests.empty() ) {
    return;
  }

  for ( auto i = downloadRequests.begin(); i != downloadRequests.end(); ) {
    if ( ( *i )->isFinished() ) {
      if ( ( *i )->dataSize() >= 0 && !alreadyDone ) {
        QByteArray resourceData;
        const vector< char > & data = ( *i )->getFullData();
        resourceData                = QByteArray( data.data(), data.size() );

        if ( !fileName.isEmpty() ) {
          const QFileInfo fileInfo( fileName );
          QDir().mkpath( fileInfo.absoluteDir().absolutePath() );

          QFile file( fileName );
          if ( file.open( QFile::WriteOnly ) ) {
            file.write( resourceData.data(), resourceData.size() );
            file.close();
          }

          if ( file.error() ) {
            emit statusBarMessage( tr( "ERROR: %1" ).arg( tr( "Resource saving error: " ) + file.errorString() ),
                                   10000,
                                   QPixmap( ":/icons/error.svg" ) );
          }
        }
        alreadyDone = true;

        downloadRequests.clear();
        break;
      }
      else {
        downloadRequests.erase( i++ );
      }
    }
    else {
      break;
    }
  }

  if ( downloadRequests.empty() ) {
    if ( !alreadyDone ) {
      emit statusBarMessage( tr( "WARNING: %1" ).arg( tr( "The referenced resource failed to download." ) ),
                             10000,
                             QPixmap( ":/icons/error.svg" ) );
    }
    emit done();
    deleteLater();
  }
}