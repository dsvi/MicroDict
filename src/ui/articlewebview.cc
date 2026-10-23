/* Licensed under GPLv3 or later, see the LICENSE file */

#include "articlewebview.hh"
#include "lite/lite_fonts.hh"
#include "lite/lite_resources.hh"
#include "articlewebpage.hh"
#include "dict/dictionary.hh"
#include "lite/lite_html.hh"
#include "lite/lite_theme.hh"
#include "utils.hh"

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QPointer>
#include <QTextBlock>
#include <QTextFragment>
#include <QWheelEvent>


using namespace Dictionary;

namespace {

const QColor HIGHLIGHT_COLOR( 255, 235, 120 );


QByteArray dataOf( const sptr< DataRequest > & req )
{
  const auto & data = req->getFullData();
  return QByteArray( data.data(), static_cast< qint64 >( data.size() ) );
}

}

bool LiteWebHistory::canGoBack() const
{
  return items && index && *index > 0;
}

bool LiteWebHistory::canGoForward() const
{
  return items && index && *index >= 0 && *index < items->size() - 1;
}

LiteHistoryItem LiteWebHistory::currentItem() const
{
  if ( items && index && *index >= 0 && *index < items->size() )
    return ( *items )[ *index ];
  return {};
}

ArticleWebView::ArticleWebView( QWidget * parent, ArticleResourceManager & mgr ):
  QTextBrowser( parent ),
  netMgr( mgr ),
  historyProxy_( &history_, &historyIndex_, this )
{
  setReadOnly( true );
  setUndoRedoEnabled( false );
  setOpenLinks( false );
  setOpenExternalLinks( false );
  setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard
                           | Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard );
  setFrameShape( QFrame::NoFrame );
  setLineWrapMode( QTextEdit::WidgetWidth );

  page_ = new ArticleWebPage( this, this );

  connect( this, &QTextBrowser::anchorClicked, this, [ this ]( const QUrl & target ) {
    QUrl url = target;
    if ( url.isRelative() )
      url = currentUrl_.resolved( url );
    emit linkClicked( url );
  } );

  viewport()->installEventFilter( this );
}

void ArticleWebView::setUp( Config::Class * _cfg )
{
  cfg = _cfg;
  setZoomFactor( _cfg->preferences.zoomFactor );
}

void ArticleWebView::load( const QUrl & url )
{
  if ( !url.isValid() ) {
    emit loadFinished( false );
    return;
  }
  fetchAndShow( url );
}

void ArticleWebView::fetchAndShow( const QUrl & url )
{
  QString contentType;
  sptr< DataRequest > req = netMgr.getResource( url, contentType );

  if ( req ) {
    if ( req->isFinished() ) {
      if ( req->dataSize() < 0 ) {
        qWarning() << "article fetch failed:" << url;
        emit loadFinished( false );
        return;
      }
      const QPointer< ArticleWebView > self( this );
      showHtml( QString::fromUtf8( dataOf( req ) ), url );
      return;
    }

    const QPointer< ArticleWebView > self( this );
    connect( req.get(), &Request::finished, this, [ self, req, url ]() {
      if ( !self || !req->isFinished() )
        return;
      if ( req->dataSize() < 0 ) {
        qWarning() << "article fetch failed:" << url;
        self->emit loadFinished( false );
        return;
      }
      self->showHtml( QString::fromUtf8( dataOf( req ) ), url );
    } );
    return;
  }

  qWarning() << "no handler for url:" << url;
  emit loadFinished( false );
}

void ArticleWebView::showHtml( const QString & html, const QUrl & baseUrl, bool pushHistory )
{
  LiteHtml::Options opt;
  if ( cfg )
    opt.expandOptionalParts = cfg->preferences.alwaysExpandOptionalParts;

  const QString prepared = LiteHtml::prepare( html, opt );
  const QString title    = LiteHtml::extractTitle( html );

  if ( pushHistory ) {
    while ( history_.size() > historyIndex_ + 1 )
      history_.removeLast();
    if ( history_.isEmpty() || history_.last().url != baseUrl ) {
      LiteHistoryItem item;
      item.url  = baseUrl;
      item.html = prepared;
      item.title = title;
      history_.append( item );
    }
    historyIndex_ = history_.size() - 1;
  }

  renderHtml( prepared, baseUrl, title );
}

