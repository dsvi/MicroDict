/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "edit_sources_models.hh"
#include "globalbroadcaster.hh"
#include <QFileDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStyleHints>

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  #include "chineseconversion.hh"
#endif


Sources::Sources( QWidget * parent, const Config::Class & cfg ):
  QWidget( parent ),
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  chineseConversion( new ChineseConversion( this, cfg.transliteration.chinese ) ),
#endif
  itemDelegate( new QItemDelegate( this ) ),
  itemEditorFactory( std::make_unique< QItemEditorFactory >() ),
  programsModel( this, cfg.programs ),
  pathsModel( this, cfg.paths ),
  soundDirsModel( this, cfg.soundDirs ),
  hunspellDictsModel( this, cfg.hunspell )
{
  ui.setupUi( this );

  const Config::Hunspell & hunspell   = cfg.hunspell;
  const Config::Transliteration & trs = cfg.transliteration;

  itemEditorFactory->registerEditor( QMetaType::Int, new QStandardItemEditorCreator< ProgramTypeEditor >() );

  itemDelegate->setItemEditorFactory( itemEditorFactory.get() );

  ui.programs->setTabKeyNavigation( true );
  ui.programs->setModel( &programsModel );
  ui.programs->resizeColumnToContents( 0 );
  ui.programs->setColumnWidth(
    1,
    QFontMetrics( QFont() ).horizontalAdvance( ProgramTypeEditor::getNameForType( Config::Program::PrefixMatch ) )
      + 16 );
  ui.programs->resizeColumnToContents( 2 );
  ui.programs->resizeColumnToContents( 3 );
  ui.programs->resizeColumnToContents( 4 );
  ui.programs->setItemDelegate( itemDelegate );
  ui.programs->setSelectionMode( QAbstractItemView::ExtendedSelection );
  ui.programs->setSelectionBehavior( QAbstractItemView::SelectRows );

  ui.paths->setTabKeyNavigation( true );
  ui.paths->setModel( &pathsModel );
  ui.paths->setSelectionMode( QAbstractItemView::ExtendedSelection );
  ui.paths->setSelectionBehavior( QAbstractItemView::SelectRows );

  fitPathsColumns();

  ui.soundDirs->setTabKeyNavigation( true );
  ui.soundDirs->setModel( &soundDirsModel );
  ui.soundDirs->setSelectionMode( QAbstractItemView::ExtendedSelection );
  ui.soundDirs->setSelectionBehavior( QAbstractItemView::SelectRows );

  fitSoundDirsColumns();

  ui.hunspellPath->setText( hunspell.dictionariesPath );
  ui.hunspellDictionaries->setTabKeyNavigation( true );
  ui.hunspellDictionaries->setModel( &hunspellDictsModel );

  fitHunspellDictsColumns();

  ui.enableRussianTransliteration->setChecked( trs.enableRussianTransliteration );
  ui.enableGermanTransliteration->setChecked( trs.enableGermanTransliteration );
  ui.enableGreekTransliteration->setChecked( trs.enableGreekTransliteration );
  ui.enableBelarusianTransliteration->setChecked( trs.enableBelarusianTransliteration );

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  ui.transliterationLayout->addWidget( chineseConversion );
  ui.transliterationLayout->addItem( new QSpacerItem( 20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding ) );
#endif

  ui.enableRomaji->setChecked( trs.romaji.enable );
  ui.enableHiragana->setChecked( trs.romaji.enableHiragana );
  ui.enableKatakana->setChecked( trs.romaji.enableKatakana );

  ui.enableCustomTransliteration->setChecked( trs.customTrans.enable );
  ui.customTransliteration->setPlainText( trs.customTrans.context );

}

void Sources::fitPathsColumns()
{
  ui.paths->resizeColumnToContents( 0 );
  ui.paths->resizeColumnToContents( 1 );
}

void Sources::fitSoundDirsColumns()
{
  ui.soundDirs->resizeColumnToContents( 0 );
  ui.soundDirs->resizeColumnToContents( 1 );
  ui.soundDirs->resizeColumnToContents( 2 );
}

void Sources::fitHunspellDictsColumns()
{
  ui.hunspellDictionaries->resizeColumnToContents( 0 );
  ui.hunspellDictionaries->resizeColumnToContents( 1 );
}

void Sources::on_addPath_clicked()
{
  QString dir = QFileDialog::getExistingDirectory( this, tr( "Choose a directory" ) );

  if ( !dir.isEmpty() ) {
    pathsModel.addNewPath( dir );
    fitPathsColumns();
  }
}

