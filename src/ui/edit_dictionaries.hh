/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "config.hh"
#include "dict/dictionary.hh"
#include "edit_sources_models.hh"
#include "edit_groups.hh"
#include "instances.hh"
#include "edit_orderandprops.hh"
#include "ui_edit_dictionaries.h"
#include <QAction>
#include <QPointer>

class EditDictionaries: public QDialog
{
  Q_OBJECT

public:

  EditDictionaries( QWidget * parent,
                    Config::Class & cfg,
                    std::vector< sptr< Dictionary::Class > > & dictionaries,
                    Instances::Groups & groupInstances );

  ~EditDictionaries();

  void editGroup( unsigned id );

  bool areDictionariesChanged() const
  {
    return dictionariesChanged;
  }

  bool areGroupsChanged() const
  {
    return groupsChanged;
  }

protected:

  virtual void accept();

private slots:

  void currentChanged( int index );

  void buttonBoxClicked( QAbstractButton * button );

  void rescanSources();

signals:

  void showDictionaryInfo( const QString & dictId );

  void showDictionaryHeadwords( Dictionary::Class * dict );

private:

  bool isSourcesChanged() const;

  void acceptChangedSources( bool rebuildGroups );

  void save( bool rebuildGroups = false );

private:

  Config::Class & cfg;
  std::vector< sptr< Dictionary::Class > > & dictionaries;
  Instances::Groups & groupInstances;

  Config::GroupBackup origGroups;

  Ui::EditDictionaries ui;
  Sources sources;
  QPointer< OrderAndProps > orderAndProps;
  QPointer< Groups > groups;

  bool dictionariesChanged;
  bool groupsChanged;

  QString lastTabName;

  QAction helpAction;
};
