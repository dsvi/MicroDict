/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QWidget>
#include <QSize>

class DictsPaneWidget: public QWidget
{
  Q_OBJECT

public:

  DictsPaneWidget( QWidget * parent = 0 ):
    QWidget( parent )
  {
  }

  virtual QSize sizeHint() const
  {
    return QSize( 204, 204 );
  }
};