void Sources::on_removePath_clicked()
{
  QModelIndexList selected = ui.paths->selectionModel()->selectedRows();

  if ( selected.isEmpty() ) {
    return;
  }

  if ( QMessageBox::question( this,
                              tr( "Confirm removal" ),
                              tr( "Remove selected directories from the list?" ),
                              QMessageBox::StandardButtons( QMessageBox::Ok | QMessageBox::Cancel ) )
       != QMessageBox::Ok ) {
    return;
  }

  pathsModel.remove( selected );

  fitPathsColumns();
}

void Sources::on_addSoundDir_clicked()
{
  QString dir = QFileDialog::getExistingDirectory( this, tr( "Choose a directory" ) );

  if ( !dir.isEmpty() ) {
    soundDirsModel.addNewSoundDir( dir, QDir( dir ).dirName() );
    fitSoundDirsColumns();
  }
}

void Sources::on_removeSoundDir_clicked()
{
  QModelIndexList selected = ui.soundDirs->selectionModel()->selectedRows();
  if ( selected.isEmpty() ) {
    return;
  }

  if ( QMessageBox::question( this,
                              tr( "Confirm removal" ),
                              tr( "Remove %1 directories from the list?" ).arg( selected.size() ),
                              QMessageBox::Ok | QMessageBox::Cancel )
       != QMessageBox::Ok ) {
    return;
  }
  soundDirsModel.removeSoundDirs( selected );
  fitSoundDirsColumns();
}

void Sources::on_changeHunspellPath_clicked()
{
  QString dir = QFileDialog::getExistingDirectory( this, tr( "Choose a directory" ) );

  if ( !dir.isEmpty() ) {
    ui.hunspellPath->setText( dir );
    hunspellDictsModel.changePath( dir );
    fitHunspellDictsColumns();
  }
}

void Sources::on_addProgram_clicked()
{
  programsModel.addNewProgram();

  QModelIndex result = programsModel.index( programsModel.rowCount( QModelIndex() ) - 1, 1, QModelIndex() );

  ui.programs->scrollTo( result );
  ui.programs->edit( result );
}

void Sources::on_removeProgram_clicked()
{
  QModelIndexList selected = ui.programs->selectionModel()->selectedRows();
  if ( selected.isEmpty() ) {
    return;
  }

  QString message = tr( "Remove %1 programs from the list?" ).arg( selected.size() );

  if ( QMessageBox::question( this,
                              tr( "Confirm removal" ),
                              message,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No )
       != QMessageBox::Yes ) {
    return;
  }

  programsModel.remove( selected );
}

Config::Hunspell Sources::getHunspell() const
{
  Config::Hunspell h;

  h.dictionariesPath    = ui.hunspellPath->text();
  h.enabledDictionaries = hunspellDictsModel.getEnabledDictionaries();

  return h;
}

Config::Transliteration Sources::getTransliteration() const
{
  Config::Transliteration tr;

  tr.enableRussianTransliteration    = ui.enableRussianTransliteration->isChecked();
  tr.enableGermanTransliteration     = ui.enableGermanTransliteration->isChecked();
  tr.enableGreekTransliteration      = ui.enableGreekTransliteration->isChecked();
  tr.enableBelarusianTransliteration = ui.enableBelarusianTransliteration->isChecked();
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  chineseConversion->getConfig( tr.chinese );
#endif
  tr.romaji.enable         = ui.enableRomaji->isChecked();
  tr.romaji.enableHiragana = ui.enableHiragana->isChecked();
  tr.romaji.enableKatakana = ui.enableKatakana->isChecked();

  tr.customTrans.enable  = ui.enableCustomTransliteration->isChecked();
  tr.customTrans.context = ui.customTransliteration->toPlainText();

  return tr;
}

ProgramsModel::ProgramsModel( QWidget * parent, const Config::Programs & programs_ ):
  QAbstractTableModel( parent ),
  programs( programs_ )
{
}

void ProgramsModel::removeProgram( int index )
{
  beginRemoveRows( QModelIndex(), index, index );
  programs.erase( programs.begin() + index );
  endRemoveRows();
}

void ProgramsModel::addNewProgram()
{
  Config::Program p;

  p.enabled = false;
  p.type    = Config::Program::Audio;

  p.id = Dictionary::generateRandomDictionaryId();

  beginInsertRows( QModelIndex(), programs.size(), programs.size() );
  programs.push_back( p );
  endInsertRows();
}

