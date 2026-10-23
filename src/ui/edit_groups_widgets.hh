/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <vector>
#include <QListWidget>
#include <QSortFilterProxyModel>
#include "config.hh"
#include "dict/dictionary.hh"

class DictListModel: public QAbstractListModel
{
  Q_OBJECT

public:

  DictListModel( QWidget * parent ):
    QAbstractListModel( parent ),
    isSource( false ),
    allDicts( 0 )
  {
  }

  void populate( const std::vector< sptr< Dictionary::Class > > & active,
                 const std::vector< sptr< Dictionary::Class > > & available );
  void populate( const std::vector< sptr< Dictionary::Class > > & active );

  void setAsSource();
  bool sourceModel() const
  {
    return isSource;
  }

  const std::vector< sptr< Dictionary::Class > > & getCurrentDictionaries() const;

  void removeSelectedRows( QItemSelectionModel * source );
  void addSelectedUniqueFromModel( QItemSelectionModel * source );

  Qt::ItemFlags flags( const QModelIndex & index ) const override;
  int rowCount( const QModelIndex & parent ) const override;
  QVariant data( const QModelIndex & index, int role ) const override;
  bool insertRows( int row, int count, const QModelIndex & parent ) override;
  bool removeRows( int row, int count, const QModelIndex & parent ) override;
  bool setData( const QModelIndex & index, const QVariant & value, int role ) override;

  void addRow( const QModelIndex & parent, sptr< Dictionary::Class > dict );

  Qt::DropActions supportedDropActions() const override;

  void filterDuplicates();

private:

  bool isSource;
  std::vector< sptr< Dictionary::Class > > dictionaries;
  const std::vector< sptr< Dictionary::Class > > * allDicts;

signals:
  void contentChanged();
};

class DictListWidget: public QListView
{
  Q_OBJECT

public:
  DictListWidget( QWidget * parent );
  ~DictListWidget() override = default;

  void populate( const std::vector< sptr< Dictionary::Class > > & active,
                 const std::vector< sptr< Dictionary::Class > > & available );
  void populate( const std::vector< sptr< Dictionary::Class > > & active );

  void setAsSource();

  const std::vector< sptr< Dictionary::Class > > & getCurrentDictionaries() const;

  DictListModel * getModel()
  {
    return &model;
  }

signals:
  void gotFocus();

protected:
  void dropEvent( QDropEvent * event ) override;
  void focusInEvent( QFocusEvent * ) override;

  void rowsAboutToBeRemoved( const QModelIndex & parent, int start, int end ) override;

private:
  DictListModel model;
};

#include "ui_edit_group_tab.h"

class DictGroupWidget: public QWidget
{
  Q_OBJECT

public:
  DictGroupWidget( QWidget * parent, const std::vector< sptr< Dictionary::Class > > &, const Config::Group & );

  Config::Group makeGroup() const;

  DictListModel * getModel() const
  {
    return ui.dictionaries->getModel();
  }

  QItemSelectionModel * getSelectionModel() const
  {
    return ui.dictionaries->selectionModel();
  }

  QString name()
  {
    return groupName;
  }

  void setName( const QString & name )
  {
    groupName = name;
  }

private slots:

  void groupIconActivated( int );
  void showDictInfo( const QPoint & pos );
  void removeCurrentItem( const QModelIndex & );

private:
  Ui::DictGroupWidget ui;
  unsigned groupId;
  QString groupName;

signals:
  void showDictionaryInfo( const QString & id );
};

class DictGroupsWidget: public QTabWidget
{
  Q_OBJECT

public:

  DictGroupsWidget( QWidget * parent );

  void populate( const Config::Groups &,
                 const std::vector< sptr< Dictionary::Class > > & allDicts,
                 const std::vector< sptr< Dictionary::Class > > & activeDicts );

  int addNewGroup( const QString & );

  int addUniqueGroup( const QString & name );

  void addAutoGroups();

  void addAutoGroupsByFolders();
  void addGroupBasedOnMap( const QMultiMap< QString, sptr< Dictionary::Class > > & groupToDicts );

  void groupsByMetadata();

  QString getCurrentGroupName() const;

  void renameCurrentGroup( const QString & );

  void removeCurrentGroup();

  void removeAllGroups();

  Config::Groups makeGroups() const;

  DictListModel * getCurrentModel() const;

  DictListModel * getModelAt( int current ) const;
  int getDictionaryCountAt( int current ) const;
  std::vector< sptr< Dictionary::Class > > getDictionaryAt( int current ) const;

  QItemSelectionModel * getCurrentSelectionModel() const;

protected:
  void tabInserted( int index ) override;
  void tabRemoved( int index ) override;

private:

  void combineGroups( int source, int target );

  const std::vector< sptr< Dictionary::Class > > * allDicts;
  const std::vector< sptr< Dictionary::Class > > * activeDicts;

private slots:
  void contextMenu( const QPoint & );
  void tabDataChanged();
  void removeTabRequested( int index );
  void addNewTab();


signals:
  void showDictionaryInfo( const QString & id );
  void countChanged();
  void newTabRequested();
};

class QuickFilterLine: public QLineEdit
{
  Q_OBJECT

public:

  QuickFilterLine( QWidget * parent );
  ~QuickFilterLine() override;

  void applyTo( QAbstractItemView * source );

  QAction * getFocusAction()
  {
    return &m_focusAction;
  }

  QModelIndex mapToSource( const QModelIndex & idx );

protected:
  void keyPressEvent( QKeyEvent * event ) override;

private:
  QSortFilterProxyModel m_proxyModel;
  QAction m_focusAction;
  QAbstractItemView * m_source;

private slots:
  void filterChangedInternal();
  void emitFilterChanged();
  void focusFilterLine();

signals:
  void filterChanged( const QString & filter );
};