void ArticleWebView::renderHtml( const QString & prepared, const QUrl & baseUrl, const QString & title )
{
  currentHtml_ = prepared;
  currentUrl_  = baseUrl;
  pendingImages_.clear();

  document()->setBaseUrl( baseUrl );
  title_ = title;

  applyColorScheme( darkMode_ );

  setHtml( withPaletteCss( prepared ) );
  ensureCursorVisible();

  if ( historyIndex_ >= 0 && historyIndex_ < history_.size() ) {
    history_[ historyIndex_ ].html  = prepared;
    history_[ historyIndex_ ].title = title_;
  }

  setZoomFactor( zoomFactor_ );

  emit page_->titleChanged( title_ );
  emit loadFinished( true );
}

void ArticleWebView::reload()
{
  if ( currentUrl_.isValid() )
    load( currentUrl_ );
}

void ArticleWebView::back()
{
  if ( !canGoBack() )
    return;
  --historyIndex_;
  const auto item = history_[ historyIndex_ ];
  if ( !item.html.isEmpty() )
    renderHtml( item.html, item.url, item.title );
  else
    fetchAndShow( item.url );
}

void ArticleWebView::forward()
{
  if ( !canGoForward() )
    return;
  ++historyIndex_;
  const auto item = history_[ historyIndex_ ];
  if ( !item.html.isEmpty() )
    renderHtml( item.html, item.url, item.title );
  else
    fetchAndShow( item.url );
}

bool ArticleWebView::canGoBack() const
{
  return historyIndex_ > 0;
}

bool ArticleWebView::canGoForward() const
{
  return historyIndex_ >= 0 && historyIndex_ < history_.size() - 1;
}

LiteWebHistory * ArticleWebView::history() const
{
  return const_cast< LiteWebHistory * >( &historyProxy_ );
}

void ArticleWebView::setZoomFactor( qreal factor )
{
  if ( factor <= 0 || qFuzzyCompare( factor, zoomFactor_ ) )
    return;
  zoomInF( static_cast< float >( ( factor / zoomFactor_ - 1.0 ) * 100.0 ) );
  zoomFactor_ = factor;
}

void ArticleWebView::applyColorScheme( bool dark )
{
  darkMode_ = dark;

  const QPalette app = qApp->palette();

  QPalette p = palette();
  p.setColor( QPalette::Base, app.color( QPalette::Base ) );
  p.setColor( QPalette::Text, app.color( QPalette::Text ) );
  p.setColor( QPalette::Link, app.color( QPalette::Link ) );
  p.setColor( QPalette::Highlight, app.color( QPalette::Highlight ) );
  p.setColor( QPalette::HighlightedText, app.color( QPalette::HighlightedText ) );


  setPalette( p );
  viewport()->setPalette( p );

  applyDefaultStyleSheet();
}

void ArticleWebView::applyFontPreferences()
{
  fontStyleSheet_ = LiteFonts::styleSheet();
  applyDefaultStyleSheet();
}

void ArticleWebView::applyDefaultStyleSheet()
{
  QString css = fontStyleSheet_;
  if ( darkMode_ ) {
    css += QStringLiteral( "body, div, p, td, th, span, li { background-color: transparent; }"
                           "img { background-color: white; }" );
  }
  document()->setDefaultStyleSheet( css );
}

QString ArticleWebView::withPaletteCss( const QString & html ) const
{
  const QString block = QStringLiteral( "<style type=\"text/css\">%1</style>" )
                          .arg( articleThemeCss( palette(), darkMode_ ) );

  int at = html.lastIndexOf( QLatin1StringView( "</head>" ) );

  if ( at < 0 )
    at = html.indexOf( QLatin1StringView( "<body" ) );

  if ( at < 0 )
    return html;

  return html.left( at ) + block + html.mid( at );
}