void ProgramsModel::remove( const QModelIndexList & indexes )
{
  beginResetModel();
  QList< qsizetype > rows;
  rows.reserve( indexes.size() );

  for ( auto & i : std::as_const( indexes ) ) {
    rows.push_back( i.row() );
  }

  decltype( programs ) newSoundDirs;
  for ( auto i = 0; i < programs.size(); ++i ) {
    if ( !rows.contains( i ) ) {
      newSoundDirs.push_back( programs[ i ] );
    }
  }
  programs.swap( newSoundDirs );
  endResetModel();
}

Qt::ItemFlags ProgramsModel::flags( const QModelIndex & index ) const
{
  Qt::ItemFlags result = QAbstractTableModel::flags( index );

  if ( index.isValid() ) {
    if ( !index.column() ) {
      result |= Qt::ItemIsUserCheckable;
    }
    else {
      result |= Qt::ItemIsEditable;
    }
  }

  return result;
}

int ProgramsModel::rowCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return programs.size();
  }
}

int ProgramsModel::columnCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return 5;
  }
}

QVariant ProgramsModel::headerData( int section, Qt::Orientation, int role ) const
{
  if ( role == Qt::DisplayRole ) {
    switch ( section ) {
      case 0:
        return tr( "Enabled" );
      case 1:
        return tr( "Type" );
      case 2:
        return tr( "Name" );
      case 3:
        return tr( "Command Line" );
      case 4:
        return tr( "Icon" );
      default:
        return QVariant();
    }
  }

  return QVariant();
}

QVariant ProgramsModel::data( const QModelIndex & index, int role ) const
{
  if ( index.row() >= programs.size() ) {
    return QVariant();
  }

  if ( role == Qt::DisplayRole || role == Qt::EditRole ) {
    switch ( index.column() ) {
      case 1:
        if ( role == Qt::DisplayRole ) {
          return ProgramTypeEditor::getNameForType( programs[ index.row() ].type );
        }
        else {
          return QVariant( (int)programs[ index.row() ].type );
        }
      case 2:
        return programs[ index.row() ].name;
      case 3:
        return programs[ index.row() ].commandLine;
      case 4:
        return programs[ index.row() ].iconFilename;
      default:
        return QVariant();
    }
  }

  if ( role == Qt::CheckStateRole && !index.column() ) {
    return programs[ index.row() ].enabled ? Qt::Checked : Qt::Unchecked;
  }

  return QVariant();
}

bool ProgramsModel::setData( const QModelIndex & index, const QVariant & value, int role )
{
  if ( index.row() >= programs.size() ) {
    return false;
  }

  if ( role == Qt::CheckStateRole && !index.column() ) {
    programs[ index.row() ].enabled = !programs[ index.row() ].enabled;

    dataChanged( index, index );
    return true;
  }

  if ( role == Qt::DisplayRole || role == Qt::EditRole ) {
    switch ( index.column() ) {
      case 1:
        programs[ index.row() ].type = Config::Program::Type( value.toInt() );
        dataChanged( index, index );
        return true;
      case 2:
        programs[ index.row() ].name = value.toString();
        dataChanged( index, index );
        return true;
      case 3:
        programs[ index.row() ].commandLine = value.toString();
        dataChanged( index, index );
        return true;
      case 4:
        programs[ index.row() ].iconFilename = value.toString();
        dataChanged( index, index );
        return true;
      default:
        return false;
    }
  }

  return false;
}

QString ProgramTypeEditor::getNameForType( int v )
{
  switch ( v ) {
    case Config::Program::Audio:
      return tr( "Audio" );
    case Config::Program::PlainText:
      return tr( "Plain Text" );
    case Config::Program::Html:
      return tr( "Html" );
    case Config::Program::PrefixMatch:
      return tr( "Prefix Match" );
    default:
      return tr( "Unknown" );
  }
}

ProgramTypeEditor::ProgramTypeEditor( QWidget * widget ):
  QComboBox( widget )
{
  for ( int x = 0; x < Config::Program::MaxTypeValue; ++x ) {
    addItem( getNameForType( x ) );
  }
}

int ProgramTypeEditor::getType() const
{
  return currentIndex();
}

void ProgramTypeEditor::setType( int t )
{
  setCurrentIndex( t );
}

PathsModel::PathsModel( QWidget * parent, const Config::Paths & paths_ ):
  QAbstractTableModel( parent ),
  paths( paths_ )
{
}

