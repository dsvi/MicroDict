/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "ui_edit_groups.h"
#include "config.hh"
#include "dict/dictionary.hh"
#include <QMenu>

class Groups: public QWidget
{
  Q_OBJECT

public:
  Groups( QWidget * parent,
          const std::vector< sptr< Dictionary::Class > > &,
          const Config::Groups &,
          const Config::Group & order );
  void resetData( const std::vector< sptr< Dictionary::Class > > & dicts_,
                  const Config::Groups & groups_,
                  const Config::Group & order );
  void editGroup( unsigned id );

  void updateDictionaryOrder( const Config::Group & order );

  Config::Groups getGroups() const;

private:
  Ui::Groups ui;
  const std::vector< sptr< Dictionary::Class > > & dicts;
  Config::Groups groups;

  QToolButton * groupsListButton;
  QMenu * groupsListMenu;

  void countChanged();

private slots:
  void addNew();
  void renameCurrent();
  void removeCurrent();
  void removeAll();
  void addToGroup();
  void removeFromGroup();

  void addAutoGroups();
  void addAutoGroupsByFolders();
  void groupsByMetadata();

  void showDictInfo( const QPoint & pos );
  void fillGroupsMenu();
  void switchToGroup( QAction * act );

signals:
  void showDictionaryInfo( const QString & id );
};