QVariant ArticleWebView::loadResource( int type, const QUrl & name )
{
  QUrl url = name;
  if ( url.isRelative() )
    url = document()->baseUrl().resolved( url );

  if ( url.scheme() == "qrc" || url.scheme() == "qrcx" ) {
    QString path = url.path();
    if ( !path.startsWith( '/' ) )
      path = '/' + path;
    QFile f( QString( ":" ) + path );
    if ( f.open( QFile::ReadOnly ) )
      return f.readAll();
    qWarning() << "lite: missing resource" << path;
    return {};
  }

  if ( type == QTextDocument::StyleSheetResource ) {
    QString contentType;
    auto req = netMgr.getResource( url, contentType );
    if ( req && req->isFinished() && req->dataSize() >= 0 )
      return dataOf( req );
    return {};
  }

  if ( type == QTextDocument::ImageResource ) {
    QString contentType;
    auto req = netMgr.getResource( url, contentType );

    if ( req ) {
      if ( req->isFinished() ) {
        if ( req->dataSize() < 0 )
          return {};
        QImage img;
        img.loadFromData( dataOf( req ) );
        return img.isNull() ? QVariant() : QVariant( img );
      }
      if ( !pendingImages_.contains( url.toString() ) ) {
        pendingImages_.insert( url.toString() );
        const QPointer< ArticleWebView > self( this );
        connect( req.get(), &Request::finished, this, [ self, req, url ]() {
          if ( !self || !req->isFinished() || req->dataSize() < 0 )
            return;
          QImage img;
          img.loadFromData( dataOf( req ) );
          if ( !img.isNull() )
            self->document()->addResource( QTextDocument::ImageResource, url, img );
        } );
      }
      return {};
    }

    return {};
  }

  return QTextBrowser::loadResource( type, name );
}

int ArticleWebView::anchorPosition( const QString & anchor ) const
{
  if ( anchor.isEmpty() )
    return -1;

  for ( QTextBlock b = document()->begin(); b.isValid(); b = b.next() ) {
    for ( QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it ) {
      const QTextFormat f = it.fragment().charFormat();
      if ( f.property( QTextFormat::IsAnchor ).toBool()
           && f.property( QTextFormat::AnchorName ).toStringList().contains( anchor ) )
        return it.fragment().position();
    }
  }
  return -1;
}

bool ArticleWebView::hasAnchor( const QString & anchor ) const
{
  return anchorPosition( anchor ) >= 0;
}

bool ArticleWebView::scrollToAnchor( const QString & anchor )
{
  const int pos = anchorPosition( anchor );
  if ( pos < 0 )
    return false;

  QTextCursor c( document() );
  c.setPosition( pos );
  setTextCursor( c );
  ensureCursorVisible();
  return true;
}

bool ArticleWebView::selectAnchorRange( const QString & anchor )
{
  const int start = anchorPosition( anchor );
  if ( start < 0 )
    return false;

  int end = document()->characterCount() - 1;
  for ( QTextBlock b = document()->begin(); b.isValid(); b = b.next() ) {
    for ( QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it ) {
      const QTextFormat f = it.fragment().charFormat();
      if ( !f.property( QTextFormat::IsAnchor ).toBool() )
        continue;
      const int p = it.fragment().position();
      if ( p > start && p < end )
        end = p;
    }
  }

  QTextCursor c( document() );
  c.setPosition( start );
  c.setPosition( end, QTextCursor::KeepAnchor );
  setTextCursor( c );
  ensureCursorVisible();
  return true;
}