void PathsModel::removePath( int index )
{
  beginRemoveRows( QModelIndex(), index, index );
  paths.erase( paths.begin() + index );
  endRemoveRows();
}

void PathsModel::addNewPath( const QString & path )
{
  beginInsertRows( QModelIndex(), paths.size(), paths.size() );
  paths.push_back( Config::Path( path, false ) );
  endInsertRows();
}

void PathsModel::remove( const QModelIndexList & indexes )
{
  beginResetModel();
  QList< qsizetype > rows;
  rows.reserve( indexes.size() );

  for ( auto & i : std::as_const( indexes ) ) {
    rows.push_back( i.row() );
  }

  decltype( paths ) newSoundDirs;
  for ( auto i = 0; i < paths.size(); ++i ) {
    if ( !rows.contains( i ) ) {
      newSoundDirs.push_back( paths[ i ] );
    }
  }
  paths.swap( newSoundDirs );
  endResetModel();
}

Qt::ItemFlags PathsModel::flags( const QModelIndex & index ) const
{
  Qt::ItemFlags result = QAbstractTableModel::flags( index );

  if ( Config::isPortableVersion() ) {
    if ( index.isValid() && index.row() == 0 ) {
      result &= ~Qt::ItemIsSelectable;
      result &= ~Qt::ItemIsEnabled;
    }
  }

  if ( index.isValid() && index.column() == 1 ) {
    result |= Qt::ItemIsUserCheckable;
  }

  return result;
}

int PathsModel::rowCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return paths.size();
  }
}

int PathsModel::columnCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return 2;
  }
}

QVariant PathsModel::headerData( int section, Qt::Orientation, int role ) const
{
  if ( role == Qt::DisplayRole ) {
    switch ( section ) {
      case 0:
        return tr( "Path" );
      case 1:
        return tr( "Recursive" );
      default:
        return QVariant();
    }
  }

  return QVariant();
}

QVariant PathsModel::data( const QModelIndex & index, int role ) const
{
  if ( index.row() >= paths.size() ) {
    return QVariant();
  }

  if ( ( role == Qt::DisplayRole || role == Qt::EditRole ) && !index.column() ) {
    return paths[ index.row() ].path;
  }

  if ( role == Qt::CheckStateRole && index.column() == 1 ) {
    return paths[ index.row() ].recursive ? Qt::Checked : Qt::Unchecked;
  }

  return QVariant();
}

bool PathsModel::setData( const QModelIndex & index, const QVariant &, int role )
{
  if ( index.row() >= paths.size() ) {
    return false;
  }

  if ( role == Qt::CheckStateRole && index.column() == 1 ) {
    paths[ index.row() ].recursive = !paths[ index.row() ].recursive;

    dataChanged( index, index );
    return true;
  }

  return false;
}

SoundDirsModel::SoundDirsModel( QWidget * parent, const Config::SoundDirs & soundDirs_ ):
  QAbstractTableModel( parent ),
  soundDirs( soundDirs_ )
{
}

void SoundDirsModel::removeSoundDir( int index )
{
  beginRemoveRows( QModelIndex(), index, index );
  soundDirs.erase( soundDirs.begin() + index );
  endRemoveRows();
}

void SoundDirsModel::addNewSoundDir( const QString & path, const QString & name )
{
  beginInsertRows( QModelIndex(), soundDirs.size(), soundDirs.size() );
  soundDirs.push_back( Config::SoundDir( path, name ) );
  endInsertRows();
}

void SoundDirsModel::removeSoundDirs( const QList< QModelIndex > & indexes )
{
  beginResetModel();
  QList< qsizetype > rows;
  rows.reserve( indexes.size() );

  for ( auto & i : std::as_const( indexes ) ) {
    rows.push_back( i.row() );
  }

  decltype( soundDirs ) newSoundDirs;
  for ( auto i = 0; i < soundDirs.size(); ++i ) {
    if ( !rows.contains( i ) ) {
      newSoundDirs.push_back( soundDirs[ i ] );
    }
  }
  soundDirs.swap( newSoundDirs );
  endResetModel();
}

Qt::ItemFlags SoundDirsModel::flags( const QModelIndex & index ) const
{
  Qt::ItemFlags result = QAbstractTableModel::flags( index );

  if ( index.isValid() && index.column() < 3 ) {
    result |= Qt::ItemIsEditable;
  }

  return result;
}

int SoundDirsModel::rowCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return soundDirs.size();
  }
}

int SoundDirsModel::columnCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return 3;
  }
}

