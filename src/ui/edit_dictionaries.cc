/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "edit_dictionaries.hh"
#include "dict/loaddictionaries.hh"
#include "help.hh"
#include <QTabWidget>
#include <QMessageBox>

using std::vector;

EditDictionaries::EditDictionaries( QWidget * parent,
                                    Config::Class & cfg_,
                                    vector< sptr< Dictionary::Class > > & dictionaries_,
                                    Instances::Groups & groupInstances_ ):
  QDialog( parent, Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint ),
  cfg( cfg_ ),
  dictionaries( dictionaries_ ),
  groupInstances( groupInstances_ ),
  sources( this, cfg ),
  orderAndProps( new OrderAndProps( this, cfg.dictionaryOrder, cfg.inactiveDictionaries, dictionaries ) ),
  groups( new Groups( this, dictionaries, cfg.groups, orderAndProps->getCurrentDictionaryOrder() ) ),
  dictionariesChanged( false ),
  groupsChanged( false ),
  helpAction( this )
{
  origGroups.groups               = groups->getGroups();
  origGroups.dictionaryOrder      = orderAndProps->getCurrentDictionaryOrder();
  origGroups.inactiveDictionaries = orderAndProps->getCurrentInactiveDictionaries();

  ui.setupUi( this );

  setWindowIcon( QIcon( ":/icons/dictionary.svg" ) );

  ui.tabs->clear();

  ui.tabs->addTab( &sources, QIcon( ":/icons/sources.png" ), tr( "&Sources" ) );
  ui.tabs->addTab( orderAndProps, QIcon( ":/icons/book.svg" ), tr( "&Dictionaries" ) );
  ui.tabs->addTab( groups, QIcon( ":/icons/bookcase.svg" ), tr( "&Groups" ) );

  connect( ui.buttons, &QDialogButtonBox::clicked, this, &EditDictionaries::buttonBoxClicked );

  connect( &sources, &Sources::rescan, this, &EditDictionaries::rescanSources );

  connect( groups, &Groups::showDictionaryInfo, this, &EditDictionaries::showDictionaryInfo );

  connect( orderAndProps, &OrderAndProps::showDictionaryHeadwords, this, &EditDictionaries::showDictionaryHeadwords );

  helpAction.setShortcut( QKeySequence( "F1" ) );
  helpAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );

  connect( &helpAction, &QAction::triggered, this, [ this ]() {
    if ( ui.tabs->currentWidget() == this->groups ) {
      Help::openHelpWebpage( Help::section::manage_groups );
    }
    else {
      Help::openHelpWebpage( Help::section::manage_sources );
    }
  } );
  connect( ui.buttons, &QDialogButtonBox::helpRequested, &helpAction, &QAction::trigger );

  addAction( &helpAction );

  connect( ui.tabs, &QTabWidget::currentChanged, this, &EditDictionaries::currentChanged );
}

void EditDictionaries::editGroup( unsigned id )
{
  ui.tabs->setTabVisible( 0, false );

  if ( id == GroupId::AllGroupId ) {
    ui.tabs->setCurrentIndex( 1 );
  }
  else {
    ui.tabs->setCurrentIndex( 2 );
    groups->editGroup( id );
  }
}

void EditDictionaries::save( bool rebuildGroups )
{
  const Config::Groups newGroups  = groups->getGroups();
  const Config::Group newOrder    = orderAndProps->getCurrentDictionaryOrder();
  const Config::Group newInactive = orderAndProps->getCurrentInactiveDictionaries();

  if ( isSourcesChanged() ) {
    acceptChangedSources( rebuildGroups );
  }

  if ( origGroups.groups != newGroups || origGroups.dictionaryOrder != newOrder
       || origGroups.inactiveDictionaries != newInactive ) {
    groupsChanged            = true;
    cfg.groups               = newGroups;
    cfg.dictionaryOrder      = newOrder;
    cfg.inactiveDictionaries = newInactive;
  }
}

void EditDictionaries::accept()
{
  save();
  QDialog::accept();
}

void EditDictionaries::currentChanged( int index )
{
  if ( index == -1 || !isVisible() ) {
    return;
  }
  qDebug() << ui.tabs->currentWidget()->objectName();
  if ( lastTabName.isEmpty() || lastTabName == "Sources" ) {

    if ( isSourcesChanged() ) {
      QMessageBox question( QMessageBox::Question,
                            tr( "Sources changed" ),
                            tr( "Some sources were changed. Would you like to accept the changes?" ),
                            QMessageBox::NoButton,
                            this );

      const QPushButton * accept = question.addButton( tr( "Accept" ), QMessageBox::AcceptRole );

      question.addButton( tr( "Cancel" ), QMessageBox::RejectRole );

      question.exec();

      if ( question.clickedButton() == accept ) {
        acceptChangedSources( true );
      }
      else {
      }
    }
  }
  else if ( lastTabName == "OrderAndProps" ) {
    groups->updateDictionaryOrder( orderAndProps->getCurrentDictionaryOrder() );
  }

  lastTabName = ui.tabs->currentWidget()->objectName();
}

void EditDictionaries::rescanSources()
{
  acceptChangedSources( true );
}

void EditDictionaries::buttonBoxClicked( QAbstractButton * button )
{
  if ( ui.buttons->buttonRole( button ) == QDialogButtonBox::ApplyRole ) {
    save( true );
  }
}

bool EditDictionaries::isSourcesChanged() const
{
  return sources.getPaths() != cfg.paths || sources.getSoundDirs() != cfg.soundDirs
    || sources.getHunspell() != cfg.hunspell || sources.getTransliteration() != cfg.transliteration
    || sources.getPrograms() != cfg.programs
    ;
}

void EditDictionaries::acceptChangedSources( bool rebuildGroups )
{
  dictionariesChanged = true;

  Config::Groups savedGroups  = groups->getGroups();
  Config::Group savedOrder    = orderAndProps->getCurrentDictionaryOrder();
  Config::Group savedInactive = orderAndProps->getCurrentInactiveDictionaries();

  cfg.paths           = sources.getPaths();
  cfg.soundDirs       = sources.getSoundDirs();
  cfg.hunspell        = sources.getHunspell();
  cfg.transliteration = sources.getTransliteration();
  cfg.programs        = sources.getPrograms();
  groupInstances.clear();

  loadDictionaries( this, cfg, dictionaries );

  Instances::updateNames( savedGroups, dictionaries );
  Instances::updateNames( savedOrder, dictionaries );
  Instances::updateNames( savedInactive, dictionaries );

  if ( rebuildGroups ) {
    setUpdatesEnabled( false );

    orderAndProps->resetData( savedOrder, savedInactive, dictionaries );
    groups->resetData( dictionaries, savedGroups, orderAndProps->getCurrentDictionaryOrder() );
    setUpdatesEnabled( true );
  }
}
EditDictionaries::~EditDictionaries()
{
  disconnect( ui.tabs, &QTabWidget::currentChanged, this, &EditDictionaries::currentChanged );
}