bool ArticleWebView::findText( const QString & text, bool caseSensitive, bool backward )
{
  if ( text.isEmpty() ) {
    QTextCursor c = textCursor();
    c.setPosition( c.position() );
    setTextCursor( c );
    return false;
  }

  QTextDocument::FindFlags flags;
  if ( caseSensitive )
    flags |= QTextDocument::FindCaseSensitively;
  if ( backward )
    flags |= QTextDocument::FindBackward;

  QTextCursor c = document()->find( text, textCursor(), flags );
  if ( c.isNull() ) {
    QTextCursor restart( document() );
    restart.movePosition( backward ? QTextCursor::End : QTextCursor::Start );
    c = document()->find( text, restart, flags );
  }
  if ( c.isNull() )
    return false;

  setTextCursor( c );
  ensureCursorVisible();
  return true;
}

bool ArticleWebView::hasSelection() const
{
  return !textCursor().selectedText().isEmpty();
}

void ArticleWebView::clearSearchHighlight()
{
  setExtraSelections( {} );
  QTextCursor c = textCursor();
  c.clearSelection();
  setTextCursor( c );
}

int ArticleWebView::highlightAll( const QStringList & words, bool caseSensitive )
{
  int count = 0;
  QTextDocument::FindFlags flags;
  if ( caseSensitive )
    flags |= QTextDocument::FindCaseSensitively;

  for ( const QString & word : words ) {
    if ( word.trimmed().isEmpty() )
      continue;
    QTextCursor c( document() );
    while ( !( c = document()->find( word, c, flags ) ).isNull() ) {
      QTextCharFormat fmt;
      fmt.setBackground( HIGHLIGHT_COLOR );
      c.mergeCharFormat( fmt );
      ++count;
    }
  }
  return count;
}

void ArticleWebView::clearHighlights()
{
  if ( currentHtml_.isEmpty() )
    return;
  setHtml( currentHtml_ );
}

QUrl ArticleWebView::linkAt( const QPoint & pos ) const
{
  const QString href = anchorAt( pos );
  if ( href.isEmpty() )
    return {};
  QUrl url( href );
  return url.isRelative() ? currentUrl_.resolved( url ) : url;
}

QUrl ArticleWebView::imageAt( const QPoint & pos ) const
{
  const QTextCursor c = cursorForPosition( pos );
  const QTextBlock block = c.block();

  for ( QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it ) {
    const QTextFragment frag = it.fragment();
    if ( !frag.isValid() || !frag.charFormat().isImageFormat() )
      continue;
    if ( frag.position() > c.position() || c.position() > frag.position() + frag.length() )
      continue;
    QUrl url( frag.charFormat().toImageFormat().name() );
    return url.isRelative() ? currentUrl_.resolved( url ) : url;
  }
  return {};
}

void ArticleWebView::mouseDoubleClickEvent( QMouseEvent * ev )
{
  QTextBrowser::mouseDoubleClickEvent( ev );
  emit doubleClicked( ev->pos() );
}

void ArticleWebView::mouseMoveEvent( QMouseEvent * ev )
{
  QTextBrowser::mouseMoveEvent( ev );
  const QString href = anchorAt( ev->pos() );
  emit linkHovered( href );
}

void ArticleWebView::mousePressEvent( QMouseEvent * ev )
{
  if ( ev->button() == Qt::MiddleButton )
    midButtonPressed = true;
  QTextBrowser::mousePressEvent( ev );

  if ( selectionBySingleClick && ev->button() == Qt::LeftButton ) {
    QTextCursor c = cursorForPosition( ev->pos() );
    c.select( QTextCursor::WordUnderCursor );
    setTextCursor( c );
  }
}

void ArticleWebView::wheelEvent( QWheelEvent * ev )
{
  if ( ev->modifiers().testFlag( Qt::ControlModifier ) ) {
    ev->ignore();
    return;
  }
  QTextBrowser::wheelEvent( ev );
}

bool ArticleWebView::eventFilter( QObject * obj, QEvent * ev )
{
  if ( obj == viewport() && ev->type() == QEvent::MouseButtonPress ) {
    auto * pe = static_cast< QMouseEvent * >( ev );
    if ( pe->button() == Qt::MiddleButton )
      midButtonPressed = true;
  }
  return QTextBrowser::eventFilter( obj, ev );
}

void ArticleWebView::linkClickedInHtml( const QUrl & )
{
}