QVariant SoundDirsModel::headerData( int section, Qt::Orientation, int role ) const
{
  if ( role == Qt::DisplayRole ) {
    switch ( section ) {
      case 0:
        return tr( "Path" );
      case 1:
        return tr( "Name" );
      case 2:
        return tr( "Icon" );
      default:
        return QVariant();
    }
  }

  return QVariant();
}

QVariant SoundDirsModel::data( const QModelIndex & index, int role ) const
{
  if ( index.row() >= soundDirs.size() ) {
    return QVariant();
  }

  if ( ( role == Qt::DisplayRole || role == Qt::EditRole ) && !index.column() ) {
    return soundDirs[ index.row() ].path;
  }

  if ( ( role == Qt::DisplayRole || role == Qt::EditRole ) && index.column() == 1 ) {
    return soundDirs[ index.row() ].name;
  }

  if ( ( role == Qt::DisplayRole || role == Qt::EditRole ) && index.column() == 2 ) {
    return soundDirs[ index.row() ].iconFilename;
  }

  return QVariant();
}

bool SoundDirsModel::setData( const QModelIndex & index, const QVariant & value, int role )
{
  if ( index.row() >= soundDirs.size() ) {
    return false;
  }

  if ( ( role == Qt::DisplayRole || role == Qt::EditRole ) && index.column() < 3 ) {
    if ( !index.column() ) {
      soundDirs[ index.row() ].path = value.toString();
    }
    else if ( index.column() == 1 ) {
      soundDirs[ index.row() ].name = value.toString();
    }
    else {
      soundDirs[ index.row() ].iconFilename = value.toString();
    }

    dataChanged( index, index );
    return true;
  }

  return false;
}

HunspellDictsModel::HunspellDictsModel( QWidget * parent, const Config::Hunspell & hunspell ):
  QAbstractTableModel( parent ),
  enabledDictionaries( hunspell.enabledDictionaries )
{
  changePath( hunspell.dictionariesPath );
}

void HunspellDictsModel::changePath( const QString & newPath )
{
  dataFiles = HunspellMorpho::findDataFiles( newPath );
  beginResetModel();
  endResetModel();
}

Qt::ItemFlags HunspellDictsModel::flags( const QModelIndex & index ) const
{
  Qt::ItemFlags result = QAbstractTableModel::flags( index );

  if ( index.isValid() ) {
    if ( !index.column() ) {
      result |= Qt::ItemIsUserCheckable;
    }
  }

  return result;
}

int HunspellDictsModel::rowCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return dataFiles.size();
  }
}

int HunspellDictsModel::columnCount( const QModelIndex & parent ) const
{
  if ( parent.isValid() ) {
    return 0;
  }
  else {
    return 2;
  }
}

QVariant HunspellDictsModel::headerData( int section, Qt::Orientation, int role ) const
{
  if ( role == Qt::DisplayRole ) {
    switch ( section ) {
      case 0:
        return tr( "Enabled" );
      case 1:
        return tr( "Name" );
      default:
        return QVariant();
    }
  }

  return QVariant();
}

QVariant HunspellDictsModel::data( const QModelIndex & index, int role ) const
{
  if ( (unsigned)index.row() >= dataFiles.size() ) {
    return QVariant();
  }

  if ( role == Qt::DisplayRole && index.column() == 1 ) {
    return dataFiles[ index.row() ].dictName;
  }

  if ( role == Qt::CheckStateRole && !index.column() ) {
    for ( unsigned x = enabledDictionaries.size(); x--; ) {
      if ( enabledDictionaries[ x ] == dataFiles[ index.row() ].dictId ) {
        return Qt::Checked;
      }
    }

    return Qt::Unchecked;
  }

  return QVariant();
}

bool HunspellDictsModel::setData( const QModelIndex & index, const QVariant &, int role )
{
  if ( (unsigned)index.row() >= dataFiles.size() ) {
    return false;
  }

  if ( role == Qt::CheckStateRole && !index.column() ) {
    for ( unsigned x = enabledDictionaries.size(); x--; ) {
      if ( enabledDictionaries[ x ] == dataFiles[ index.row() ].dictId ) {
        enabledDictionaries.erase( enabledDictionaries.begin() + x );
        dataChanged( index, index );
        return true;
      }
    }

    enabledDictionaries.push_back( dataFiles[ index.row() ].dictId );

    dataChanged( index, index );
    return true;
  }

  return false;
}

void Sources::on_rescan_clicked()
{
  emit rescan();
}
