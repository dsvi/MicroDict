/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once
#include "ui_edit_sources.h"
#include "config.hh"
#include "dict/hunspell.hh"
#include <QAbstractItemModel>
#include <QComboBox>
#include <QItemDelegate>
#include <QItemEditorFactory>
#include <memory>

#include "globalbroadcaster.hh"

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
class ChineseConversion;
#endif

class ProgramsModel: public QAbstractTableModel
{
  Q_OBJECT

public:

  ProgramsModel( QWidget * parent, const Config::Programs & );

  void removeProgram( int index );
  void addNewProgram();

  void remove( const QModelIndexList & );

  const Config::Programs & getCurrentPrograms() const
  {
    return programs;
  }

  Qt::ItemFlags flags( const QModelIndex & index ) const override;
  int rowCount( const QModelIndex & parent ) const override;
  int columnCount( const QModelIndex & parent ) const override;
  QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
  QVariant data( const QModelIndex & index, int role ) const override;
  bool setData( const QModelIndex & index, const QVariant & value, int role ) override;

private:

  Config::Programs programs;
};

class ProgramTypeEditor: public QComboBox
{
  Q_OBJECT
  Q_PROPERTY( int type READ getType WRITE setType USER true )

public:
  explicit ProgramTypeEditor( QWidget * widget = nullptr );

  static QString getNameForType( int );

public:
  int getType() const;
  void setType( int );
};

class PathsModel: public QAbstractTableModel
{
  Q_OBJECT

public:

  PathsModel( QWidget * parent, const Config::Paths & );

  void removePath( int index );

  void addNewPath( const QString & );

  void remove( const QModelIndexList & );

  const Config::Paths & getCurrentPaths() const
  {
    return paths;
  }

  Qt::ItemFlags flags( const QModelIndex & index ) const override;
  int rowCount( const QModelIndex & parent ) const override;
  int columnCount( const QModelIndex & parent ) const override;
  QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
  QVariant data( const QModelIndex & index, int role ) const override;
  bool setData( const QModelIndex & index, const QVariant & value, int role ) override;

private:

  Config::Paths paths;
};

class SoundDirsModel: public QAbstractTableModel
{
  Q_OBJECT

public:

  SoundDirsModel( QWidget * parent, const Config::SoundDirs & );

  void removeSoundDir( int index );

  void addNewSoundDir( const QString & path, const QString & name );

  void removeSoundDirs( const QList< QModelIndex > & indexes );

  const Config::SoundDirs & getCurrentSoundDirs() const
  {
    return soundDirs;
  }

  Qt::ItemFlags flags( const QModelIndex & index ) const override;
  int rowCount( const QModelIndex & parent ) const override;
  int columnCount( const QModelIndex & parent ) const override;
  QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
  QVariant data( const QModelIndex & index, int role ) const override;
  bool setData( const QModelIndex & index, const QVariant & value, int role ) override;

private:

  Config::SoundDirs soundDirs;
};

class HunspellDictsModel: public QAbstractTableModel
{
  Q_OBJECT

public:

  HunspellDictsModel( QWidget * parent, const Config::Hunspell & );

  void changePath( const QString & newPath );

  const Config::Hunspell::Dictionaries & getEnabledDictionaries() const
  {
    return enabledDictionaries;
  }

  Qt::ItemFlags flags( const QModelIndex & index ) const override;
  int rowCount( const QModelIndex & parent ) const override;
  int columnCount( const QModelIndex & parent ) const override;
  QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
  QVariant data( const QModelIndex & index, int role ) const override;
  bool setData( const QModelIndex & index, const QVariant & value, int role ) override;

private:

  Config::Hunspell::Dictionaries enabledDictionaries;
  std::vector< HunspellMorpho::DataFiles > dataFiles;
};


class Sources: public QWidget
{
  Q_OBJECT

public:
  Sources( QWidget * parent, const Config::Class & );

  const Config::Paths & getPaths() const
  {
    return pathsModel.getCurrentPaths();
  }

  const Config::SoundDirs & getSoundDirs() const
  {
    return soundDirsModel.getCurrentSoundDirs();
  }

  const Config::Programs & getPrograms() const
  {
    return programsModel.getCurrentPrograms();
  }
  Config::Hunspell getHunspell() const;

  Config::Transliteration getTransliteration() const;

signals:

  void rescan();

private:
  Ui::Sources ui;

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  ChineseConversion * chineseConversion;
#endif
  QItemDelegate * itemDelegate;
  std::unique_ptr< QItemEditorFactory > itemEditorFactory;

  ProgramsModel programsModel;
  PathsModel pathsModel;
  SoundDirsModel soundDirsModel;
  HunspellDictsModel hunspellDictsModel;

  void fitPathsColumns();
  void fitSoundDirsColumns();
  void fitHunspellDictsColumns();

private slots:

  void on_addPath_clicked();
  void on_removePath_clicked();

  void on_addSoundDir_clicked();
  void on_removeSoundDir_clicked();

  void on_changeHunspellPath_clicked();

  void on_addProgram_clicked();
  void on_removeProgram_clicked();

  void on_rescan_clicked();
};
