#include "keyboardstate.hh"
#include "preferences.hh"
#include "help.hh"
#include <thread>
#include <algorithm>
#include "qtsingleapplication.h"

Preferences::Preferences( QWidget * parent, Config::Class & cfg_ ):
  QDialog( parent ),
  cfg( cfg_ ),
  helpAction( this )
{
  const Config::Preferences & p = cfg_.preferences;
  ui.setupUi( this );

  helpAction.setShortcut( QKeySequence( "F1" ) );
  helpAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );

  connect( &helpAction, &QAction::triggered, this, [ this ]() {
    const auto * currentTab = ui.tabWidget->currentWidget();
    if ( ui.tab_FTS == currentTab ) {
      Help::openHelpWebpage( Help::section::ui_fulltextserch );
    }
    else {
      Help::openHelpWebpage();
    }
  } );
  connect( ui.buttonBox, &QDialogButtonBox::helpRequested, &helpAction, &QAction::trigger );

  addAction( &helpAction );

  ui.newTabsOpenAfterCurrentOne->setChecked( p.newTabsOpenAfterCurrentOne );
  ui.newTabsOpenInBackground->setChecked( p.newTabsOpenInBackground );
  ui.hideSingleTab->setChecked( p.hideSingleTab );
  ui.mruTabOrder->setChecked( p.mruTabOrder );
  ui.enableTrayIcon->setChecked( p.enableTrayIcon );

  ui.startToTray->setChecked( p.startToTray );
  ui.closeToTray->setChecked( p.closeToTray );
  ui.showDockIcon->setChecked( p.showDockIcon );
#ifdef Q_OS_MACOS
  ui.enableTrayIcon->setCheckable( false );
  ui.closeToTray->setChecked( true );
  ui.closeToTray->setEnabled( false );
#else
  ui.showDockIcon->hide();
#endif
  ui.cbAutostart->setChecked( p.autoStart );
  ui.doubleClickTranslates->setChecked( p.doubleClickTranslates );
  ui.selectBySingleClick->setChecked( p.selectWordBySingleClick );
  ui.autoScrollToTargetArticle->setChecked( p.autoScrollToTargetArticle );
  ui.escKeyHidesMainWindow->setChecked( p.escKeyHidesMainWindow );

  ui.enableClipboardHotkey->setChecked( p.enableClipboardHotkey );
  ui.clipboardHotkey->setKeySequence( p.clipboardHotkey );

#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
  ui.clipboardHotkey->setMaximumSequenceLength( 2 );
