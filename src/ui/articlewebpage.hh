/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class ArticleWebView;

class ArticleWebPage: public QObject
{
  Q_OBJECT

public:
  explicit ArticleWebPage( ArticleWebView * view, QObject * parent = nullptr );

  void setHtml( const QString & content, const QUrl & baseUrl = QUrl() );
  void setContent( const QByteArray & data, const QString & mimeType = QString(), const QUrl & baseUrl = QUrl() );
  void toHtml( const std::function< void( const QString & ) > & callback ) const;

  void setUrl( const QUrl & url );
  QUrl url() const;
  QString title() const;

  void runJavaScript( const QString & script );
  void runJavaScript( const QString & script, const std::function< void( const QVariant & ) > & callback );
  void setWebChannel( QObject * channel );

signals:
  void titleChanged( const QString & title );
  void linkHovered( const QString & link );
  void linkClicked( const QUrl & url );
  void loadFinished( bool ok );

private:
  ArticleWebView * view;
};
