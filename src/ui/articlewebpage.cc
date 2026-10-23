/* Licensed under GPLv3 or later, see the LICENSE file */

#include "articlewebpage.hh"
#include "articlewebview.hh"

ArticleWebPage::ArticleWebPage( ArticleWebView * view_, QObject * parent ):
  QObject( parent ),
  view( view_ )
{
}

void ArticleWebPage::setHtml( const QString & content, const QUrl & baseUrl )
{
  view->showHtml( content, baseUrl );
}

void ArticleWebPage::setContent( const QByteArray & data, const QString & mimeType, const QUrl & baseUrl )
{
  if ( mimeType.contains( "plain" ) ) {
    view->showHtml( Qt::convertFromPlainText( QString::fromUtf8( data ), Qt::WhiteSpaceNormal ), baseUrl );
    return;
  }
  view->showHtml( QString::fromUtf8( data ), baseUrl );
}

void ArticleWebPage::toHtml( const std::function< void( const QString & ) > & callback ) const
{
  callback( view->currentHtml() );
}

void ArticleWebPage::setUrl( const QUrl & url )
{
  view->load( url );
}

QUrl ArticleWebPage::url() const
{
  return view->url();
}

QString ArticleWebPage::title() const
{
  return view->title();
}


void ArticleWebPage::runJavaScript( const QString & script )
{
  static bool warned = false;
  if ( !warned ) {
    warned = true;
    qInfo() << "lite: runJavaScript() is a no-op;" << script.left( 60 );
  }
}

void ArticleWebPage::runJavaScript( const QString & script,
                                    const std::function< void( const QVariant & ) > & callback )
{
  runJavaScript( script );
  if ( callback )
    callback( QVariant() );
}

void ArticleWebPage::setWebChannel( [[maybe_unused]] QObject * channel )
{
}