#endif

  ui.storeHistory->setChecked( p.storeHistory );
  ui.historyMaxSizeField->setValue( p.maxStringsInHistory );
  ui.alwaysExpandOptionalParts->setChecked( p.alwaysExpandOptionalParts );

  ui.favoritesSaveIntervalField->setValue( p.favoritesStoreInterval );
  ui.confirmFavoritesDeletion->setChecked( p.confirmFavoritesDeletion );

  ui.collapseBigArticles->setChecked( p.collapseBigArticles );
  on_collapseBigArticles_toggled( ui.collapseBigArticles->isChecked() );
  ui.articleSizeLimit->setValue( p.articleSizeLimit );

  ui.limitInputPhraseLength->setChecked( p.limitInputPhraseLength );
  on_limitInputPhraseLength_toggled( ui.limitInputPhraseLength->isChecked() );
  ui.inputPhraseLengthLimit->setValue( p.inputPhraseLengthLimit );

  ui.ignoreDiacritics->setChecked( p.ignoreDiacritics );

  ui.ignorePunctuation->setChecked( p.ignorePunctuation );
  ui.sessionCollapse->setChecked( p.sessionCollapse );

  ui.synonymSearchEnabled->setChecked( p.synonymSearchEnabled );

  ui.stripClipboard->setChecked( p.stripClipboard );

  ui.raiseWindowOnSearch->setChecked( p.raiseWindowOnSearch );

  ui.maxDictsInContextMenu->setValue( p.maxDictionaryRefsInContextMenu );

  ui.pronounceOnLoadMain->setChecked( p.pronounceOnLoadMain );

  ui.internalPlayerBackend->addItems( InternalPlayerBackend::availableBackends() );

  ui.useExternalPlayer->setChecked( true );

  if ( ui.internalPlayerBackend->count() > 0 ) {
    ui.useInternalPlayer->setChecked( p.useInternalPlayer );

    int index = ui.internalPlayerBackend->findText( p.internalPlayerBackend.getName() );
    if ( index >= 0 ) {
      ui.internalPlayerBackend->setCurrentIndex( index );
    }
    else {
    }
  }
  else {
    ui.useInternalPlayer->hide();
    ui.internalPlayerBackend->hide();
  }

  ui.audioPlaybackProgram->setText( p.audioPlaybackProgram );

  ui.externalPlayerHint->setText(
    tr( "The audio path will be passed as a command-line argument to the external player. " )
    + R"(<a href="https://dsvi.github.io/MicroDict/ui_audioengine/">)"
    + tr( "View detailed configuration guide" ) + "</a>" );
  ui.externalPlayerHint->setOpenExternalLinks( true );
  QFont hintFont = ui.externalPlayerHint->font();
  hintFont.setPointSize( hintFont.pointSize() - 1 );
  ui.externalPlayerHint->setFont( hintFont );
  QColor hintColor = palette().color( QPalette::Disabled, QPalette::Text );
  ui.externalPlayerHint->setStyleSheet( QString( "color: %1;" ).arg( hintColor.name() ) );

  ui.removeInvalidIndexOnExit->setChecked( p.removeInvalidIndexOnExit );
  ui.enableApplicationLog->setChecked( p.enableApplicationLog );

  ui.ftsGroupBox->setChecked( p.fts.enabled );

  ui.allowAard->setChecked( !p.fts.disabledTypes.contains( "AARD", Qt::CaseInsensitive ) );
  ui.allowBGL->setChecked( !p.fts.disabledTypes.contains( "BGL", Qt::CaseInsensitive ) );
  ui.allowDictD->setChecked( !p.fts.disabledTypes.contains( "DICTD", Qt::CaseInsensitive ) );
  ui.allowDSL->setChecked( !p.fts.disabledTypes.contains( "DSL", Qt::CaseInsensitive ) );
  ui.allowMDict->setChecked( !p.fts.disabledTypes.contains( "MDICT", Qt::CaseInsensitive ) );
  ui.allowSDict->setChecked( !p.fts.disabledTypes.contains( "SDICT", Qt::CaseInsensitive ) );
  ui.allowSlob->setChecked( !p.fts.disabledTypes.contains( "SLOB", Qt::CaseInsensitive ) );
  ui.allowStardict->setChecked( !p.fts.disabledTypes.contains( "STARDICT", Qt::CaseInsensitive ) );
  ui.allowXDXF->setChecked( !p.fts.disabledTypes.contains( "XDXF", Qt::CaseInsensitive ) );
  ui.allowGls->setChecked( !p.fts.disabledTypes.contains( "GLS", Qt::CaseInsensitive ) );

  ui.maxDictionarySize->setValue( p.fts.maxDictionarySize );

  ui.parallelThreads->setMaximum( static_cast<int>( std::max( 1u, std::thread::hardware_concurrency() ) ) );
  ui.parallelThreads->setValue( p.fts.parallelThreads );
}

void Preferences::buildDisabledTypes( QString & disabledTypes, bool is_checked, QString name )
{
  if ( !is_checked ) {
    if ( !disabledTypes.isEmpty() ) {
      disabledTypes += ',';
    }
    disabledTypes += name;
  }
}

