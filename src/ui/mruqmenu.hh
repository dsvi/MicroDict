#pragma once

#include <QMenu>
#include <QEvent>

class MRUQMenu: public QMenu
{
  Q_OBJECT

public:
  explicit MRUQMenu( const QString title, QWidget * parent = 0 );

private:
  void keyReleaseEvent( QKeyEvent * kev ) override;

signals:
  void requestTabChange( int index );
};
