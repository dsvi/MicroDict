/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QComboBox>
#include <QAction>
#include <QList>
#include "instances.hh"

class GroupComboBox: public QComboBox
{
  Q_OBJECT

public:

  GroupComboBox( QWidget * parent );

  void fill( const Instances::Groups & );

  void setCurrentGroup( unsigned id );

  unsigned getCurrentGroup() const;

  QList< QAction * > getExternActions();

protected:

  virtual bool event( QEvent * event ) override;
  virtual void paintEvent( QPaintEvent * ) override;
  virtual QSize sizeHint() const override;

  virtual QSize minimumSizeHint() const override
  {
    return sizeHint();
  }

private slots:

  void popupGroups();
  void selectNextGroup();
  void selectPreviousGroup();

private:

  QAction popupAction;
  QAction selectNextAction, selectPreviousAction;
  QMap< int, int > shortcuts;
};