Config::Preferences Preferences::getPreferences()
{
  Config::Preferences p;

  p.newTabsOpenAfterCurrentOne = ui.newTabsOpenAfterCurrentOne->isChecked();
  p.newTabsOpenInBackground    = ui.newTabsOpenInBackground->isChecked();
  p.hideSingleTab              = ui.hideSingleTab->isChecked();
  p.mruTabOrder                = ui.mruTabOrder->isChecked();
  p.enableTrayIcon             = ui.enableTrayIcon->isChecked();
  p.startToTray                = ui.startToTray->isChecked();
  p.closeToTray                = ui.closeToTray->isChecked();
  p.showDockIcon               = ui.showDockIcon->isChecked();
#ifdef Q_OS_MACOS
  p.enableTrayIcon = true;
  p.closeToTray    = true;
#endif
  p.autoStart                  = ui.cbAutostart->isChecked();
  p.doubleClickTranslates      = ui.doubleClickTranslates->isChecked();
  p.selectWordBySingleClick    = ui.selectBySingleClick->isChecked();
  p.autoScrollToTargetArticle  = ui.autoScrollToTargetArticle->isChecked();
  p.escKeyHidesMainWindow      = ui.escKeyHidesMainWindow->isChecked();

  p.darkMode       = cfg.preferences.darkMode;
  p.darkReaderMode = cfg.preferences.darkReaderMode;

  p.enableClipboardHotkey  = ui.enableClipboardHotkey->isChecked();
  p.clipboardHotkey        = ui.clipboardHotkey->keySequence();

  p.storeHistory              = ui.storeHistory->isChecked();
  p.maxStringsInHistory       = ui.historyMaxSizeField->text().toUInt();
  p.alwaysExpandOptionalParts = ui.alwaysExpandOptionalParts->isChecked();

  p.favoritesStoreInterval   = ui.favoritesSaveIntervalField->text().toUInt();
  p.confirmFavoritesDeletion = ui.confirmFavoritesDeletion->isChecked();

  p.collapseBigArticles    = ui.collapseBigArticles->isChecked();
  p.articleSizeLimit       = ui.articleSizeLimit->value();
  p.limitInputPhraseLength = ui.limitInputPhraseLength->isChecked();
  p.inputPhraseLengthLimit = ui.inputPhraseLengthLimit->value();
  p.ignoreDiacritics       = ui.ignoreDiacritics->isChecked();
  p.ignorePunctuation      = ui.ignorePunctuation->isChecked();
  p.sessionCollapse        = ui.sessionCollapse->isChecked();
  p.stripClipboard         = ui.stripClipboard->isChecked();
  p.raiseWindowOnSearch    = ui.raiseWindowOnSearch->isChecked();

  p.synonymSearchEnabled = ui.synonymSearchEnabled->isChecked();

  p.maxDictionaryRefsInContextMenu = ui.maxDictsInContextMenu->text().toInt();

  p.pronounceOnLoadMain = ui.pronounceOnLoadMain->isChecked();
  p.useInternalPlayer   = ui.useInternalPlayer->isChecked();
  p.internalPlayerBackend.setName( ui.internalPlayerBackend->currentText() );
  p.audioPlaybackProgram = ui.audioPlaybackProgram->text();

  p.removeInvalidIndexOnExit = ui.removeInvalidIndexOnExit->isChecked();
  p.enableApplicationLog     = ui.enableApplicationLog->isChecked();

  p.fts.enabled           = ui.ftsGroupBox->isChecked();
  p.fts.maxDictionarySize = ui.maxDictionarySize->value();
  p.fts.parallelThreads   = ui.parallelThreads->value();

  buildDisabledTypes( p.fts.disabledTypes, ui.allowAard->isChecked(), "AARD" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowBGL->isChecked(), "BGL" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowDictD->isChecked(), "DICTD" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowDSL->isChecked(), "DSL" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowMDict->isChecked(), "MDICT" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowSDict->isChecked(), "SDICT" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowSlob->isChecked(), "SLOB" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowStardict->isChecked(), "STARDICT" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowXDXF->isChecked(), "XDXF" );
  buildDisabledTypes( p.fts.disabledTypes, ui.allowGls->isChecked(), "GLS" );

  return p;
}

void Preferences::on_enableClipboardHotkey_toggled( bool checked )
{
  ui.clipboardHotkey->setEnabled( checked );
}

void Preferences::on_useExternalPlayer_toggled( bool enabled )
{
  ui.internalPlayerBackend->setEnabled( !enabled );
  ui.audioPlaybackProgram->setEnabled( enabled );
}



void Preferences::on_collapseBigArticles_toggled( bool checked )
{
  ui.articleSizeLimit->setEnabled( checked );
}

void Preferences::on_limitInputPhraseLength_toggled( bool checked )
{
  ui.inputPhraseLengthLimit->setEnabled( checked );
}
