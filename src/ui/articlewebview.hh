/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "config.hh"
#include <QByteArray>
#include <QPoint>
#include <QSet>
#include <QTextBrowser>
#include <QUrl>
#include <QList>

class ArticleResourceManager;
class ArticleWebPage;

struct LiteHistoryItem {
  QUrl url;
  QString title;
  QString html;
};

class LiteWebHistory: public QObject
{
  Q_OBJECT

public:
  LiteWebHistory( const QList< LiteHistoryItem > * items, const int * index, QObject * parent = nullptr ) :
    QObject( parent ),
    items( items ),
    index( index )
  {
  }

  bool canGoBack() const;
  bool canGoForward() const;
  LiteHistoryItem currentItem() const;

private:
  const QList< LiteHistoryItem > * items;
  const int * index;
};

class ArticleWebView: public QTextBrowser
{
  Q_OBJECT

public:
  ArticleWebView( QWidget * parent, ArticleResourceManager & netMgr );

  void setUp( Config::Class * _cfg );

  ArticleWebPage * page() const
  {
    return page_;
  }

  void load( const QUrl & url );
  void reload();
  void back();
  void forward();
  bool canGoBack() const;
  bool canGoForward() const;
  LiteWebHistory * history() const;

  QUrl url() const
  {
    return currentUrl_;
  }

  QString title() const
  {
    return title_;
  }

  void showHtml( const QString & html, const QUrl & baseUrl, bool pushHistory = true );

  QString currentHtml() const
  {
    return currentHtml_;
  }

  qreal zoomFactor() const
  {
    return zoomFactor_;
  }
  void setZoomFactor( qreal factor );

  QString selectedText() const
  {
    return textCursor().selectedText();
  }

  QUrl linkAt( const QPoint & pos ) const;
  QUrl imageAt( const QPoint & pos ) const;

  bool scrollToAnchor( const QString & anchor );
  bool selectAnchorRange( const QString & anchor );
  bool hasAnchor( const QString & anchor ) const;

  bool findText( const QString & text, bool caseSensitive, bool backward );

  bool hasSelection() const;

  void clearSearchHighlight();
  int highlightAll( const QStringList & words, bool caseSensitive );
  void clearHighlights();

  void applyColorScheme( bool dark );

  void applyFontPreferences();

  bool isMidButtonPressed() const
  {
    return midButtonPressed;
  }
  void resetMidButtonPressed()
  {
    midButtonPressed = false;
  }
  void setSelectionBySingleClick( bool set )
  {
    selectionBySingleClick = set;
  }

  void linkClickedInHtml( const QUrl & );

signals:
  void doubleClicked( QPoint pos );
  void linkClicked( const QUrl & url );
  void loadFinished( bool ok );
  void linkHovered( const QString & link );

protected:
  QVariant loadResource( int type, const QUrl & name ) override;
  bool eventFilter( QObject * obj, QEvent * ev ) override;
  void wheelEvent( QWheelEvent * ev ) override;
  void mouseDoubleClickEvent( QMouseEvent * ev ) override;
  void mouseMoveEvent( QMouseEvent * ev ) override;
  void mousePressEvent( QMouseEvent * ev ) override;

private:
  void fetchAndShow( const QUrl & url );
  void renderHtml( const QString & html, const QUrl & baseUrl, const QString & title );
  int anchorPosition( const QString & anchor ) const;

  ArticleResourceManager & netMgr;
  ArticleWebPage * page_;
  Config::Class * cfg = nullptr;

  QList< LiteHistoryItem > history_;
  int historyIndex_ = -1;
  LiteWebHistory historyProxy_;

  QUrl currentUrl_;
  QString title_;
  QString currentHtml_;

  qreal zoomFactor_ = 1.0;
  bool midButtonPressed = false;
  bool selectionBySingleClick = false;
  bool darkMode_ = false;
  QString fontStyleSheet_;

  void applyDefaultStyleSheet();

  QString withPaletteCss( const QString & html ) const;

  QSet< QString > pendingImages_;
};
