#pragma once

#include <QWidget>
#include <QObject>
#include "ui/articleview.hh"

class ArticleSaver: public QObject
{
  Q_OBJECT

public:
  explicit ArticleSaver( QWidget * uiParent, ArticleView * view, Config::Class & cfg );
  ~ArticleSaver() override;

  void save();

signals:
  void statusMessage( const QString & text, int timeout );

private:
  QWidget * uiParent_;
  ArticleView * view_;
  Config::Class & cfg_;
};
