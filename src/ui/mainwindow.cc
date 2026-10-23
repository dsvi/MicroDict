/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "mainwindow.hh"
#include "keyboardstate.hh"
#include "logger.hh"
#include "edit_dictionaries.hh"
#include "dict/loaddictionaries.hh"
#include "preferences.hh"
#include "globalregex.hh"
#include "lite/lite_theme.hh"
#include "about.hh"
#include "mruqmenu.hh"
#include "gestures.hh"
#include "dictheadwords.hh"
#include <QTextStream>
#include <QDir>
#include <QUrl>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QList>
#include <QToolBar>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QRunnable>
#include <QThreadPool>
#include <QStyleFactory>
#include <QStyleHints>
#include <QVersionNumber>

#include "folding.hh"
#include "articlesaver.hh"

#include <set>
#include <map>

#include "dictinfo.hh"
#include "historypanewidget.hh"
#include "utils.hh"
#include "help.hh"
#include <QListWidgetItem>
#include "config.hh"
#include "globalregex.hh"

#ifdef Q_OS_MAC
  #include "macos/mac_app_activation.hh"
#endif

#include <QGuiApplication>
#include <QWindow>
#include <QStandardPaths>
#include <QProxyStyle>

#define MIN_THREAD_COUNT 4

using std::set;
using std::map;
using std::pair;

void MainWindow::updateArticleFonts() const
{
  for ( int i = 0; i < ui.tabWidget->count(); ++i ) {
    if ( auto * view = qobject_cast< ArticleView * >( ui.tabWidget->widget( i ) ) )
      view->applyFontPreferences();
  }
}

MainWindow::MainWindow( Config::Class & cfg_ ):
  trayIcon( nullptr ),
  foundInDictsLabel( &dictsPaneTitleBar ),
  escAction( this ),
  focusTranslateLineAction( this ),
  addTabAction( this ),
  closeCurrentTabAction( this ),
  closeAllTabAction( this ),
  closeRestTabAction( this ),
  switchToNextTabAction( this ),
  switchToPrevTabAction( this ),
  showDictBarNamesAction( tr( "Show Names in Dictionary &Bar" ), this ),
  toggleMenuBarAction( tr( "&Menubar" ), this ),
  lockPanelsAction( tr( "Lock Panels" ), this ),
  focusHeadwordsDlgAction( this ),
  focusArticleViewAction( this ),
  addAllTabToFavoritesAction( this ),
  useSmallIconsInToolbarsAction( tr( "Show &Small Icons in Toolbars" ), this ),
  useLargeIconsInToolbarsAction( tr( "Show &Large Icons in Toolbars" ), this ),
  useNormalIconsInToolbarsAction( tr( "Show &Normal Icons in Toolbars" ), this ),
  stopAudioAction( this ),
  trayIconMenu( this ),
  addTab( this ),
  cfg( cfg_ ),
  history( cfg_.preferences.maxStringsInHistory, cfg_.maxHeadwordSize ),
  dictionaryBar( this, cfg.preferences.maxDictionaryRefsInContextMenu ),
  articleMaker( dictionaries, groupInstances, cfg.preferences ),
  articleResMgr( this, dictionaries, articleMaker ),
  audioPlayerFactory(
    cfg.preferences.useInternalPlayer, cfg.preferences.internalPlayerBackend, cfg.preferences.audioPlaybackProgram ),
  wordFinder( this ),
  wordListSelChanged( false ),
  wasMaximized( false ),
  isQuitting( false ),
  headwordsDlg( nullptr ),
  ftsIndexing( dictionaries ),
  ftsRestartTimer( this ),
  ftsStateChanged( false ),
  ftsDlg( nullptr ),
  starIcon( ":/icons/star.svg" ),
  blueStarIcon( ":/icons/star_blue.svg" )
{
  if ( QThreadPool::globalInstance()->maxThreadCount() < MIN_THREAD_COUNT ) {
    QThreadPool::globalInstance()->setMaxThreadCount( MIN_THREAD_COUNT );
  }

  GlobalBroadcaster::instance()->setConfig( &cfg );
  GlobalBroadcaster::instance()->setAudioPlayer( &audioPlayerFactory.player() );
  GlobalBroadcaster::instance()->setAllDictionaries( &dictionaries );
  GlobalBroadcaster::instance()->setGroups( &groupInstances );

  connect( qApp, &QApplication::aboutToQuit, this, [ this ]() {
    if ( !isQuitting ) {
      performCleanup();
    }
  } );

  ftsRestartTimer.setSingleShot( true );
  ftsRestartTimer.setInterval( 3000 );
  connect( &ftsRestartTimer, &QTimer::timeout, this, [ this ]() {
    if ( ftsStateChanged ) {
      qDebug() << "Restarting FTS indexing after DictInfo closed";
      ftsIndexing.doIndexing();
      ftsStateChanged = false;
    }
  } );

  ui.setupUi( this );

#ifndef Q_OS_MAC
  Gestures::registerRecognizers();
#endif
  setStatusBar( nullptr );
  mainStatusBar = new MainStatusBar( this );

  navToolbar = addToolBar( tr( "&Navigation" ) );
  navToolbar->setObjectName( "navToolbar" );

  navBack = navToolbar->addAction( QIcon( ":/icons/previous.svg" ), tr( "Back" ) );
  navToolbar->widgetForAction( navBack )->setObjectName( "backButton" );
  navForward = navToolbar->addAction( QIcon( ":/icons/next.svg" ), tr( "Forward" ) );
  navToolbar->widgetForAction( navForward )->setObjectName( "forwardButton" );

  QWidget * translateBoxWidget     = new QWidget( this );
  QHBoxLayout * translateBoxLayout = new QHBoxLayout( translateBoxWidget );
  translateBoxWidget->setLayout( translateBoxLayout );
  translateBoxLayout->setContentsMargins( 0, 0, 0, 0 );
  translateBoxLayout->setSpacing( 0 );

  groupListInToolbar = new GroupComboBox( navToolbar );
  groupListInToolbar->setObjectName( "groupListToolbar" );
  groupListInToolbar->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::MinimumExpanding );
  groupListInToolbar->setSizeAdjustPolicy( QComboBox::AdjustToContents );
  translateBoxLayout->addWidget( groupListInToolbar, 0 );

  translateBox = new TranslateBox( navToolbar );
  translateBox->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::MinimumExpanding );
  translateBoxLayout->addWidget( translateBox, 1 );
  translateBoxToolBarAction = navToolbar->addWidget( translateBoxWidget );

  navToolbar->addSeparator();


  navPronounce = navToolbar->addAction( QIcon( ":/icons/playsound_full.png" ), tr( "Pronounce Word (Alt+S)" ) );
  navPronounce->setShortcut( QKeySequence( "Alt+S" ) );
  navPronounce->setEnabled( false );
  navToolbar->widgetForAction( navPronounce )->setObjectName( "soundButton" );

  connect( navPronounce, &QAction::triggered, this, [ this ]() {
    getCurrentArticleView()->playSound();
  } );

  navToolbar->widgetForAction( navToolbar->addSeparator() )->setObjectName( "separatorBeforeZoom" );

  zoomIn = navToolbar->addAction( QIcon( ":/icons/icon32_zoomin.png" ), tr( "Zoom In" ) );
  zoomIn->setShortcuts( QList< QKeySequence >() << QKeySequence::ZoomIn << QKeySequence( "Ctrl+=" ) );
  navToolbar->widgetForAction( zoomIn )->setObjectName( "zoomInButton" );

  zoomOut = navToolbar->addAction( QIcon( ":/icons/icon32_zoomout.png" ), tr( "Zoom Out" ) );
  zoomOut->setShortcut( QKeySequence::ZoomOut );
  navToolbar->widgetForAction( zoomOut )->setObjectName( "zoomOutButton" );

  zoomBase = navToolbar->addAction( QIcon( ":/icons/icon32_zoombase.png" ), tr( "Normal Size" ) );
  zoomBase->setShortcut( QKeySequence( "Ctrl+0" ) );
  navToolbar->widgetForAction( zoomBase )->setObjectName( "zoomBaseButton" );

  navToolbar->widgetForAction( navToolbar->addSeparator() )->setObjectName( "separatorBeforeSave" );

  navToolbar->addAction( ui.saveArticle );
  navToolbar->widgetForAction( ui.saveArticle )->setObjectName( "saveArticleButton" );


  navToolbar->widgetForAction( navToolbar->addSeparator() )->setObjectName( "separatorBeforeAddToFavorites" );

  addToFavorites = navToolbar->addAction( starIcon, tr( "Add current tab to Favorites" ) );
  navToolbar->widgetForAction( addToFavorites )->setObjectName( "addToFavoritesButton" );

  connect( addToFavorites, &QAction::triggered, this, &MainWindow::handleAddToFavoritesButton );
  connect( ui.actionAddToFavorites, &QAction::triggered, this, &MainWindow::addCurrentTabToFavorites );

  beforeOptionsSeparator = navToolbar->addSeparator();
  navToolbar->widgetForAction( beforeOptionsSeparator )->setObjectName( "beforeOptionsSeparator" );
  beforeOptionsSeparator->setVisible( cfg.preferences.hideMenubar );

  ui.rescanFiles->setShortcuts( QList< QKeySequence >() << QKeySequence( "Ctrl+F5" ) << QKeySequence( "F5" ) );

  QMenu * buttonMenu = new QMenu( this );
  buttonMenu->addAction( ui.dictionaries );
  buttonMenu->addAction( ui.preferences );
  buttonMenu->addSeparator();
  buttonMenu->addMenu( ui.menuFavorites );
  buttonMenu->addMenu( ui.menuHistory );
  buttonMenu->addSeparator();
  buttonMenu->addMenu( ui.menuFile );
  buttonMenu->addMenu( ui.menuView );
  buttonMenu->addMenu( ui.menuSearch );
  buttonMenu->addMenu( ui.menu_Help );

  ui.fullTextSearchAction->setEnabled( true );

  menuButton = new QToolButton( navToolbar );
  menuButton->setPopupMode( QToolButton::InstantPopup );
  menuButton->setMenu( buttonMenu );
  menuButton->setIcon( QIcon( ":/icons/menu_button.svg" ) );
  menuButton->setToolTip( tr( "Menu Button" ) );
  menuButton->setObjectName( "menuButton" );
  menuButton->setFocusPolicy( Qt::NoFocus );

  menuButtonAction = navToolbar->addWidget( menuButton );
  menuButtonAction->setVisible( cfg.preferences.hideMenubar );

  groupListInDock = new GroupComboBox( &searchPaneTitleBar );

  searchPaneTitleBarLayout.setContentsMargins( 3, 5, 3, 5 );
  searchPaneTitleBarLayout.addWidget( groupListInDock );
  searchPaneTitleBarLayout.addStretch();

  searchPaneTitleBar.setLayout( &searchPaneTitleBarLayout );

  ui.searchPane->setTitleBarWidget( &searchPaneTitleBar );
  connect( ui.searchPane->toggleViewAction(), &QAction::triggered, this, &MainWindow::updateSearchPaneAndBar );

  if ( cfg.preferences.searchInDock ) {
    groupList     = groupListInDock;
    translateLine = ui.translateLine;
  }
  else {
    groupList     = groupListInToolbar;
    translateLine = translateBox->translateLine();
  }
  connect( &wordFinder, &WordFinder::updated, this, &MainWindow::prefixMatchUpdated );
  connect( &wordFinder, &WordFinder::finished, this, &MainWindow::prefixMatchFinished );


  groupList->setFocusPolicy( Qt::ClickFocus );
  ui.wordList->setFocusPolicy( Qt::ClickFocus );

  wordListDefaultFont      = ui.wordList->font();
  translateLineDefaultFont = translateLine->font();
  groupListDefaultFont     = groupList->font();

  foundInDictsLabel.setText( tr( "Found in Dictionaries:" ) );
  dictsPaneTitleBarLayout.addWidget( &foundInDictsLabel );
  dictsPaneTitleBarLayout.setContentsMargins( 5, 5, 5, 5 );
  dictsPaneTitleBar.setLayout( &dictsPaneTitleBarLayout );
  dictsPaneTitleBar.setObjectName( "dictsPaneTitleBar" );
  ui.dictsPane->setTitleBarWidget( &dictsPaneTitleBar );
  ui.dictsList->setContextMenuPolicy( Qt::CustomContextMenu );

  connect( ui.dictsPane, &QDockWidget::visibilityChanged, this, &MainWindow::dictsPaneVisibilityChanged );

  connect( ui.dictsList, &QListWidget::itemClicked, this, &MainWindow::foundDictsPaneClicked );

  connect( ui.dictsList, &QWidget::customContextMenuRequested, this, &MainWindow::foundDictsContextMenuRequested );

  connect( zoomIn, &QAction::triggered, this, &MainWindow::zoomin );
  connect( zoomOut, &QAction::triggered, this, &MainWindow::zoomout );
  connect( zoomBase, &QAction::triggered, this, &MainWindow::unzoom );

  ui.menuZoom->addAction( zoomIn );
  ui.menuZoom->addAction( zoomOut );
  ui.menuZoom->addAction( zoomBase );

  connect( trayIconMenu.addAction( tr( "Show &Main Window" ) ), &QAction::triggered, this, [ this ] {
    this->toggleMainWindow( true );
  } );

  trayIconMenu.addSeparator();
  connect( trayIconMenu.addAction( tr( "&Quit" ) ), &QAction::triggered, this, &MainWindow::quitApp );

#ifdef Q_OS_MACOS
  connect( dockMenu.addAction( tr( "Show &Main Window" ) ), &QAction::triggered, this, [ this ] {
    this->toggleMainWindow( true );
  } );
#endif

  addGlobalAction( &escAction, [ this ]() {
    handleEsc();
  } );
  escAction.setShortcut( QKeySequence( "Esc" ) );

  addGlobalAction( &focusTranslateLineAction, [ this ]() {
    focusTranslateLine();
  } );
  focusTranslateLineAction.setShortcuts( QList< QKeySequence >()
                                         << QKeySequence( "Alt+D" ) << QKeySequence( "Ctrl+L" ) );

  addGlobalAction( &focusHeadwordsDlgAction, [ this ]() {
    focusHeadwordsDialog();
  } );
  focusHeadwordsDlgAction.setShortcut( QKeySequence( "Ctrl+D" ) );

  addGlobalAction( &focusArticleViewAction, [ this ]() {
    focusArticleView();
  } );
  focusArticleViewAction.setShortcut( QKeySequence( "Ctrl+N" ) );

  addGlobalAction( ui.fullTextSearchAction, [ this ]() {
    showFullTextSearchDialog();
  } );

  addGlobalAction( &stopAudioAction, [ this ]() {
    stopAudio();
  } );
  stopAudioAction.setShortcut( QKeySequence( "Ctrl+Shift+S" ) );

  addTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  addTabAction.setShortcut( QKeySequence( "Ctrl+T" ) );

  tabListMenu = new MRUQMenu( tr( "Opened tabs" ), ui.tabWidget );

  connect( tabListMenu, &MRUQMenu::requestTabChange, ui.tabWidget, &MainTabWidget::setCurrentIndex );

  connect( &addTabAction, &QAction::triggered, this, &MainWindow::addNewTab );

  addAction( &addTabAction );

  closeCurrentTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  closeCurrentTabAction.setShortcut( QKeySequence( "Ctrl+W" ) );
  closeCurrentTabAction.setText( tr( "Close current tab" ) );

  connect( &closeCurrentTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab );

  addAction( &closeCurrentTabAction );

  closeAllTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  closeAllTabAction.setShortcut( QKeySequence( "Ctrl+Shift+W" ) );
  closeAllTabAction.setText( tr( "Close all tabs" ) );

  connect( &closeAllTabAction, &QAction::triggered, this, &MainWindow::closeAllTabs );

  addAction( &closeAllTabAction );

  closeRestTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  closeRestTabAction.setText( tr( "Close all tabs except current" ) );

  connect( &closeRestTabAction, &QAction::triggered, this, &MainWindow::closeRestTabs );

  addAction( &closeRestTabAction );

  switchToNextTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  switchToNextTabAction.setShortcut( QKeySequence( "Ctrl+PgDown" ) );

  connect( &switchToNextTabAction, &QAction::triggered, this, &MainWindow::switchToNextTab );

  addAction( &switchToNextTabAction );

  switchToPrevTabAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );
  switchToPrevTabAction.setShortcut( QKeySequence( "Ctrl+PgUp" ) );

  connect( &switchToPrevTabAction, &QAction::triggered, this, &MainWindow::switchToPrevTab );

  addAction( &switchToPrevTabAction );

  addAllTabToFavoritesAction.setText( tr( "Add all tabs to Favorites" ) );

  connect( &addAllTabToFavoritesAction, &QAction::triggered, this, &MainWindow::addAllTabsToFavorites );

  tabMenu = new QMenu( this );
  tabMenu->addAction( &closeCurrentTabAction );
  tabMenu->addAction( &closeRestTabAction );
  tabMenu->addSeparator();
  tabMenu->addAction( &closeAllTabAction );
  tabMenu->addSeparator();
  tabMenu->addAction( addToFavorites );
  tabMenu->addAction( &addAllTabToFavoritesAction );

  showDictBarNamesAction.setCheckable( true );
  showDictBarNamesAction.setChecked( cfg.showingDictBarNames );

  connect( &showDictBarNamesAction, &QAction::triggered, this, &MainWindow::showDictBarNamesTriggered );

  useSmallIconsInToolbarsAction.setCheckable( true );
  useSmallIconsInToolbarsAction.setChecked( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Small );
  useLargeIconsInToolbarsAction.setCheckable( true );
  useLargeIconsInToolbarsAction.setChecked( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Large );
  useNormalIconsInToolbarsAction.setCheckable( true );
  useNormalIconsInToolbarsAction.setChecked( cfg.usingToolbarsIconSize == Config::ToolbarsIconSize::Normal );

  smallLargeIconGroup->addAction( &useLargeIconsInToolbarsAction );
  smallLargeIconGroup->addAction( &useSmallIconsInToolbarsAction );
  smallLargeIconGroup->addAction( &useNormalIconsInToolbarsAction );

  connect( smallLargeIconGroup, &QActionGroup::triggered, this, &MainWindow::iconSizeActionTriggered );

  toggleMenuBarAction.setCheckable( true );
  toggleMenuBarAction.setChecked( !cfg.preferences.hideMenubar );
  toggleMenuBarAction.setShortcut( QKeySequence( "Ctrl+M" ) );

  connect( &toggleMenuBarAction, &QAction::triggered, this, &MainWindow::toggleMenuBarTriggered );

  ui.menuView->addAction( &toggleMenuBarAction );
  ui.menuView->addSeparator();
  ui.menuView->addAction( ui.searchPane->toggleViewAction() );
  ui.searchPane->toggleViewAction()->setShortcut( QKeySequence( "Ctrl+S" ) );
  ui.menuView->addAction( ui.dictsPane->toggleViewAction() );
  ui.dictsPane->toggleViewAction()->setShortcut( QKeySequence( "Ctrl+R" ) );
  ui.menuView->addAction( ui.favoritesPane->toggleViewAction() );
  ui.menuView->addAction( ui.historyPane->toggleViewAction() );

  ui.menuView->addSeparator();
  ui.menuView->addAction( dictionaryBar.toggleViewAction() );
  ui.menuView->addAction( navToolbar->toggleViewAction() );
  ui.menuView->addSeparator();
  ui.menuView->addAction( &showDictBarNamesAction );
  ui.menuView->addSeparator();
  ui.menuView->addAction( &useSmallIconsInToolbarsAction );
  ui.menuView->addAction( &useNormalIconsInToolbarsAction );
  ui.menuView->addAction( &useLargeIconsInToolbarsAction );
  ui.menuView->addSeparator();
  ui.alwaysOnTop->setChecked( cfg.preferences.alwaysOnTop );
  lockPanelsAction.setCheckable( true );
  lockPanelsAction.setChecked( cfg.preferences.panelsLocked );
  connect( &lockPanelsAction, &QAction::toggled, this, &MainWindow::onLockPanelsToggled );
  ui.menuView->addAction( &lockPanelsAction );
  ui.menuView->addAction( ui.alwaysOnTop );

  const Instances::Group * igrp = groupInstances.findGroup( cfg.lastMainGroupId );
  if ( cfg.lastMainGroupId == GroupId::AllGroupId ) {
    if ( igrp ) {
      igrp->checkMutedDictionaries( &cfg.mutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( &cfg.mutedDictionaries );
  }
  else {
    Config::Group * grp = cfg.getGroup( cfg.lastMainGroupId );
    if ( igrp && grp ) {
      igrp->checkMutedDictionaries( &grp->mutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( grp ? &grp->mutedDictionaries : nullptr );
  }
  GlobalBroadcaster::instance()->currentGroupId = cfg.lastMainGroupId;

  showDictBarNamesTriggered();

  connect( this, &MainWindow::clickOnDictPane, &dictionaryBar, &DictionaryBar::dictsPaneClicked );

  addToolBarBreak();
  addToolBar( &dictionaryBar );

  connect( dictionaryBar.toggleViewAction(), &QAction::triggered, this, &MainWindow::dictionaryBarToggled );
  connect( dictionaryBar.toggleViewAction(), &QAction::toggled, this, &MainWindow::dictionaryBarToggled );

  connect( &dictionaryBar, &DictionaryBar::editGroupRequested, this, &MainWindow::editCurrentGroup );

  connect( &dictionaryBar, &DictionaryBar::showDictionaryInfo, this, &MainWindow::showDictionaryInfo );

  connect( &dictionaryBar, &DictionaryBar::showDictionaryHeadwords, this, &MainWindow::showDictionaryHeadwords );

  connect( &dictionaryBar, &DictionaryBar::openDictionaryFolder, this, &MainWindow::openDictionaryFolder );
  connect( &dictionaryBar,
           &DictionaryBar::showStatusBarMessage,
           this,
           [ this ]( const QString & message, int timeout ) {
             showStatusBarMessage( message, timeout );
           } );

  ui.favoritesPaneWidget->setUp( &cfg, { ui.showHideFavorites, ui.importFavorites, ui.exportFavorites } );
  ui.favoritesPaneWidget->setSaveInterval( cfg.preferences.favoritesStoreInterval );

  connect( ui.favoritesPane, &QDockWidget::visibilityChanged, this, &MainWindow::updateFavoritesMenu );
  connect( ui.showHideFavorites, &QAction::triggered, this, &MainWindow::toggle_favoritesPane );

  connect( ui.favoritesPaneWidget,
           &FavoritesPaneWidget::favoritesItemRequested,
           this,
           &MainWindow::headwordFromFavorites );

  connect( ui.favoritesPaneWidget, &FavoritesPaneWidget::activeFavChange, this, &MainWindow::updateFavIconSlot );

  GlobalBroadcaster::instance()->isWordPresentedInFavorites = [ this ]( const QString & word ) {
    return this->ui.favoritesPaneWidget->isWordPresentInActiveFolder( word );
  };

  ui.historyPaneWidget->setUp( &cfg, &history, ui.menuHistory );
  history.enableAdd( cfg.preferences.storeHistory );

  connect( ui.historyPaneWidget, &HistoryPaneWidget::historyItemRequested, this, &MainWindow::showHistoryItem );

  connect( ui.historyPane, &QDockWidget::visibilityChanged, this, &MainWindow::updateHistoryMenu );
  connect( ui.showHideHistory, &QAction::triggered, this, &MainWindow::toggle_historyPane );

  connect( navBack, &QAction::triggered, this, &MainWindow::backClicked );
  connect( navForward, &QAction::triggered, this, &MainWindow::forwardClicked );

  addTab.setAutoRaise( true );
  addTab.setToolTip( tr( "New Tab" ) );
  addTab.setFocusPolicy( Qt::NoFocus );
  addTab.setIcon( QIcon( ":/icons/addtab.svg" ) );

  ui.tabWidget->setHideSingleTab( cfg.preferences.hideSingleTab );
  ui.tabWidget->clear();

  ui.tabWidget->setCornerWidget( &addTab, Qt::TopLeftCorner );

  ui.tabWidget->setMovable( true );

  ui.tabWidget->setDocumentMode( true );

  ui.tabWidget->setContextMenuPolicy( Qt::CustomContextMenu );

  connect( &addTab, &QAbstractButton::clicked, this, &MainWindow::addNewTab );

  connect( ui.tabWidget, &MainTabWidget::tabBarDoubleClicked, this, [ this ]( const int index ) {
    if ( -1 == index ) {
      this->addNewTab();
    }
  } );

  connect( ui.tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::tabCloseRequested );

  connect( ui.tabWidget, &QTabWidget::currentChanged, this, &MainWindow::tabSwitched );

  connect( ui.tabWidget, &QWidget::customContextMenuRequested, this, &MainWindow::tabMenuRequested );

  ui.tabWidget->setTabsClosable( true );

  connect( ui.quit, &QAction::triggered, this, &MainWindow::quitApp );

  connect( ui.dictionaries, &QAction::triggered, this, &MainWindow::editDictionaries );

  connect( ui.preferences, &QAction::triggered, this, &MainWindow::editPreferences );

  connect( ui.visitHomepage, &QAction::triggered, this, &MainWindow::visitHomepage );
  connect( ui.visitForum, &QAction::triggered, this, &MainWindow::visitForum );
  connect( ui.openConfigFolder, &QAction::triggered, this, &MainWindow::openConfigFolder );
  connect( ui.about, &QAction::triggered, this, &MainWindow::showAbout );
  connect( ui.showReference, &QAction::triggered, this, []() {
    Help::openHelpWebpage();
  } );

  connect( groupListInDock, &GroupComboBox::currentIndexChanged, this, &MainWindow::currentGroupChanged );

  connect( groupListInToolbar, &GroupComboBox::currentIndexChanged, this, &MainWindow::currentGroupChanged );

  connect( ui.translateLine, &QLineEdit::textChanged, this, &MainWindow::translateInputChanged );

  connect( translateBox->translateLine(), &QLineEdit::textEdited, this, &MainWindow::translateInputChanged );

  connect( ui.translateLine, &QLineEdit::returnPressed, this, [ this ]() {
    translateInputFinished( true );
  } );
  connect( translateBox, &TranslateBox::returnPressed, this, [ this ]() {
    translateInputFinished( true );
  } );

  connect( ui.wordList, &QListWidget::itemSelectionChanged, this, &MainWindow::wordListSelectionChanged );

  connect( ui.wordList, &QListWidget::itemClicked, this, &MainWindow::wordListItemActivated );

  connect( GlobalBroadcaster::instance(),
           &GlobalBroadcaster::mutedDictionariesChanged,
           this,
           &MainWindow::mutedDictionariesChanged );

  this->installEventFilter( this );

  ui.translateLine->installEventFilter( this );
  translateBox->translateLine()->installEventFilter( this );

  ui.wordList->installEventFilter( this );

  ui.wordList->viewport()->installEventFilter( this );

  ui.dictsList->installEventFilter( this );
  ui.dictsList->viewport()->installEventFilter( this );
  ui.tabWidget->installEventFilter( this );

  ui.historyList->installEventFilter( this );

  ui.favoritesTree->installEventFilter( this );

  groupListInDock->installEventFilter( this );
  groupListInToolbar->installEventFilter( this );

  connect( &ftsIndexing, &FTS::FtsIndexing::newIndexingName, this, &MainWindow::showFTSIndexingName );
  connect( GlobalBroadcaster::instance(),
           &GlobalBroadcaster::indexingDictionary,
           this,
           &MainWindow::showFTSIndexingName );

  connect( GlobalBroadcaster::instance(), &GlobalBroadcaster::stopFtsIndexing, this, [ this ]() {
    qDebug() << "FTS state changed, stopping indexing and scheduling restart";
    ftsIndexing.stopIndexing();
    ftsStateChanged = true;
    ftsRestartTimer.start();
  } );

  connect( GlobalBroadcaster::instance(), &GlobalBroadcaster::startFtsIndexing, this, [ this ]() {
    qDebug() << "Direct FTS indexing start requested";
    ftsStateChanged = false;
    ftsRestartTimer.stop();
    ftsIndexing.doIndexing();
  } );

  connect( &GlobalBroadcaster::instance()->pronounce_engine,
           &PronounceEngine::emitAudio,
           this,
           [ this ]( auto audioUrl ) {
             auto view = getCurrentArticleView();
             if ( view == nullptr ) {
               return;
             }
             view->setAudioLink( audioUrl );
             if ( !isActiveWindow() ) {
               return;
             }
             if ( cfg.preferences.pronounceOnLoadMain ) {
               QTimer::singleShot( 150, view, [ view, audioUrl ]() {
                 view->playAudio( QUrl::fromEncoded( audioUrl.toUtf8() ) );
               } );
             }
           } );
  updateArticleFonts();

  makeDictionaries();

  setWindowTitle( "MicroDict" );

  createTabList();

  updateAppearances();

  history.enableAdd( false );
  createNewTab( true, ArticleMaker::welcomeWord() )->load( QUrl( "gdinternal://welcome-page" ) );
  history.enableAdd( cfg.preferences.storeHistory );

  if ( cfg.mainWindowState.size() && !cfg.resetState ) {
    restoreState( cfg.mainWindowState );
  }
  if ( cfg.mainWindowGeometry.size() ) {
    restoreGeometry( cfg.mainWindowGeometry );
  }

  if ( !cfg.preferences.enableTrayIcon || !cfg.preferences.startToTray ) {
    show();
    focusTranslateLine();
#ifdef Q_OS_MACOS
    QTimer::singleShot( 0, this, []() { MacAppActivation::activate(); } );
#endif
  }
#ifdef Q_OS_MACOS
  else {
    MacAppActivation::setDockIconVisible( cfg.preferences.showDockIcon );
  }
#endif


  updateSearchPaneAndBar( cfg.preferences.searchInDock );
  ui.searchPane->setVisible( cfg.preferences.searchInDock );

  QTimer::singleShot( 1000, this, &MainWindow::trayIconUpdateOrInit );

  adjustCurrentZoomFactor();
  scaleArticlesByCurrentZoomFactor();

  setAutostart( cfg.preferences.autoStart );

  QTimer::singleShot( 2000, this, &MainWindow::installHotKeys );

  if ( cfg.preferences.alwaysOnTop ) {
    on_alwaysOnTop_triggered( true );
  }
  onLockPanelsToggled( cfg.preferences.panelsLocked );

  if ( cfg.preferences.hideMenubar ) {
    toggleMenuBarTriggered( false );
  }

  QTimer::singleShot( 3000, this, [ this ]() {
    doDeferredInit( dictionaries );
  } );

  updateStatusLine();


  wasMaximized = isMaximized();

#ifndef Q_OS_MACOS
  ui.centralWidget->grabGesture( Gestures::GDPinchGestureType );
  ui.centralWidget->grabGesture( Gestures::GDSwipeGestureType );
#endif

  if ( layoutDirection() == Qt::RightToLeft ) {
    navBack->setIcon( QIcon( ":/icons/next.svg" ) );
    navForward->setIcon( QIcon( ":/icons/previous.svg" ) );
  }


  iconSizeActionTriggered( nullptr );

  if ( Config::followsDesktopTheme( cfg.preferences.darkMode ) ) {
    connect( &SystemAppearance::instance(),
             &SystemAppearance::changed,
             this,
             &MainWindow::refreshAppearances,
             Qt::UniqueConnection );
  }

#ifdef Q_OS_MACOS
  connect( qApp, &QApplication::applicationStateChanged, this, &MainWindow::handleApplicationStateChanged );
#endif
}



void MainWindow::prefixMatchUpdated()
{
  updateMatchResults( false );
}

void MainWindow::prefixMatchFinished()
{
  updateMatchResults( true );
}

void MainWindow::updateMatchResults( bool finished )
{
  const WordFinder::SearchResults & results = wordFinder.getResults();

  if ( cfg.preferences.searchInDock ) {
    ui.wordList->setUpdatesEnabled( false );
    ui.wordList->clear();

    for ( const auto & result : results ) {
      auto i = new QListWidgetItem( result.first, ui.wordList );
      i->setToolTip( result.first );

      if ( result.second ) {
        QFont f = i->font();
        f.setItalic( true );
        i->setFont( f );
      }

      i->setTextAlignment( Qt::AlignLeft );
    }

    if ( ui.wordList->count() ) {
      ui.wordList->scrollToItem( ui.wordList->item( 0 ), QAbstractItemView::PositionAtTop );
      ui.wordList->setCurrentItem( nullptr, QItemSelectionModel::Clear );
    }

    ui.wordList->setUpdatesEnabled( true );
    ui.wordList->unsetCursor();
  }
  else {

    QStringList _results;
    _results.clear();

    for ( const auto & result : results ) {
      _results << result.first;
    }
    translateBox->setModel( _results );
  }

  if ( finished ) {

    refreshTranslateLine();

    if ( !wordFinder.getErrorString().isEmpty() ) {
      emit showStatusBarMessage( tr( "WARNING: %1" ).arg( wordFinder.getErrorString() ),
                                 20000,
                                 QPixmap( ":/icons/error.svg" ) );
    }
  }
}

void MainWindow::refreshTranslateLine()
{
  if ( !translateLine ) {
    return;
  }

  bool setMark = wordFinder.getResults().empty() && !wordFinder.wasSearchUncertain();
  Utils::Widget::setNoResultColor( translateLine, setMark );
  if ( !cfg.preferences.searchInDock ) {
    translateBox->setNoResults( setMark );
  }
}


void MainWindow::ctrlTabPressed()
{
  emit fillWindowsMenu();
  tabListButton->click();
}

void MainWindow::updateSearchPaneAndBar( bool searchInDock )
{
  QString text = translateLine->text();

  if ( ftsDlg ) {
    removeGroupComboBoxActionsFromDialog( ftsDlg, groupList );
  }
  if ( headwordsDlg ) {
    removeGroupComboBoxActionsFromDialog( headwordsDlg, groupList );
  }

  if ( searchInDock ) {
    cfg.preferences.searchInDock = true;

    navToolbar->setAllowedAreas( Qt::AllToolBarAreas );

    groupList     = groupListInDock;
    translateLine = ui.translateLine;

    translateBoxToolBarAction->setVisible( false );

    translateBox->setPopupEnabled( false );
  }
  else {
    cfg.preferences.searchInDock = false;

    navToolbar->setAllowedAreas( Qt::BottomToolBarArea | Qt::TopToolBarArea );
    if ( toolBarArea( navToolbar ) & ( Qt::LeftToolBarArea | Qt::RightToolBarArea ) ) {
      if ( toolBarArea( &dictionaryBar ) == Qt::TopToolBarArea ) {
        insertToolBar( &dictionaryBar, navToolbar );
      }
      else {
        addToolBar( Qt::TopToolBarArea, navToolbar );
      }
    }

    groupList     = groupListInToolbar;
    translateLine = translateBox->translateLine();

    translateBoxToolBarAction->setVisible( true );
  }

  if ( ftsDlg ) {
    addGroupComboBoxActionsToDialog( ftsDlg, groupList );
  }
  if ( headwordsDlg ) {
    addGroupComboBoxActionsToDialog( headwordsDlg, groupList );
  }

  translateLine->setToolTip( tr(
    "String to search in dictionaries. The wildcards '*', '?' and sets of symbols '[...]' are allowed.\nTo find '*', '?', '[', ']' symbols use '\\*', '\\?', '\\[', '\\]' respectively" ) );

  wordListSelChanged = false;

  updateGroupList( false );

  setInputLineText( text, WildcardPolicy::WildcardsAreAlreadyEscaped, DisablePopup );
  focusTranslateLine();
}

void MainWindow::mousePressEvent( QMouseEvent * event )
{
  if ( handleBackForwardMouseButtons( event ) ) {
    return;
  }

  if ( event->button() != Qt::MiddleButton ) {
    return QMainWindow::mousePressEvent( event );
  }

  setInputLineText( clipboardTextForLookup(), WildcardPolicy::EscapeWildcards, NoPopupChange );

  QKeyEvent ev( QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier );
  QApplication::sendEvent( translateLine, &ev );
}

MainWindow::~MainWindow()
{
  closeHeadwordsDialog();

  ftsIndexing.stopIndexing();
#ifndef Q_OS_MACOS
  ui.centralWidget->ungrabGesture( Gestures::GDPinchGestureType );
  ui.centralWidget->ungrabGesture( Gestures::GDSwipeGestureType );
#endif

  while ( ui.tabWidget->count() ) {
    QWidget * w = ui.tabWidget->widget( 0 );

    ui.tabWidget->removeTab( 0 );

    delete w;
  }


  delete ui.historyPaneWidget;
}

void MainWindow::addGlobalAction( QAction * action, const std::function< void() > & slotFunc )
{
  action->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  connect( action, &QAction::triggered, this, slotFunc );

  ui.centralWidget->addAction( action );
  ui.dictsPane->addAction( action );
  ui.searchPaneWidget->addAction( action );
  ui.favoritesPane->addAction( action );
  ui.historyPane->addAction( action );
  groupList->addAction( action );
  translateBox->addAction( action );
}

void MainWindow::addGlobalActionsToDialog( QDialog * dialog )
{
  dialog->addAction( &focusTranslateLineAction );
  dialog->addAction( &focusHeadwordsDlgAction );
  dialog->addAction( &focusArticleViewAction );
  dialog->addAction( ui.fullTextSearchAction );
  dialog->addAction( ui.quit );
}

void MainWindow::addGroupComboBoxActionsToDialog( QDialog * dialog, GroupComboBox * pGroupComboBox )
{
  dialog->addActions( pGroupComboBox->getExternActions() );
}

void MainWindow::removeGroupComboBoxActionsFromDialog( QDialog * dialog, GroupComboBox * pGroupComboBox )
{
  QList< QAction * > actions = pGroupComboBox->getExternActions();
  for ( QList< QAction * >::iterator it = actions.begin(); it != actions.end(); ++it ) {
    dialog->removeAction( *it );
  }
}

void MainWindow::commitData()
{
  isQuitting = true;

  if ( cfg.preferences.removeInvalidIndexOnExit && !dictMap.isEmpty() ) {
    const QDir dir( Config::getIndexDir() );

    const QFileInfoList entries = dir.entryInfoList( QDir::Files | QDir::NoDotAndDotDot );

    for ( auto & file : entries ) {
      const QString fileName = file.fileName();

      if ( dictMap.contains( fileName.toStdString() ) ) {
        continue;
      }
      auto filePath = file.absoluteFilePath();
      qDebug() << "remove invalid index files & fts dirs";

      QFile::remove( filePath );

      QStringList ftsSuffixes; // NOLINT(readability-identifier-naming)
      ftsSuffixes << QString::fromStdString( Dictionary::getFtsSuffix() );
      for ( const auto & old : Dictionary::getObsoleteFtsSuffixes() ) {
        ftsSuffixes << QString::fromStdString( old );
      }

      for ( const auto & suffix : std::as_const( ftsSuffixes ) ) {
        const QStringList dirs { filePath + suffix, filePath + suffix + "_temp" };
        for ( const auto & dirName : dirs ) {
          QDir d( dirName );
          if ( d.exists() ) {
            d.removeRecursively();
          }
        }
      }
    }

    const QFileInfoList dirs = dir.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot );

    for ( auto & file : dirs ) {
      const QString fileName = file.fileName();

      if ( !fileName.endsWith( "_temp" ) ) {
        continue;
      }

      const QFileInfo info( fileName );
      const QDateTime lastModified = info.lastModified();

      if ( lastModified.addDays( 7 ) > QDateTime::currentDateTime() ) {
        continue;
      }
      QDir d( fileName );
      d.removeRecursively();
    }
  }


  try {
    cfg.mainWindowState    = saveState();
    cfg.mainWindowGeometry = saveGeometry();

    try {
      Config::save( cfg );
    }
    catch ( std::exception & e ) {
      qWarning( "Configuration saving failed, error: %s", e.what() );
    }

    history.save();

    ui.favoritesPaneWidget->saveData();
  }
  catch ( std::exception & e ) {
    qWarning( "Commit data failed, error: %s", e.what() );
  }
}

void MainWindow::performCleanup()
{
  isQuitting = true;

  HotkeyWrapper::releaseDesktopShortcuts();
  hotkeyWrapper.reset();

  for ( auto viewer : findChildren< QMainWindow * >( "ResourceViewer" ) ) {
    viewer->close();
  }

  commitData();
}


void MainWindow::updateAppearances()
{
#if defined( Q_OS_MAC )
  if ( QStyle * style = QStyleFactory::create( "Fusion" ) ) {
    QApplication::setStyle( style );
  }
#endif

  const bool isDark = SystemAppearance::instance().isDark();

  qInfo() << "Interface appearance:" << ( isDark ? "dark" : "light" ) << "with style"
          << qApp->style()->name() << "-" << SystemAppearance::instance().describe()
          << "- palette" << ( SystemAppearance::instance().publishesColors()
                                ? "read from the desktop"
                                : "Qt's own, the desktop publishes none" )
          << "Window" << qApp->palette().color( QPalette::Window ).name()
          << "Base" << qApp->palette().color( QPalette::Base ).name()
          << "Text" << qApp->palette().color( QPalette::Text ).name()
          << "Highlight" << qApp->palette().color( QPalette::Highlight ).name();
}

void MainWindow::refreshAppearances()
{
  updateAppearances();

  if ( trayIcon ) {
    trayIconUpdateOrInit();
  }
}

void MainWindow::trayIconUpdateOrInit()
{
#ifdef Q_OS_MACOS
  dockMenu.setAsDockMenu();
#endif

  if ( !cfg.preferences.enableTrayIcon ) {
    if ( trayIcon ) {
      delete trayIcon;
      trayIcon = nullptr;
    }
    ui.actionCloseToTray->setVisible( false );
  }
  else {
    // The tray shows the application's own icon -- the same picture the window, the
    // menu entry and the About box carry. It is a coloured image, so it reads on a
    // light panel and on a dark one alike; there is no variant to pick here.
    // The theme name is the one the desktop entry's Icon= and the installed hicolor
    // icon use, so a panel that gets the icon over the StatusNotifier bus can load
    // it itself at its own size; the resource is what a build that installed no icon
    // falls back to.
    QIcon icon = QIcon::fromTheme( "microdict", QIcon( ":/icons/programicon.png" ) );

    if ( !trayIcon ) {
      trayIcon = new QSystemTrayIcon( this );
      trayIcon->setContextMenu( &trayIconMenu );
      trayIcon->setToolTip( QApplication::applicationName() );
      connect( trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayIconActivated );
    }

    trayIcon->setIcon( icon );
    trayIcon->show();

    ui.actionCloseToTray->setVisible( true );
  }
}

void MainWindow::wheelEvent( QWheelEvent * ev )
{
  if ( ev->modifiers().testFlag( Qt::ControlModifier ) ) {
    if ( ev->angleDelta().y() > 0 ) {
      zoomin();
    }
    else if ( ev->angleDelta().y() < 0 ) {
      zoomout();
    }
    ev->accept();
  }
  else {
    ev->ignore();
  }
}

void MainWindow::closeEvent( QCloseEvent * ev )
{
  if ( isQuitting ) {
    ev->accept();
    return;
  }

  if ( !cfg.preferences.enableTrayIcon || !cfg.preferences.closeToTray ) {
    ev->accept();
    quitApp();
    return;
  }

  if ( !cfg.preferences.searchInDock && translateBox ) {
    translateBox->setPopupEnabled( false );
  }

#ifdef Q_OS_MACOS
  if ( !ev->spontaneous() || !isVisible() ) {
    return;
  }
#endif

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
#else
  if ( trayIcon ) {
    trayIcon->showMessage(
      QApplication::applicationName(),
      tr( "Application is still running in the background. Click the tray icon to show the window." ),
      QSystemTrayIcon::Information,
      3000 );
  }
  ev->ignore();
  hide();
#endif
}

void MainWindow::hideEvent( QHideEvent * event )
{
  QMainWindow::hideEvent( event );
#ifdef Q_OS_MACOS
  if ( !cfg.preferences.showDockIcon ) {
    MacAppActivation::setDockIconVisible( false );
  }
#endif
}

void MainWindow::quitApp()
{
  performCleanup();
  qApp->quit();
}


void MainWindow::makeDictionaries()
{

  wordFinder.clear();

  dictionariesUnmuted.clear();

  ftsIndexing.stopIndexing();
  ftsIndexing.clearDictionaries();

  loadDictionaries( this, cfg, dictionaries, false );

  dictMap = Dictionary::dictToMap( dictionaries );

  for ( unsigned x = 0; x < dictionaries.size(); x++ ) {
    dictionaries[ x ]->setFTSParameters( cfg.preferences.fts );
    dictionaries[ x ]->setSynonymSearchEnabled( cfg.preferences.synonymSearchEnabled );
  }

  ftsIndexing.setDictionaries( dictionaries );
  ftsIndexing.doIndexing();

  updateStatusLine();
  updateGroupList( false );
}

void MainWindow::updateStatusLine()
{
  unsigned articleCount = 0, wordCount = 0;

  for ( unsigned x = dictionaries.size(); x--; ) {
    articleCount += dictionaries[ x ]->getArticleCount();
    wordCount += dictionaries[ x ]->getWordCount();
  }

  mainStatusBar->showMessage(
    tr( "%1 dictionaries, %2 articles, %3 words" ).arg( dictionaries.size() ).arg( articleCount ).arg( wordCount ),
    10000 );
}

void MainWindow::updateGroupList( bool reload )
{
  bool haveGroups = !cfg.groups.empty();

  groupList->setVisible( haveGroups );

  disconnect( groupList, &GroupComboBox::currentIndexChanged, this, &MainWindow::currentGroupChanged );

  groupInstances.clear();

  {
    Instances::Group g( cfg.dictionaryOrder, dictionaries, Config::Group() );

    Instances::complementDictionaryOrder( g,
                                          Instances::Group( cfg.inactiveDictionaries, dictionaries, Config::Group() ),
                                          dictionaries );

    g.name = tr( "All" );
    g.id   = GroupId::AllGroupId;
    g.icon = "folder.png";

    groupInstances.push_back( g );
  }

  for ( auto & group : cfg.groups ) {
    groupInstances.push_back( Instances::Group( group, dictionaries, cfg.inactiveDictionaries ) );
  }

  Instances::updateNames( cfg, dictionaries );

  groupList->fill( groupInstances );
  groupList->setCurrentGroup( cfg.lastMainGroupId );

  dictionaryBar.updateToGroup( groupInstances.findGroup( groupList->getCurrentGroup() ), &cfg.mutedDictionaries, cfg );

  if ( reload ) {
    qDebug() << "Reloading all the tabs...";

    for ( int i = 0; i < ui.tabWidget->count(); ++i ) {
      auto & view = dynamic_cast< ArticleView & >( *( ui.tabWidget->widget( i ) ) );

      view.reload();
    }
  }

  connect( groupList, &GroupComboBox::currentIndexChanged, this, &MainWindow::currentGroupChanged );
}

const vector< sptr< Dictionary::Class > > & MainWindow::getActiveDicts()
{
  if ( groupInstances.empty() ) {
    return dictionaries;
  }

  int current = groupList->currentIndex();

  if ( current < 0 || current >= (int)groupInstances.size() ) {
    return dictionaries;
  }

  const QSet< QString > * mutedDictionaries = dictionaryBar.getMutedDictionaries();
  if ( !dictionaryBar.toggleViewAction()->isChecked() || mutedDictionaries == nullptr ) {
    return groupInstances[ current ].dictionaries;
  }
  else {
    const vector< sptr< Dictionary::Class > > & activeDicts = groupInstances[ current ].dictionaries;

    dictionariesUnmuted.clear();
    dictionariesUnmuted.reserve( activeDicts.size() );

    for ( unsigned x = 0; x < activeDicts.size(); ++x ) {
      if ( !mutedDictionaries->contains( QString::fromStdString( activeDicts[ x ]->getId() ) ) ) {
        dictionariesUnmuted.push_back( activeDicts[ x ] );
      }
    }

    return dictionariesUnmuted;
  }
}

void MainWindow::createTabList()
{
  tabListMenu->setIcon( QIcon( ":/icons/windows-list.svg" ) );
  connect( tabListMenu, &QMenu::aboutToShow, this, &MainWindow::fillWindowsMenu );
  connect( tabListMenu, &QMenu::triggered, this, &MainWindow::switchToWindow );

  tabListButton = new QToolButton( ui.tabWidget );
  tabListButton->setAutoRaise( true );
  tabListButton->setIcon( tabListMenu->icon() );
  tabListButton->setMenu( tabListMenu );
  tabListButton->setToolTip( tr( "Open Tabs List" ) );
  tabListButton->setPopupMode( QToolButton::InstantPopup );
  ui.tabWidget->setCornerWidget( tabListButton );
  tabListButton->setFocusPolicy( Qt::NoFocus );
}

void MainWindow::fillWindowsMenu()
{
  tabListMenu->clear();

  if ( cfg.preferences.mruTabOrder ) {
    for ( int i = 0; i < mruList.count(); i++ ) {
      QAction * act = tabListMenu->addAction( ui.tabWidget->tabIcon( ui.tabWidget->indexOf( mruList.at( i ) ) ),
                                              ui.tabWidget->tabText( ui.tabWidget->indexOf( mruList.at( i ) ) ) );

      act->setData( ui.tabWidget->indexOf( mruList.at( i ) ) );

      if ( ui.tabWidget->currentIndex() == ui.tabWidget->indexOf( mruList.at( i ) ) ) {
        QFont f( act->font() );
        f.setBold( true );
        act->setFont( f );
      }
    }
    if ( tabListMenu->actions().size() > 1 ) {
      tabListMenu->setActiveAction( tabListMenu->actions().at( 1 ) );
    }
  }
  else {
    for ( int i = 0; i < ui.tabWidget->count(); i++ ) {
      QAction * act = tabListMenu->addAction( ui.tabWidget->tabIcon( i ), ui.tabWidget->tabText( i ) );
      act->setData( i );
      if ( ui.tabWidget->currentIndex() == i ) {
        QFont f( act->font() );
        f.setBold( true );
        act->setFont( f );
      }
    }
  }
}

void MainWindow::switchToWindow( QAction * act )
{
  int idx = act->data().toInt();
  ui.tabWidget->setCurrentIndex( idx );
}

void MainWindow::addNewTab()
{
  createNewTab( true, tr( "(untitled)" ) )->load( QUrl( "gdinternal://untitle-page" ) );
}

ArticleView * MainWindow::createNewTab( bool switchToIt, const QString & name )
{
  ArticleView * view = new ArticleView( this,
                                        articleResMgr,
                                        cfg,
                                        translateLine,
                                        dictionaryBar.toggleViewAction(),
                                        groupList->getCurrentGroup() );

  connect( view, &ArticleView::titleChanged, this, &MainWindow::titleChanged );

  connect( view, &ArticleView::pageLoaded, this, &MainWindow::pageLoaded );

  connect( view, &ArticleView::updateFoundInDictsList, this, &MainWindow::updateFoundInDictsList );

  connect( view, &ArticleView::openLinkInNewTab, this, &MainWindow::openLinkInNewTab );

  connect( view, &ArticleView::showDefinitionInNewTab, this, &MainWindow::showDefinitionInNewTab );

  connect( view, &ArticleView::typingEvent, this, &MainWindow::typingEvent );

  connect( view, &ArticleView::activeArticleChanged, this, &MainWindow::activeArticleChanged );

  connect( view, &ArticleView::statusBarMessage, this, &MainWindow::showStatusBarMessage );

  connect( view, &ArticleView::showDictsPane, this, &MainWindow::showDictsPane );

  connect( view, &ArticleView::forceAddWordToHistory, this, &MainWindow::forceAddWordToHistory );

  connect( view, &ArticleView::sendWordToHistory, this, &MainWindow::addWordToHistory );

  connect( view, &ArticleView::sendWordToInputLine, this, &MainWindow::sendWordToInputLine );

  connect( view, &ArticleView::storeResourceSavePath, this, &MainWindow::storeResourceSavePath );

  connect( view, &ArticleView::zoomIn, this, &MainWindow::zoomin );

  connect( view, &ArticleView::zoomOut, this, &MainWindow::zoomout );
  connect( view, &ArticleView::saveBookmarkSignal, this, &MainWindow::addBookmarkToFavorite );
  connect( view, &ArticleView::translateSelectedText, this, &MainWindow::handleTranslateSelectedText );

  connect( ui.searchInPageAction, &QAction::triggered, view, [ this, view ]() {
    view->openSearch();
  } );

  view->setSelectionBySingleClick( cfg.preferences.selectWordBySingleClick );

  int index = cfg.preferences.newTabsOpenAfterCurrentOne ? ui.tabWidget->currentIndex() + 1 : ui.tabWidget->count();

  QString escaped = Utils::escapeAmps( name );

  ui.tabWidget->insertTab( index, view, escaped );
  mruList.append( dynamic_cast< QWidget * >( view ) );

  if ( switchToIt ) {
    ui.tabWidget->setCurrentIndex( index );
  }

  view->setZoomFactor( cfg.preferences.zoomFactor );

  return view;
}

void MainWindow::tabCloseRequested( int x )
{
  QWidget * w = ui.tabWidget->widget( x );

  mruList.removeOne( w );

  if ( cfg.preferences.mruTabOrder && !mruList.empty() ) {
    ui.tabWidget->setCurrentWidget( mruList.at( 0 ) );
  }
  else if ( ui.tabWidget->count() > 1 ) {
    int n = x >= ui.tabWidget->count() - 1 ? x - 1 : x + 1;
    if ( n >= 0 ) {
      ui.tabWidget->setCurrentIndex( n );
    }
  }

  ui.tabWidget->removeTab( x );
  delete w;

  if ( ui.tabWidget->count() == 0 ) {
    addNewTab();
  }
}

void MainWindow::closeCurrentTab()
{
  tabCloseRequested( ui.tabWidget->currentIndex() );
}

void MainWindow::closeAllTabs()
{
  while ( ui.tabWidget->count() > 1 ) {
    closeCurrentTab();
  }

  closeCurrentTab();
}

void MainWindow::closeRestTabs()
{
  if ( ui.tabWidget->count() < 2 ) {
    return;
  }

  int idx = ui.tabWidget->currentIndex();

  for ( int i = 0; i < idx; i++ ) {
    tabCloseRequested( 0 );
  }

  ui.tabWidget->setCurrentIndex( 0 );

  while ( ui.tabWidget->count() > 1 ) {
    tabCloseRequested( 1 );
  }
}

void MainWindow::switchToNextTab()
{
  if ( ui.tabWidget->count() < 2 ) {
    return;
  }

  ui.tabWidget->setCurrentIndex( ( ui.tabWidget->currentIndex() + 1 ) % ui.tabWidget->count() );
}

void MainWindow::switchToPrevTab()
{
  if ( ui.tabWidget->count() < 2 ) {
    return;
  }

  if ( !ui.tabWidget->currentIndex() ) {
    ui.tabWidget->setCurrentIndex( ui.tabWidget->count() - 1 );
  }
  else {
    ui.tabWidget->setCurrentIndex( ui.tabWidget->currentIndex() - 1 );
  }
}

void MainWindow::backClicked()
{
  ArticleView * view = getCurrentArticleView();

  view->back();
}

void MainWindow::forwardClicked()
{
  ArticleView * view = getCurrentArticleView();

  view->forward();
}

void MainWindow::titleChanged( ArticleView * view, const QString & title )
{
  QString escaped = Utils::escapeAmps( title );

  const int maxTabTitleLength = 30;
  escaped                     = Utils::ellipsizeString( escaped, maxTabTitleLength );

  int index = ui.tabWidget->indexOf( view );
  if ( !escaped.isEmpty() ) {
    ui.tabWidget->setTabText( index, escaped );
  }

  if ( index == ui.tabWidget->currentIndex() ) {
    updateFavIcon( title );

    updateWindowTitle();
  }
}

void MainWindow::iconChanged( ArticleView * view, const QIcon & icon )
{
  ui.tabWidget->setTabIcon( ui.tabWidget->indexOf( view ), groupInstances.size() > 1 ? icon : QIcon() );
}

void MainWindow::updateWindowTitle()
{
  ArticleView * view = getCurrentArticleView();
  if ( view ) {
    QString str = view->getTitle();
    if ( !str.isEmpty() ) {
      setWindowTitle( QString( "%1 - %2" ).arg( str, "MicroDict" ) );
    }
  }
}

void MainWindow::pageLoaded( ArticleView * view )
{
  if ( view != getCurrentArticleView() ) {
    return;
  }

  updateBackForwardButtons();
  updatePronounceAvailability();
}

void MainWindow::showStatusBarMessage( const QString & message, int timeout, const QPixmap & icon )
{
  if ( message.isEmpty() ) {
    mainStatusBar->clearMessage();
  }
  else {
    mainStatusBar->showMessage( message, timeout, icon );
  }
}

void MainWindow::tabSwitched( int )
{
  translateBox->setPopupEnabled( false );
  updateBackForwardButtons();
  updatePronounceAvailability();
  updateFoundInDictsList();
  updateWindowTitle();
  if ( mruList.size() > 1 ) {
    int from = mruList.indexOf( ui.tabWidget->widget( ui.tabWidget->currentIndex() ) );
    if ( from > 0 ) {
      mruList.move( from, 0 );
    }
  }

  auto view = getCurrentArticleView();
  QString headword;
  if ( view ) {
    headword = view->getCurrentWord();
  }

  updateFavIcon( headword );

  if ( view ) {
    groupList->setCurrentGroup( view->getCurrentGroupId() );
  }
}

void MainWindow::tabMenuRequested( QPoint pos )
{
  int tabIndex = ui.tabWidget->tabBar()->tabAt( pos );
  if ( tabIndex == -1 ) {
    return;
  }

  ArticleView * view = qobject_cast< ArticleView * >( ui.tabWidget->widget( tabIndex ) );
  tabMenu->popup( ui.tabWidget->mapToGlobal( pos ) );
}

void MainWindow::dictionaryBarToggled( bool )
{
  disconnect( dictionaryBar.toggleViewAction(), &QAction::toggled, this, &MainWindow::dictionaryBarToggled );

  dictionaryBar.updateToGroup( groupInstances.findGroup( groupList->getCurrentGroup() ), &cfg.mutedDictionaries, cfg );
  dictionaryBar.setDictionaryIconSize( this->getIconSizeLogical() );

  applyMutedDictionariesState();
}

void MainWindow::showDictsPane()
{
  if ( !ui.dictsPane->isVisible() ) {
    ui.dictsPane->show();
  }
}

void MainWindow::dictsPaneVisibilityChanged( bool visible )
{
  if ( visible ) {
    updateFoundInDictsList();
  }
}

void MainWindow::updateFoundInDictsList()
{
  if ( !ui.dictsList->isVisible() ) {
    return;
  }

  ArticleView * view = getCurrentArticleView();

  if ( !view ) {
    return;
  }

  ui.dictsList->clear();

  QStringList ids  = view->getArticlesList();
  QString activeId = view->getActiveArticleId();

  for ( QStringList::const_iterator i = ids.constBegin(); i != ids.constEnd(); ++i ) {

    for ( unsigned x = dictionaries.size(); x--; ) {
      if ( dictionaries[ x ]->getId() == i->toUtf8().data() ) {
        QString dictName = QString::fromUtf8( dictionaries[ x ]->getName().c_str() );
        QString dictId   = QString::fromUtf8( dictionaries[ x ]->getId().c_str() );
        auto * item =
          new QListWidgetItem( dictionaries[ x ]->getIcon(), dictName, ui.dictsList, QListWidgetItem::Type );
        item->setData( Qt::UserRole, QVariant( dictId ) );
        item->setToolTip( dictName );

        ui.dictsList->addItem( item );

        if ( dictId == activeId ) {
          ui.dictsList->setCurrentItem( item );
        }
        break;
      }
    }
  }
}

void MainWindow::updateBackForwardButtons()
{
  ArticleView * view = getCurrentArticleView();

  if ( view != nullptr ) {
    navBack->setEnabled( view->canGoBack() );
    navForward->setEnabled( view->canGoForward() );
  }
}

void MainWindow::updatePronounceAvailability()
{
  if ( ui.tabWidget->count() > 0 ) {
    ArticleView * pView = getCurrentArticleView();
    if ( pView == nullptr ) {
      return;
    }

    pView->hasSound( [ this ]( bool has ) {
      navPronounce->setEnabled( has );
    } );
  }
  else {
    navPronounce->setEnabled( false );
  }
}

void MainWindow::editDictionaries( unsigned editDictionaryGroup )
{
  hotkeyWrapper.reset();
  closeHeadwordsDialog();
  closeFullTextSearchDialog();

  wordFinder.clear();
  dictionariesUnmuted.clear();

  {

    Config::Class newCfg = cfg;
    EditDictionaries dicts( this, newCfg, dictionaries, groupInstances );

    connect( &dicts, &EditDictionaries::showDictionaryInfo, this, &MainWindow::showDictionaryInfo );

    connect( &dicts, &EditDictionaries::showDictionaryHeadwords, this, &MainWindow::showDictionaryHeadwords );

    if ( editDictionaryGroup != GroupId::NoGroupId ) {
      dicts.editGroup( editDictionaryGroup );
    }

    dicts.restoreGeometry( cfg.dictionariesDialogGeometry );
    dicts.exec();
    cfg.dictionariesDialogGeometry = newCfg.dictionariesDialogGeometry = dicts.saveGeometry();

    if ( dicts.areDictionariesChanged() || dicts.areGroupsChanged() ) {
      ftsIndexing.stopIndexing();
      ftsIndexing.clearDictionaries();
      for ( auto & group : newCfg.groups ) {
        unsigned id = group.id;
        if ( id != GroupId::NoGroupId ) {
          const Config::Group * grp = cfg.getGroup( id );
          if ( grp ) {
            group.mutedDictionaries = grp->mutedDictionaries;
          }
        }
      }

      cfg = newCfg;

      dictMap = Dictionary::dictToMap( dictionaries );

      updateGroupList();

      Config::save( cfg );

      updateSuggestionList();

      for ( auto & dictionary : dictionaries ) {
        dictionary->setFTSParameters( cfg.preferences.fts );
        dictionary->setSynonymSearchEnabled( cfg.preferences.synonymSearchEnabled );
      }

      ftsIndexing.setDictionaries( dictionaries );
      ftsIndexing.doIndexing();
    }
  }

  installHotKeys();
}

void MainWindow::editCurrentGroup()
{
  editDictionaries( groupList->getCurrentGroup() );
}

void MainWindow::editPreferences()
{
  hotkeyWrapper.reset();
  closeHeadwordsDialog();
  closeFullTextSearchDialog();

  ftsIndexing.stopIndexing();
  ftsIndexing.clearDictionaries();

  Preferences preferences( this, cfg );

  preferences.show();

  if ( preferences.exec() == QDialog::Accepted ) {
    Config::Preferences p = preferences.getPreferences();

    p.zoomFactor     = cfg.preferences.zoomFactor;
    p.helpZoomFactor = cfg.preferences.helpZoomFactor;
    p.hideMenubar    = cfg.preferences.hideMenubar;
    p.searchInDock   = cfg.preferences.searchInDock;
    p.alwaysOnTop    = cfg.preferences.alwaysOnTop;

    p.fts.dialogGeometry = cfg.preferences.fts.dialogGeometry;

    p.fts.searchMode = cfg.preferences.fts.searchMode;

    if ( cfg.preferences.favoritesStoreInterval != p.favoritesStoreInterval ) {
      ui.favoritesPaneWidget->setSaveInterval( p.favoritesStoreInterval );
    }

    bool needReload = ( cfg.preferences.collapseBigArticles != p.collapseBigArticles
                        || cfg.preferences.articleSizeLimit != p.articleSizeLimit
                        || cfg.preferences.alwaysExpandOptionalParts != p.alwaysExpandOptionalParts );

    cfg.preferences = p;

#ifdef Q_OS_MACOS
    MacAppActivation::setDockIconVisible( cfg.preferences.showDockIcon || isVisible() );
#endif

    for ( int x = 0; x < ui.tabWidget->count(); ++x ) {
      auto & view = dynamic_cast< ArticleView & >( *( ui.tabWidget->widget( x ) ) );

      view.setSelectionBySingleClick( p.selectWordBySingleClick );
      view.syncBackgroundColorWithCfgDarkReader();
      if ( needReload ) {
        view.reload();
      }

      if ( Config::followsDesktopTheme( cfg.preferences.darkReaderMode ) ) {
        connect( &SystemAppearance::instance(),
                 &SystemAppearance::changed,
                 &view,
                 &ArticleView::applyColorSchemeAndReload,
                 Qt::UniqueConnection );
      }
      else {
        disconnect( &SystemAppearance::instance(),
                    &SystemAppearance::changed,
                    &view,
                    &ArticleView::applyColorSchemeAndReload );
      }
    }

    if ( Config::followsDesktopTheme( cfg.preferences.darkMode ) ) {
      connect( &SystemAppearance::instance(),
               &SystemAppearance::changed,
               this,
               &MainWindow::refreshAppearances,
               Qt::UniqueConnection );
    }
    else {
      disconnect( &SystemAppearance::instance(),
                  &SystemAppearance::changed,
                  this,
                  &MainWindow::refreshAppearances );
    }

    audioPlayerFactory.setPreferences( cfg.preferences.useInternalPlayer,
                                       cfg.preferences.internalPlayerBackend,
                                       cfg.preferences.audioPlaybackProgram );

    trayIconUpdateOrInit();

    ui.tabWidget->setHideSingleTab( cfg.preferences.hideSingleTab );

    setAutostart( cfg.preferences.autoStart );

    history.enableAdd( cfg.preferences.storeHistory );
    history.setMaxSize( cfg.preferences.maxStringsInHistory );
    ui.historyPaneWidget->updateHistoryCounts();

    for ( const auto & dictionarie : dictionaries ) {
      dictionarie->setFTSParameters( cfg.preferences.fts );
      dictionarie->setSynonymSearchEnabled( cfg.preferences.synonymSearchEnabled );
    }

    Logger::switchLoggingMethod( cfg.preferences.enableApplicationLog );

    Config::save( cfg );
  }

  installHotKeys();
  QTimer::singleShot( 5000, this, [ this ]() {
    ftsIndexing.doIndexing();
  } );
}

void MainWindow::currentGroupChanged( int )
{
  unsigned grg_id               = groupList->getCurrentGroup();
  cfg.lastMainGroupId           = grg_id;
  const Instances::Group * igrp = groupInstances.findGroup( grg_id );
  if ( grg_id == GroupId::AllGroupId ) {
    if ( igrp ) {
      igrp->checkMutedDictionaries( &cfg.mutedDictionaries );
    }
    dictionaryBar.setMutedDictionaries( &cfg.mutedDictionaries );
  }
  else {
    Config::Group * grp = cfg.getGroup( grg_id );
    if ( grp ) {
      if ( igrp ) {
        igrp->checkMutedDictionaries( &grp->mutedDictionaries );
      }
      dictionaryBar.setMutedDictionaries( &grp->mutedDictionaries );
    }
    else {
      dictionaryBar.setMutedDictionaries( nullptr );
    }
  }

  if ( igrp ) {
    GlobalBroadcaster::instance()->currentGroupId = grg_id;
    ui.tabWidget->setTabIcon( ui.tabWidget->currentIndex(), igrp->makeIcon() );
  }
  else {
    ui.tabWidget->setTabIcon( ui.tabWidget->currentIndex(), QIcon() );
  }

  dictionaryBar.updateToGroup( groupInstances.findGroup( groupList->getCurrentGroup() ), &cfg.mutedDictionaries, cfg );

  translateBox->setPopupEnabled( false );
  updateSuggestionList();

  if ( auto view = getCurrentArticleView() ) {
    if ( view->getCurrentGroupId() != grg_id ) {
      if ( view->isInternalPage() ) {
        view->setCurrentGroupId( grg_id );
        return;
      }
      view->setCurrentGroupId( grg_id );
      QString word = Folding::unescapeWildcardSymbols( view->getWord() );
      respondToTranslationRequest( word, false );
    }
  }

  if ( ftsDlg ) {
    ftsDlg->setCurrentGroup( grg_id );
  }

  if ( igrp ) {
    ui.favoritesPaneWidget->trySetCurrentActiveFav( igrp->favoritesFolder.split( '/' ) );
  }
}

void MainWindow::translateInputChanged( const QString & newValue )
{
  updateSuggestionList( newValue );
  GlobalBroadcaster::instance()->translateLineText = newValue;
}

void MainWindow::updateSuggestionList()
{
  updateSuggestionList( translateLine->text() );
}

void MainWindow::updateSuggestionList( const QString & newValue )
{
  if ( !mainStatusBar->currentMessage().isEmpty() ) {
    mainStatusBar->clearMessage();
  }

  if ( ui.wordList->selectionModel()->hasSelection() ) {
    ui.wordList->setCurrentItem( nullptr, QItemSelectionModel::Clear );
  }

  QString req = newValue.trimmed();

  if ( !req.size() ) {
    wordFinder.cancel();
    ui.wordList->clear();
    ui.wordList->unsetCursor();

    Utils::Widget::setNoResultColor( translateLine, false );
    if ( !cfg.preferences.searchInDock ) {
      translateBox->setNoResults( false );
    }
    return;
  }

  ui.wordList->setCursor( Qt::WaitCursor );
  wordFinder.prefixMatch( req, getActiveDicts() );
}

void MainWindow::translateInputFinished( bool checkModifiers )
{
  QString word = Folding::unescapeWildcardSymbols( translateLine->text() );
  respondToTranslationRequest( word, checkModifiers );
}

void MainWindow::respondToTranslationRequest( const QString & word,
                                              bool checkModifiers,
                                              const QString & scrollTo,
                                              bool focus )
{
  if ( !word.isEmpty() ) {
    Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    if ( checkModifiers && ( mods & ( Qt::ControlModifier | Qt::ShiftModifier ) ) ) {
      addNewTab();
    }

    showTranslationFor( word, 0, scrollTo );

    if ( focus ) {
      if ( cfg.preferences.searchInDock && ui.searchPane->isFloating() ) {
        activateWindow();
      }
      focusArticleView();
    }
  }
}

void MainWindow::setInputLineText( QString text, WildcardPolicy wildcardPolicy, TranslateBoxPopup popupAction )
{
  if ( wildcardPolicy == WildcardPolicy::EscapeWildcards ) {
    text = Folding::escapeWildcardSymbols( text );
  }

  if ( popupAction == NoPopupChange || cfg.preferences.searchInDock ) {
    translateLine->setText( text );
  }
  else {
    translateBox->setText( text, popupAction == EnablePopup );
  }

  updateSuggestionList();
  GlobalBroadcaster::instance()->translateLineText = text;
}

void MainWindow::handleEsc()
{
  ArticleView * view = getCurrentArticleView();
  if ( view && view->closeSearch() ) {
    return;
  }

  if ( cfg.preferences.escKeyHidesMainWindow ) {
    toggleMainWindow( false );
  }
  else {
    focusTranslateLine();
  }
}

void MainWindow::focusTranslateLine()
{
  if ( cfg.preferences.searchInDock ) {
    if ( ui.searchPane->isFloating() ) {
      ui.searchPane->activateWindow();
    }
  }
  else {
    if ( !isActiveWindow() ) {
      activateWindow();
    }
  }

  translateLine->clearFocus();
  translateLine->setFocus();
  translateLine->selectAll();
}

void MainWindow::applyMutedDictionariesState()
{
  translateBox->setPopupEnabled( false );

  updateSuggestionList();

  ArticleView * view = getCurrentArticleView();

  if ( view ) {
    view->updateMutedContents();
  }
}

bool MainWindow::handleBackForwardMouseButtons( QMouseEvent * event )
{
  if ( event->button() == Qt::XButton1 ) {
    backClicked();
    return true;
  }
  else if ( event->button() == Qt::XButton2 ) {
    forwardClicked();
    return true;
  }
  else {
    return false;
  }
}

bool MainWindow::eventFilter( QObject * obj, QEvent * ev )
{
  if ( ev->type() == QEvent::ShortcutOverride || ev->type() == QEvent::KeyPress ) {
    auto * ke = dynamic_cast< QKeyEvent * >( ev );
    const int key = ke->key();
    if ( key == Qt::Key_F3 ) {
      ArticleView * view = getCurrentArticleView();
      if ( view && view->handleF3( obj, ev ) ) {
        return true;
      }
    }

    if ( obj == this && ev->type() == QEvent::KeyPress
         && ( key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Space || key == Qt::Key_PageUp
              || key == Qt::Key_PageDown ) ) {
      ArticleView * view = getCurrentArticleView();
      if ( view ) {
        view->focus();
        return true;
      }
    }
  }

  if ( obj == this && ( ev->type() == QEvent::Move || ev->type() == QEvent::Resize ) ) {
    if ( !cfg.preferences.searchInDock ) {
      translateBox->setPopupEnabled( false );
      return false;
    }
  }

  if ( obj == this && ev->type() == QEvent::WindowStateChange ) {
    auto stev    = dynamic_cast< QWindowStateChangeEvent * >( ev );
    wasMaximized = ( stev->oldState() == Qt::WindowMaximized && isMinimized() );
  }

  if ( ev->type() == QEvent::MouseButtonPress ) {
    auto event = dynamic_cast< QMouseEvent * >( ev );

    return handleBackForwardMouseButtons( event );
  }

  if ( ev->type() == QEvent::KeyPress ) {
    auto keyevent = dynamic_cast< QKeyEvent * >( ev );

    const bool handleCtrlTab = ( obj == translateLine || obj == ui.wordList || obj == ui.historyList
                                 || obj == ui.favoritesTree || obj == ui.dictsList || obj == groupList );

    if ( keyevent->modifiers() == Qt::ControlModifier && keyevent->key() == Qt::Key_Tab ) {
      if ( cfg.preferences.mruTabOrder ) {
        ctrlTabPressed();
        return true;
      }
      else if ( handleCtrlTab ) {
        QApplication::sendEvent( ui.tabWidget, ev );
        return true;
      }
      return false;
    }

    if ( handleCtrlTab && keyevent->matches( QKeySequence::PreviousChild ) ) {
      QApplication::sendEvent( ui.tabWidget, ev );
      return true;
    }
  }

  if ( obj == translateLine ) {
    if ( ev->type() == QEvent::KeyPress ) {
      QKeyEvent * keyEvent = dynamic_cast< QKeyEvent * >( ev );

      if ( cfg.preferences.searchInDock ) {
        if ( keyEvent->matches( QKeySequence::MoveToNextLine ) && ui.wordList->count() ) {
          ui.wordList->setFocus( Qt::ShortcutFocusReason );
          ui.wordList->setCurrentRow( 0, QItemSelectionModel::ClearAndSelect );
          return true;
        }
      }
    }

    if ( ev->type() == QEvent::FocusIn ) {

      if ( const auto focusEvent = dynamic_cast< QFocusEvent * >( ev ); focusEvent->reason() == Qt::MouseFocusReason ) {
        QTimer::singleShot( 0, this, &MainWindow::focusTranslateLine );
      }
      return false;
    }
  }
  else if ( obj == ui.wordList ) {
    if ( ev->type() == QEvent::KeyPress ) {
      QKeyEvent * keyEvent = dynamic_cast< QKeyEvent * >( ev );

      if ( keyEvent->matches( QKeySequence::MoveToPreviousLine ) && !ui.wordList->currentRow() ) {
        ui.wordList->setCurrentRow( 0, QItemSelectionModel::Clear );
        translateLine->setFocus( Qt::ShortcutFocusReason );
        return true;
      }

      if ( keyEvent->matches( QKeySequence::InsertParagraphSeparator ) && ui.wordList->selectedItems().size() ) {
        if ( cfg.preferences.searchInDock ) {
          if ( ui.searchPane->isFloating() ) {
            activateWindow();
          }
        }

        focusArticleView();

        return cfg.preferences.searchInDock;
      }
    }
  }
  else if ( obj == ui.dictsList ) {
    if ( ev->type() == QEvent::KeyPress ) {
      QKeyEvent * keyEvent = dynamic_cast< QKeyEvent * >( ev );

      if ( keyEvent->matches( QKeySequence::MoveToNextLine ) && ui.dictsList->count() > 0 ) {
        int currentRow = ui.dictsList->currentRow();
        if ( currentRow < ui.dictsList->count() - 1 ) {
          ui.dictsList->setCurrentRow( currentRow + 1, QItemSelectionModel::ClearAndSelect );
          jumpToDictionary( ui.dictsList->item( currentRow + 1 ), true );
          return true;
        }
      }
      else if ( keyEvent->matches( QKeySequence::MoveToPreviousLine ) && ui.dictsList->count() > 0 ) {
        int currentRow = ui.dictsList->currentRow();
        if ( currentRow > 0 ) {
          ui.dictsList->setCurrentRow( currentRow - 1, QItemSelectionModel::ClearAndSelect );
          jumpToDictionary( ui.dictsList->item( currentRow - 1 ), true );
          return true;
        }
      }
    }
  }

  if ( ev->type() == QEvent::KeyPress && obj != translateLine ) {

    if ( const auto key_event = dynamic_cast< QKeyEvent * >( ev );
         key_event->modifiers() == Qt::NoModifier || key_event->modifiers() == Qt::ShiftModifier ) {
      const QString text = key_event->text();

      if ( Utils::ignoreKeyEvent( key_event ) || key_event->key() == Qt::Key_Return
           || key_event->key() == Qt::Key_Enter ) {
        return false;
      }
      if ( !text.isEmpty() ) {
        typingEvent( text );
        return true;
      }
    }
  }

  return QMainWindow::eventFilter( obj, ev );
}

void MainWindow::wordListItemActivated( QListWidgetItem * item )
{
  if ( wordListSelChanged ) {
    wordListSelChanged = false;
  }
  else {
    respondToTranslationRequest( item->text(), true );
  }
}

void MainWindow::wordListSelectionChanged()
{
  QList< QListWidgetItem * > selected = ui.wordList->selectedItems();

  if ( !selected.empty() ) {
    wordListSelChanged = true;
    showTranslationFor( selected.front()->text() );
  }
}

void MainWindow::dictsListItemActivated( QListWidgetItem * item )
{
  jumpToDictionary( item, true );
}

void MainWindow::dictsListSelectionChanged()
{
  QList< QListWidgetItem * > selected = ui.dictsList->selectedItems();
  if ( selected.size() ) {
    jumpToDictionary( selected.front() );
  }
}

void MainWindow::jumpToDictionary( QListWidgetItem * item, bool force )
{
  auto dictId = item->data( Qt::UserRole ).toString();

  if ( ArticleView * view = getCurrentArticleView() ) {
    ui.tabWidget->setCurrentWidget( view );
    view->jumpToDictionary( dictId, force );
  }
}

void MainWindow::openLinkInNewTab( const QUrl & url,
                                   const QUrl & referrer,
                                   const QString & fromArticle,
                                   const Contexts & contexts )
{
  createNewTab( !cfg.preferences.newTabsOpenInBackground, "" )->openLink( url, referrer, fromArticle, contexts );
}

void MainWindow::showDefinitionInNewTab( const QString & word,
                                         unsigned group,
                                         const QString & fromArticle,
                                         const Contexts & contexts )
{
  createNewTab( !cfg.preferences.newTabsOpenInBackground, word )->showDefinition( word, group, fromArticle, contexts );
}

void MainWindow::handleTranslateSelectedText( const QString & word, const QUrl & url, const QString & currentArticle )
{
  Qt::KeyboardModifiers kmod = QApplication::keyboardModifiers();
  if ( kmod & ( Qt::ControlModifier | Qt::ShiftModifier ) ) {
    ArticleView * newView = createNewTab( !cfg.preferences.newTabsOpenInBackground, word );
    auto groupId          = newView->getGroup( url );
    if ( groupId == GroupId::NoGroupId ) {
      groupId = groupList->getCurrentGroup();
    }
    newView->showDefinition( word, groupId, currentArticle, Contexts() );
  }
  else {
    ArticleView * currentView = getCurrentArticleView();
    if ( currentView ) {
      auto groupId = currentView->getGroup( url );
      if ( groupId == GroupId::NoGroupId || currentView->isInternalPage() ) {
        groupId = groupList->getCurrentGroup();
      }

      if ( Utils::Url::hasQueryItem( url, "dictionaries" ) ) {
        QStringList dictsList = Utils::Url::queryItemValue( url, "dictionaries" ).split( ",", Qt::SkipEmptyParts );
        currentView->showDefinition( word, dictsList, groupId, false );
      }
      else {
        currentView->showDefinition( word, groupId, currentArticle );
      }
    }
  }
}

void MainWindow::activeArticleChanged( const ArticleView * view, const QString & id )
{
  if ( view != getCurrentArticleView() ) {
    return;
  }

  for ( int i = 0; i < ui.dictsList->count(); ++i ) {
    QListWidgetItem * w = ui.dictsList->item( i );
    QString dictId      = w->data( Qt::UserRole ).toString();

    if ( dictId == id ) {
      if ( i != ui.dictsList->currentRow() ) {
        ui.dictsList->setCurrentRow( i );
      }
      return;
    }
  }
}

void MainWindow::typingEvent( const QString & t )
{
  if ( t == "\n" || t == "\r" ) {
    if ( translateLine->isEnabled() ) {
      focusTranslateLine();
    }
  }
  else {
    if ( ( cfg.preferences.searchInDock && ui.searchPane->isFloating() ) || ui.dictsPane->isFloating() ) {
      ui.searchPane->activateWindow();
    }

    if ( translateLine->isEnabled() ) {
      if ( navToolbar->isFloating() ) {
        navToolbar->activateWindow();
      }

      translateLine->clear();
      translateLine->setFocus();
      setInputLineText( t, WildcardPolicy::WildcardsAreAlreadyEscaped, EnablePopup );
      translateLine->setCursorPosition( t.size() );
    }
  }
}

void MainWindow::mutedDictionariesChanged()
{
  if ( dictionaryBar.toggleViewAction()->isChecked() ) {
    applyMutedDictionariesState();
  }
}

void MainWindow::showHistoryItem( const QString & word )
{
  history.enableAdd( false );

  setInputLineText( word, WildcardPolicy::EscapeWildcards, DisablePopup );
  showTranslationFor( word );

  history.enableAdd( cfg.preferences.storeHistory );
}

void MainWindow::showTranslationFor( const QString & word, unsigned inGroup, const QString & scrollTo )
{
  ArticleView * view = getCurrentArticleView();

  navPronounce->setEnabled( false );

  unsigned group = inGroup;
  if ( group == 0 ) {
    group = groupInstances.empty() ? 0 : groupInstances[ groupList->currentIndex() ].id;
  }

  view->showDefinition( word, group, scrollTo );
}

void MainWindow::showTranslationForDicts( const QString & inWord,
                                          const QStringList & dictIDs,
                                          const QString & searchRegExp,
                                          bool ignoreDiacritics )
{
  ArticleView * view = getCurrentArticleView();

  navPronounce->setEnabled( false );

  view->showDefinition( inWord,
                        dictIDs,
                        searchRegExp,
                        groupInstances[ groupList->currentIndex() ].id,
                        ignoreDiacritics );
}

void MainWindow::toggleMainWindow( bool ensureShow )
{
  bool shown = false;

  if ( !cfg.preferences.searchInDock ) {
    translateBox->setPopupEnabled( false );
  }

#ifdef Q_OS_MACOS
  if ( !isVisible() || isMinimized() ) {
    MacAppActivation::setDockIconVisible( true );
  }
#endif

  if ( !isVisible() ) {
    show();

    activateWindow();
    raise();
    shown = true;
  }
  else if ( isMinimized() ) {
    if ( wasMaximized ) {
      showMaximized();
    }
    else {
      showNormal();
    }
    activateWindow();
    raise();
    shown = true;
  }
  else if ( !isActiveWindow() ) {
    activateWindow();
    if ( cfg.preferences.raiseWindowOnSearch ) {
      raise();
    }
    shown = true;
  }
  else if ( !ensureShow ) {

#ifdef Q_OS_MAC
    showMinimized();
#else
    if ( cfg.preferences.enableTrayIcon )
      hide();
    else
      showMinimized();
#endif


    if ( headwordsDlg ) {
      headwordsDlg->hide();
    }

    if ( ftsDlg ) {
      ftsDlg->hide();
    }
  }

  if ( shown ) {
#ifdef Q_OS_MACOS
    MacAppActivation::activate();
#endif
    if ( headwordsDlg ) {
      headwordsDlg->show();
    }

    if ( ftsDlg ) {
      ftsDlg->show();
    }

    focusTranslateLine();
  }
}

void MainWindow::installHotKeys()
{
  const QKeySequence wanted = cfg.preferences.enableClipboardHotkey ? cfg.preferences.clipboardHotkey : QKeySequence();

  // Only give back a grab this process actually holds. At first install there is
  // nothing of ours to release, and releasing then cleans up whatever the
  // desktop already keeps under this shortcut's name -- including a working grab
  // held by a run that did have an application id. Releasing before this run has
  // a shortcut session of its own destroys a hotkey and replaces it with
  // nothing; setAutostart says why a run can have no id to get one with.
  if ( !installedHotkey.isEmpty() && wanted != installedHotkey ) {
    HotkeyWrapper::releaseDesktopShortcuts();
  }

  installedHotkey = wanted;

  hotkeyWrapper.reset();

  if ( wanted.isEmpty() ) {
    return;
  }

  hotkeyWrapper = std::make_shared< HotkeyWrapper >( this );

  if ( !hotkeyWrapper->hotkeysAvailable() ) {
    if ( !hotkeysUnavailableReported ) {
      hotkeysUnavailableReported = true;

      QMessageBox::warning( this,
                            "MicroDict",
                            tr( "Global hotkeys are not available: this desktop offers no "
                                "org.freedesktop.portal.GlobalShortcuts service, which needs "
                                "xdg-desktop-portal 1.16 or newer.<br>"
                                "The rest of the application is unaffected." ) );
    }

    return;
  }

  hotkeyWrapper->setGlobalKey( wanted, 1 );

  connect( hotkeyWrapper.get(),
           &HotkeyWrapper::hotkeyActivated,
           this,
           &MainWindow::hotKeyActivated,
           Qt::AutoConnection );
}

QString MainWindow::clipboardTextForLookup()
{
  QString subtype = QStringLiteral( "plain" );

  if ( Utils::isWayland() ) {
    return QApplication::clipboard()->text( subtype, QClipboard::Clipboard );
  }

  return QApplication::clipboard()->text( subtype, QClipboard::Selection );
}

void MainWindow::hotKeyActivated( int )
{
  toggleMainWindow( true );

  static constexpr int clipboardDelayMs = 50;

  QTimer::singleShot( clipboardDelayMs, this, [ this ]() {
    const QString word = cfg.preferences.sanitizeInputPhrase( clipboardTextForLookup() );

    if ( word.isEmpty() ) {
      qInfo() << "Global hotkey: the clipboard holds no text to look up.";
      return;
    }

    wordReceived( word );
  } );
}


static bool isNewerVersion( const QString & candidate, const QString & reference )
{
  return QVersionNumber::fromString( candidate ) > QVersionNumber::fromString( reference );
}


void MainWindow::trayIconActivated( QSystemTrayIcon::ActivationReason r )
{
  switch ( r ) {
    case QSystemTrayIcon::Trigger:
      toggleMainWindow( false );

#ifdef Q_OS_MACOS
      if ( isVisible() ) {
        this->raise();
        this->activateWindow();
      }
#endif
      break;

#ifdef Q_OS_MACOS
    case QSystemTrayIcon::DoubleClick:
      toggleMainWindow( true );
      break;
#endif

    case QSystemTrayIcon::MiddleClick:
      break;
    default:
      break;
  }
}


#ifdef Q_OS_MACOS
void MainWindow::handleApplicationStateChanged( Qt::ApplicationState state )
{
  if ( state != Qt::ApplicationActive || ( !isMinimized() && isVisible() ) ) {
    return;
  }

  toggleMainWindow( true );
}
#endif

void MainWindow::visitHomepage()
{
  QDesktopServices::openUrl( QUrl( "https://dsvi.github.io/MicroDict/" ) );
}

void MainWindow::openConfigFolder()
{
  QDesktopServices::openUrl( QUrl::fromLocalFile( Config::getConfigDir() ) );
}

void MainWindow::visitForum()
{
  QDesktopServices::openUrl( QUrl( "https://github.com/dsvi/MicroDict/discussions" ) );
}

void MainWindow::showAbout()
{
  About about( this );

  about.show();
  about.exec();
}

void MainWindow::showDictBarNamesTriggered()
{
  bool show = showDictBarNamesAction.isChecked();

  dictionaryBar.setToolButtonStyle( show ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly );
  cfg.showingDictBarNames = show;
}

DictionaryBar::IconSize MainWindow::getIconSizeLogical()
{
  if ( useLargeIconsInToolbarsAction.isChecked() ) {
    return DictionaryBar::IconSize::Large;
  }
  else if ( useSmallIconsInToolbarsAction.isChecked() ) {
    return DictionaryBar::IconSize::Small;
  }
  else {
    return DictionaryBar::IconSize::Normal;
  }
}


int MainWindow::getIconSize()
{
  bool useLargeIcons = useLargeIconsInToolbarsAction.isChecked();
  int extent         = QApplication::style()->pixelMetric( QStyle::PM_ToolBarIconSize );
  if ( useLargeIcons ) {
    extent = QApplication::style()->pixelMetric( QStyle::PM_LargeIconSize );
  }
  else if ( useSmallIconsInToolbarsAction.isChecked() ) {
    extent = QApplication::style()->pixelMetric( QStyle::PM_SmallIconSize );
  }
  else {
  }
  return extent;
}

void MainWindow::iconSizeActionTriggered( QAction * )
{
  bool useLargeIcons = useLargeIconsInToolbarsAction.isChecked();
  int extent         = getIconSize();
  if ( useLargeIcons ) {
    cfg.usingToolbarsIconSize = Config::ToolbarsIconSize::Large;
  }
  else if ( useSmallIconsInToolbarsAction.isChecked() ) {
    cfg.usingToolbarsIconSize = Config::ToolbarsIconSize::Small;
  }
  else {
    cfg.usingToolbarsIconSize = Config::ToolbarsIconSize::Normal;
  }

  navToolbar->setIconSize( QSize( extent, extent ) );
  menuButton->setIconSize( QSize( extent, extent ) );

  dictionaryBar.setDictionaryIconSize( getIconSizeLogical() );
}

void MainWindow::toggleMenuBarTriggered( bool announce )
{
  cfg.preferences.hideMenubar = !toggleMenuBarAction.isChecked();

  if ( announce ) {
    if ( cfg.preferences.hideMenubar ) {
      mainStatusBar->showMessage( tr( "You have chosen to hide a menubar. Use %1 to show it back." )
                                    .arg( QString( "<b>%1</b>" ) )
                                    .arg( QT_TR_NOOP( "Ctrl+M" ) ),
                                  10000,
                                  QPixmap( ":/icons/warning.svg" ) );
    }
    else {
      mainStatusBar->clearMessage();
    }
  }

  QList< QMenu * > allMenus = menuBar()->findChildren< QMenu * >();
  for ( const auto & menu : allMenus ) {
    QList< QAction * > allMenuActions = menu->actions();
    for ( const auto & action : allMenuActions ) {
      if ( !action->shortcut().isEmpty() ) {
        if ( cfg.preferences.hideMenubar ) {
          addAction( action );
        }
        else {
          removeAction( action );
        }
      }
    }
  }

  menuBar()->setVisible( !cfg.preferences.hideMenubar );
  beforeOptionsSeparator->setVisible( cfg.preferences.hideMenubar );
  menuButtonAction->setVisible( cfg.preferences.hideMenubar );
}

void MainWindow::on_clearHistory_triggered()
{
  QMessageBox::StandardButton reply;
  reply = QMessageBox::question( this,
                                 tr( "Clear History" ),
                                 tr( "Are you sure you want to clear all history items?" ),
                                 QMessageBox::Yes | QMessageBox::No );
  if ( reply == QMessageBox::Yes ) {
    history.clear();
    history.save();
  }
}

void MainWindow::on_clearFavorites_triggered()
{
  ui.favoritesPaneWidget->clearAllItems();
}

void MainWindow::on_newTab_triggered()
{
  addNewTab();
}

namespace {

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )

// The name this program's own menu entry carries, used as the fallback when no
// menu entry for the program can be found anywhere.
constexpr char kDesktopFileName[] = "io.github.dsvi.microdict.desktop";

// Written into the login entry so a later run can recognise it as ours even
// under a name this program did not choose for itself.
constexpr char kLoginEntryMark[] = "X-MicroDict-LoginEntry=1";

// Does this menu entry's file name name this program? The word is looked for
// case-insensitively and has to stand on its own: whatever sits next to it, if
// anything does, must not be a letter or a digit. That is what tells
// appimagekit_5f2c-MicroDict.desktop and io.github.dsvi.microdict.desktop from
// some other program whose name only happens to end in the same letters.
bool nameSaysMicroDict( const QString & name )
{
  static const QString word = QStringLiteral( "microdict" );
  int from = 0;

  while ( true ) {
    const int at = name.indexOf( word, from, Qt::CaseInsensitive );

    if ( at < 0 )
      return false;

    const int after = at + word.size();
    const bool leftFree = at == 0 || !name.at( at - 1 ).isLetterOrNumber();
    const bool rightFree = after == name.size() || !name.at( after ).isLetterOrNumber();

    if ( leftFree && rightFree )
      return true;

    from = at + 1;
  }
}

// True if this .desktop file was written by this program itself: the mark is what
// lets a later run find the login entry again under a name it did not choose.
bool isLoginEntry( const QString & path )
{
  QFile file( path );

  if ( !file.open( QIODevice::ReadOnly ) )
    return false;

  bool mainGroup = false;

  for ( const QString & raw : QString::fromUtf8( file.readAll() ).split( '\n' ) ) {
    const QString line = raw.trimmed();

    if ( line.startsWith( QLatin1Char( '[' ) ) ) {
      mainGroup = ( line == QLatin1String( "[Desktop Entry]" ) );
      continue;
    }

    if ( mainGroup && line.startsWith( QLatin1String( "X-MicroDict-LoginEntry" ) ) )
      return true;
  }

  return false;
}

#endif

} // namespace

void MainWindow::setAutostart( bool autostart )
{
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
  // The desktop reads login entries from its own config dir, which is what
  // XDG_CONFIG_HOME points at. An AppImage's portable "<image>.config" is the
  // program's own and no desktop ever looks there, so in that case the entry
  // has to go to the default home config dir instead.
  QString configRoot
    = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );
  const QString appImage = QString::fromLocal8Bit( qgetenv( "APPIMAGE" ) );

  if ( !appImage.isEmpty() &&
       ( configRoot == appImage + ".config" ||
         configRoot.startsWith( appImage + ".home" ) ) ) {
    configRoot = QDir::homePath() + "/.config";
  }

  // Inside an AppImage the executable sits in a mount point whose name changes
  // on every run, so the entry has to name the AppImage file itself.
  QString execPath = QCoreApplication::applicationFilePath();
  const QByteArray appDir = qgetenv( "APPDIR" );

  if ( !appDir.isEmpty() && !appImage.isEmpty() &&
       execPath.startsWith( QString::fromLocal8Bit( appDir ) ) ) {
    execPath = appImage;
  }

  const QString execLine = QStringLiteral( "Exec=\"%1\" %u" ).arg( execPath );
  const QString tryExecLine = QStringLiteral( "TryExec=%1" ).arg( execPath );

  // The file name is not a label, it is the program's identity: systemd's
  // xdg-autostart-generator names the login unit after this file
  // (app-<name>@autostart.service), xdg-desktop-portal reads the application id
  // back out of that name, and the GlobalShortcuts portal hands out a shortcut
  // session only to a process whose id resolves to a menu entry in one of the
  // applications dirs. A login entry named after nothing in particular leaves
  // the autostarted process with no id at all, and the hotkey is refused with
  // "An app id is required".
  // So the name is not invented here but borrowed from a menu entry the desktop
  // already has for this program, whoever installed it: our own installer, a
  // distribution package, or a launcher that integrated an AppImage and calls it
  // appimagekit_<hash>-MicroDict.desktop. The menu entry is looked for by name
  // alone, since a name is all the portal itself goes by when it resolves the id
  // back to a file: any .desktop whose name says microdict on its own, in any
  // spelling, is a menu entry for this program.
  QStringList menuRoots = QStandardPaths::standardLocations( QStandardPaths::GenericDataLocation );
  menuRoots << QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );

  QString menuEntryPath;

  for ( const QString & root : std::as_const( menuRoots ) ) {
    const QDir dir( root + QLatin1String( "/applications" ) );
    const QStringList entries = dir.entryList( { QStringLiteral( "*.desktop" ) }, QDir::Files );

    if ( entries.contains( QLatin1String( kDesktopFileName ) ) ) {
      // This program's own name beats every other: it is the one name that does
      // not change when a launcher re-integrates the program under a new hash.
      menuEntryPath = dir.absoluteFilePath( QLatin1String( kDesktopFileName ) );
      break;
    }

    if ( menuEntryPath.isEmpty() ) {
      for ( const QString & name : entries ) {
        if ( nameSaysMicroDict( name ) ) {
          menuEntryPath = dir.absoluteFilePath( name );
          break;
        }
      }
    }
  }

  // Nothing to borrow: the entry is written under the name this program's own
  // installer uses, and a desktop that has no menu entry for the program at all
  // -- a bare AppImage that was never integrated -- will not resolve it.
  const QString menuEntryName = menuEntryPath.isEmpty()
      ? QLatin1String( kDesktopFileName )
      : QFileInfo( menuEntryPath ).fileName();

  if ( autostart && menuEntryPath.isEmpty() ) {
    // Nothing here to name the login entry after, so the run that entry starts
    // will carry an id the desktop cannot resolve and the portal will refuse its
    // shortcut with "An app id is required". Say so where the reason is known.
    qWarning() << "Global hotkeys: no menu entry for this program in an applications dir,"
               << "so the login entry is written as" << menuEntryName
               << "- a run started by it has no application id and can get no global hotkey.";
  }

  const QString autostartRoot = configRoot + QLatin1String( "/autostart" );
  const QString destinationPath = autostartRoot + QLatin1Char( '/' ) + menuEntryName;

  // One login entry per program: whatever this program wrote earlier under some
  // other name goes, or systemd generates a second unit for it and the portal
  // then sees two applications here, one of them unresolvable. The name used
  // before this mark existed is recognised by name instead.
  const QDir autostartDir( autostartRoot );

  for ( const QString & name : autostartDir.entryList( { QStringLiteral( "*.desktop" ) }, QDir::Files ) ) {
    if ( name == menuEntryName )
      continue;

    const QString path = autostartDir.absoluteFilePath( name );

    if ( name == QLatin1String( "microdict-owned-by-preferences.desktop" )
         || isLoginEntry( path ) ) {
      QFile::remove( path );
    }
  }

  if ( !autostart ) {
    QFile::remove( destinationPath );
    return;
  }

  // An entry that already names this very program is left alone. A moved or
  // renamed AppImage -- its updater appends a hash to the file name -- leaves
  // the old path behind, so that one has to be rewritten.
  if ( QFile oldFile( destinationPath ); oldFile.open( QIODevice::ReadOnly ) ) {
    const QString existing = QString::fromUtf8( oldFile.readAll() );
    oldFile.close();

    if ( existing.contains( execLine ) )
      return;
  }

  // The menu entry found above is the starting point: it carries the name, the
  // icon and the categories the desktop already shows for this program. Its
  // Desktop Actions belong to the menu, and the loop below would rewrite their
  // Exec lines into a second way of starting the program, so the groups go.
  QFile entry( menuEntryPath );
  QString contents;

  if ( entry.open( QIODevice::ReadOnly ) ) {
    contents = QString::fromUtf8( entry.readAll() );
  }
  else {
    contents = QStringLiteral( "[Desktop Entry]\nType=Application\nName=MicroDict\n" );
  }

  QStringList lines;
  QStringList pending; // comments and blank lines kept back until the group
                       // after them is known: the note above a delete action
                       // belongs to that action and goes away with it
  bool inActionGroup = false;
  bool execRewritten = false;
  bool tryExecRewritten = false;

  for ( QString line : contents.split( '\n' ) ) {
    if ( line.startsWith( '[' ) ) {
      inActionGroup = line.startsWith( "[Desktop Action" );
      if ( !inActionGroup ) {
        lines << pending;
        pending.clear();
      }
    }
    if ( inActionGroup ) {
      continue;
    }
    if ( line.startsWith( '#' ) || line.trimmed().isEmpty() ) {
      pending << line;
      continue;
    }
    lines << pending;
    pending.clear();
    if ( line.startsWith( "Actions=" ) ) {
      continue;
    }
    if ( line.startsWith( "Exec=" ) ) {
      line = execLine;
      execRewritten = true;
    }
    else if ( line.startsWith( "TryExec=" ) ) {
      line = tryExecLine;
      tryExecRewritten = true;
    }
    lines << line;
  }
  if ( !inActionGroup ) {
    lines << pending; // a note that was not above an action stays put
  }
  while ( !lines.isEmpty() && lines.back().trimmed().isEmpty() ) {
    lines.removeLast();
  }
  if ( !tryExecRewritten ) {
    // A desktop that cannot find this skips the entry instead of failing loudly
    lines << tryExecLine;
  }
  if ( !execRewritten ) {
    lines << execLine;
  }
  lines << QLatin1String( kLoginEntryMark );
  lines << QString(); // so the file ends with a newline

  QDir().mkpath( QFileInfo( destinationPath ).absolutePath() );

  if ( QFile outFile( destinationPath );
       outFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
    outFile.write( lines.join( '\n' ).toUtf8() );
  }
#endif
}

void MainWindow::on_actionCloseToTray_triggered()
{
  if ( cfg.preferences.enableTrayIcon && isVisible() ) {
    hide();
  }
}

void MainWindow::on_saveArticle_triggered()
{
  ArticleView * view = getCurrentArticleView();
  if ( !view ) {
    return;
  }
  auto * saver = new ArticleSaver( this, view, cfg );
  connect( saver, &ArticleSaver::statusMessage, this, [ this ]( const QString & message, int timeout ) {
    mainStatusBar->showMessage( message, timeout );
  } );
  saver->save();
}

void MainWindow::on_rescanFiles_triggered()
{
  hotkeyWrapper.reset();
  closeHeadwordsDialog();
  closeFullTextSearchDialog();

  ftsIndexing.stopIndexing();
  ftsIndexing.clearDictionaries();

  groupInstances.clear();
  dictionaries.clear();
  dictionariesUnmuted.clear();
  dictionaryBar.setDictionaries( dictionaries );

  loadDictionaries( this, cfg, dictionaries );
  dictMap = Dictionary::dictToMap( dictionaries );

  for ( const auto & dictionarie : dictionaries ) {
    dictionarie->setFTSParameters( cfg.preferences.fts );
    dictionarie->setSynonymSearchEnabled( cfg.preferences.synonymSearchEnabled );
  }

  ftsIndexing.setDictionaries( dictionaries );
  ftsIndexing.doIndexing();

  updateGroupList();


  installHotKeys();
}

void MainWindow::on_alwaysOnTop_triggered( bool checked )
{
  cfg.preferences.alwaysOnTop = checked;

  bool wasVisible = isVisible();

  Qt::WindowFlags flags = this->windowFlags();
  if ( checked ) {
    setWindowFlags( flags | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint );
    mainStatusBar->showMessage( tr( "The main window is set to be always on top." ),
                                10000,
                                QPixmap( ":/icons/warning.svg" ) );
  }
  else {
    setWindowFlags( flags ^ ( Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint ) );
    mainStatusBar->clearMessage();
  }

  if ( wasVisible ) {
    show();
  }

  installHotKeys();
}

void MainWindow::onLockPanelsToggled( bool locked )
{
  cfg.preferences.panelsLocked = locked;

  const QList< QDockWidget * > dockWidgets = findChildren< QDockWidget * >();

  for ( QDockWidget * dockWidget : dockWidgets ) {
    if ( locked ) {
      dockWidget->setFeatures( QDockWidget::NoDockWidgetFeatures );
    }
    else {
      dockWidget->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable );
    }
  }
}

void MainWindow::zoomin()
{
  cfg.preferences.zoomFactor += 0.1;
  applyZoomFactor();
}

void MainWindow::zoomout()
{
  cfg.preferences.zoomFactor -= 0.1;
  applyZoomFactor();
}

void MainWindow::unzoom()
{
  cfg.preferences.zoomFactor = 1;
  applyZoomFactor();
}

void MainWindow::applyZoomFactor()
{
  adjustCurrentZoomFactor();

  QTimer::singleShot( 0, this, &MainWindow::scaleArticlesByCurrentZoomFactor );
}

void MainWindow::adjustCurrentZoomFactor()
{
  if ( cfg.preferences.zoomFactor >= 5 ) {
    cfg.preferences.zoomFactor = 5;
  }
  else if ( cfg.preferences.zoomFactor <= 0.25 ) {
    cfg.preferences.zoomFactor = 0.25;
  }

  zoomIn->setEnabled( cfg.preferences.zoomFactor < 5 );
  zoomOut->setEnabled( cfg.preferences.zoomFactor > 0.25 );
  zoomBase->setEnabled( !qFuzzyCompare( cfg.preferences.zoomFactor, 1.0 ) );
}

void MainWindow::scaleArticlesByCurrentZoomFactor()
{
  for ( int i = 0; i < ui.tabWidget->count(); i++ ) {
    auto & view = dynamic_cast< ArticleView & >( *( ui.tabWidget->widget( i ) ) );
    view.setZoomFactor( cfg.preferences.zoomFactor );
  }

}

void MainWindow::showTranslation( const QString & word )
{
  wordReceived( word );
}

bool MainWindow::handleStructuredMessage( const QString & message )
{
  if ( !message.startsWith( "action:" ) ) {
    return false;
  }

  QMap< QString, QString > params;
  QStringList parts = message.split( '|' );
  for ( const QString & part : parts ) {
    QStringList keyValue = part.split( ':' );
    if ( keyValue.size() >= 2 ) {
      params[ keyValue[ 0 ] ] = keyValue.mid( 1 ).join( ':' );
    }
  }

  if ( QString action = params.value( "action" ); action == "translate" ) {
    QString word       = QUrl::fromPercentEncoding( params.value( "word" ).toLatin1() );
    QString group      = params.value( "group" );

    if ( !group.isEmpty() ) {
      setGroupByName( group );
    }

    showTranslation( word );
  }

  return true;
}


void MainWindow::messageFromAnotherInstanceReceived( const QString & message )
{
  if ( message == "bringToFront" ) {
    toggleMainWindow( true );
    return;
  }

  if ( handleStructuredMessage( message ) ) {
    return;
  }

  qWarning() << "Unknown message received from another instance: " << message;
}

ArticleView * MainWindow::getCurrentArticleView()
{
  QWidget * currentWidget   = ui.tabWidget->currentWidget();
  ArticleView * currentView = qobject_cast< ArticleView * >( currentWidget );

  if ( currentView ) {
    return currentView;
  }

  return nullptr;
}



void MainWindow::wordReceived( const QString & word )
{
  const bool shouldFocus = cfg.preferences.raiseWindowOnSearch;
  if ( shouldFocus ) {
    toggleMainWindow( true );
  }
  setInputLineText( word, WildcardPolicy::EscapeWildcards, NoPopupChange );
  respondToTranslationRequest( word, false, QString(), shouldFocus );
}

void MainWindow::updateFavoritesMenu()
{
  if ( ui.favoritesPane->isVisible() ) {
    ui.showHideFavorites->setText( tr( "&Hide" ) );
  }
  else {
    ui.showHideFavorites->setText( tr( "&Show" ) );
  }
}

void MainWindow::updateHistoryMenu()
{
  if ( ui.historyPane->isVisible() ) {
    ui.showHideHistory->setText( tr( "&Hide" ) );
  }
  else {
    ui.showHideHistory->setText( tr( "&Show" ) );
  }
}

void MainWindow::toggle_favoritesPane()
{
  if ( ui.favoritesPane->isVisible() ) {
    ui.favoritesPane->hide();
  }
  else {
    ui.favoritesPane->show();
    ui.favoritesPane->raise();
  }
}

void MainWindow::toggle_historyPane()
{
  if ( ui.historyPane->isVisible() ) {
    ui.historyPane->hide();
  }
  else {
    ui.historyPane->show();
    ui.historyPane->raise();
  }
}

void MainWindow::on_exportHistory_triggered()
{
  QString exportPath;
  if ( cfg.historyExportPath.isEmpty() ) {
    exportPath = QDir::homePath();
  }
  else {
    exportPath = QDir::fromNativeSeparators( cfg.historyExportPath );
    if ( !QDir( exportPath ).exists() ) {
      exportPath = QDir::homePath();
    }
  }

  QString fileName = QFileDialog::getSaveFileName( this,
                                                   tr( "Export history to file" ),
                                                   exportPath,
                                                   tr( "Text files (*.txt);;All files (*.*)" ) );
  if ( fileName.size() == 0 ) {
    return;
  }

  cfg.historyExportPath = QDir::toNativeSeparators( QFileInfo( fileName ).absoluteDir().absolutePath() );
  QFile file( fileName );


  if ( !file.open( QFile::WriteOnly | QIODevice::Text ) ) {
    errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
    return;
  }

  [[maybe_unused]] auto guard = qScopeGuard( [ &file ]() {
    file.close();
  } );

  QByteArray line;
  line.append( 0xEF ).append( 0xBB ).append( 0xBF );
  if ( file.write( line ) != line.size() ) {
    errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
    return;
  }

  const QList< History::Item > & items = history.getItems();

  QList< History::Item >::const_iterator i;
  for ( i = items.constBegin(); i != items.constEnd(); ++i ) {
    line = i->word.toUtf8();

    line.replace( '\n', ' ' );
    line.replace( '\r', ' ' );

    line += "\n";

    if ( file.write( line ) != line.size() ) {
      errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
      return;
    }
  }

  mainStatusBar->showMessage( tr( "History export complete" ), 5000 );
}

void MainWindow::on_importHistory_triggered()
{
  QString importPath;
  if ( cfg.historyExportPath.isEmpty() ) {
    importPath = QDir::homePath();
  }
  else {
    importPath = QDir::fromNativeSeparators( cfg.historyExportPath );
    if ( !QDir( importPath ).exists() ) {
      importPath = QDir::homePath();
    }
  }

  QString fileName = QFileDialog::getOpenFileName( this,
                                                   tr( "Import history from file" ),
                                                   importPath,
                                                   tr( "Text files (*.txt);;All files (*.*)" ) );
  if ( fileName.size() == 0 ) {
    return;
  }

  QFileInfo fileInfo( fileName );
  cfg.historyExportPath = QDir::toNativeSeparators( fileInfo.absoluteDir().absolutePath() );
  QString errStr;
  QFile file( fileName );

  if ( !file.open( QFile::ReadOnly | QIODevice::Text ) ) {
    errStr = QString( tr( "Import error: " ) ) + file.errorString();
    errorMessageOnStatusBar( errStr );
    return;
  }

  QTextStream fileStream( &file );
  QString itemStr, trimmedStr;
  QList< QString > itemList;

  do {
    itemStr = fileStream.readLine();
    if ( fileStream.status() >= QTextStream::ReadCorruptData ) {
      break;
    }

    trimmedStr = itemStr.trimmed();
    if ( trimmedStr.isEmpty() ) {
      continue;
    }

    if ( (unsigned)trimmedStr.size() <= history.getMaxItemLength() ) {
      itemList.prepend( trimmedStr );
    }

  } while ( !fileStream.atEnd() && itemList.size() < (int)history.getMaxSize() );

  history.enableAdd( true );

  for ( QList< QString >::const_iterator i = itemList.constBegin(); i != itemList.constEnd(); ++i ) {
    history.addItem( { *i } );
  }

  history.enableAdd( cfg.preferences.storeHistory );

  if ( file.error() != QFile::NoError ) {
    errStr = QString( tr( "Import error: " ) ) + file.errorString();
    errorMessageOnStatusBar( errStr );
    return;
  }

  if ( fileStream.status() >= QTextStream::ReadCorruptData ) {
    errStr = QString( tr( "Import error: invalid data in file" ) );
    errorMessageOnStatusBar( errStr );
    return;
  }
  file.close();
  mainStatusBar->showMessage( tr( "History import complete" ), 5000 );
}

void MainWindow::errorMessageOnStatusBar( const QString & errStr )
{
  this->mainStatusBar->showMessage( errStr, 10000, QPixmap( ":/icons/error.svg" ) );
}

void MainWindow::on_exportFavorites_triggered()
{
  QString exportPath;
  if ( cfg.historyExportPath.isEmpty() ) {
    exportPath = QDir::homePath();
  }
  else {
    exportPath = QDir::fromNativeSeparators( cfg.historyExportPath );
    if ( !QDir( exportPath ).exists() ) {
      exportPath = QDir::homePath();
    }
  }

  QString fileName = QFileDialog::getSaveFileName( this,
                                                   tr( "Export Favorites to file" ),
                                                   exportPath,
                                                   tr( "Text files (*.txt);;XML files (*.xml)" ) );
  if ( fileName.size() == 0 ) {
    return;
  }
  cfg.historyExportPath = QDir::toNativeSeparators( QFileInfo( fileName ).absoluteDir().absolutePath() );
  QFile file( fileName );
  if ( !file.open( QFile::WriteOnly | QIODevice::Text ) ) {
    errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
    return;
  }
  if ( fileName.endsWith( ".xml", Qt::CaseInsensitive ) ) {
    QByteArray data;
    ui.favoritesPaneWidget->getDataInXml( data );

    if ( file.write( data ) != data.size() ) {
      errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
      return;
    }
  }
  else {
    QByteArray line;
    line.append( 0xEF ).append( 0xBB ).append( 0xBF );
    if ( file.write( line ) != line.size() ) {
      errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
      return;
    }

    QString data;
    ui.favoritesPaneWidget->getDataInPlainText( data );

    line = data.toUtf8();
    if ( file.write( line ) != line.size() ) {
      errorMessageOnStatusBar( QString( tr( "Export error: " ) ) + file.errorString() );
      return;
    }
  }
  file.close();

  mainStatusBar->showMessage( tr( "Favorites export complete" ), 5000 );
}

void MainWindow::on_importFavorites_triggered()
{
  QString importPath;
  if ( cfg.historyExportPath.isEmpty() ) {
    importPath = QDir::homePath();
  }
  else {
    importPath = QDir::fromNativeSeparators( cfg.historyExportPath );
    if ( !QDir( importPath ).exists() ) {
      importPath = QDir::homePath();
    }
  }

  QString fileName = QFileDialog::getOpenFileName( this,
                                                   tr( "Import Favorites from file" ),
                                                   importPath,
                                                   tr( "Text and XML files (*.txt *.xml);;All files (*.*)" ) );
  if ( fileName.size() == 0 ) {
    return;
  }

  QFileInfo fileInfo( fileName );
  cfg.historyExportPath = QDir::toNativeSeparators( fileInfo.absoluteDir().absolutePath() );
  QString errStr;
  QFile file( fileName );

  if ( !file.open( QFile::ReadOnly | QIODevice::Text ) ) {
    errorMessageOnStatusBar( QString( tr( "Import error: " ) ) + file.errorString() );
    return;
  }

  QByteArray data = file.readAll();

  if ( fileInfo.suffix().compare( "txt", Qt::CaseInsensitive ) == 0 ) {
    if ( !ui.favoritesPaneWidget->setDataFromTxt( QString::fromUtf8( data.data(), data.size() ) ) ) {
      errorMessageOnStatusBar( QString( tr( "Data parsing error" ) ) );
      return;
    }
  }
  else {
    if ( !ui.favoritesPaneWidget->setDataFromXml( QString::fromUtf8( data.data(), data.size() ) ) ) {
      errorMessageOnStatusBar( QString( tr( "Data parsing error" ) ) );
      return;
    }
  }

  file.close();
  mainStatusBar->showMessage( tr( "Favorites import complete" ), 5000 );
}

void MainWindow::focusWordList()
{
  if ( ui.wordList->count() > 0 ) {
    ui.wordList->setFocus();
  }
}

void MainWindow::addWordToHistory( const QString & word )
{
  history.addItem( { word.trimmed() } );
}

void MainWindow::forceAddWordToHistory( const QString & word )
{
  history.enableAdd( true );
  history.addItem( { word.trimmed() } );
  history.enableAdd( cfg.preferences.storeHistory );
}

void MainWindow::foundDictsPaneClicked( QListWidgetItem * item )
{

  Qt::KeyboardModifiers m = QApplication::keyboardModifiers();
  if ( ( m & ( Qt::ControlModifier | Qt::ShiftModifier ) ) || ( m == Qt::AltModifier ) ) {
    QString id = item->data( Qt::UserRole ).toString();
    emit clickOnDictPane( id );
  }

  jumpToDictionary( item, true );
}

void MainWindow::showDictionaryInfo( const QString & id )
{
  for ( unsigned x = 0; x < dictionaries.size(); x++ ) {
    if ( dictionaries[ x ]->getId() == id.toUtf8().data() ) {
      DictInfo infoMsg( cfg, this );
      infoMsg.showInfo( dictionaries[ x ] );
      int result = infoMsg.exec();

      if ( result == DictInfo::OPEN_FOLDER ) {
        openDictionaryFolder( id );
      }
      else if ( result == DictInfo::SHOW_HEADWORDS ) {
        showDictionaryHeadwords( dictionaries[ x ].get() );
      }

      break;
    }
  }
}

void MainWindow::showDictionaryHeadwords( Dictionary::Class * dict )
{

  QWidget * owner = qobject_cast< QWidget * >( sender() );

  if ( owner->objectName() == "EditDictionaries" ) {
    DictHeadwords headwords( owner, cfg, dict );
    headwords.exec();
  }
  else {
    if ( headwordsDlg == nullptr ) {
      headwordsDlg = new DictHeadwords( this, cfg, dict );
      addGlobalActionsToDialog( headwordsDlg );
      addGroupComboBoxActionsToDialog( headwordsDlg, groupList );
      connect( headwordsDlg,
               &DictHeadwords::headwordSelected,
               this,
               [ this ]( const QString & headword, const QString & dictID ) {
                 setInputLineText( headword, WildcardPolicy::EscapeWildcards, NoPopupChange );
                 respondToTranslationRequest( headword, false, ArticleView::scrollToFromDictionaryId( dictID ), false );
               } );
      connect( headwordsDlg,
               &DictHeadwords::closeDialog,
               this,
               &MainWindow::closeHeadwordsDialog,
               Qt::QueuedConnection );
    }
    else {
      headwordsDlg->setup( dict );
    }
    headwordsDlg->show();
  }
}


void MainWindow::closeHeadwordsDialog()
{
  if ( headwordsDlg ) {
    delete headwordsDlg;
    headwordsDlg = nullptr;
  }
}

void MainWindow::focusHeadwordsDialog()
{
  if ( headwordsDlg ) {
    headwordsDlg->activateWindow();
    if ( ftsDlg ) {
      ftsDlg->lower();
    }
  }
}

void MainWindow::focusArticleView()
{
  if ( ArticleView * view = getCurrentArticleView() ) {
    if ( !isActiveWindow() ) {
      activateWindow();
    }
    view->focus();
  }
}

void MainWindow::stopAudio()
{
  if ( ArticleView * view = getCurrentArticleView() ) {
    view->stopSound();
  }
}

void MainWindow::openDictionaryFolder( const QString & id )
{
  for ( unsigned x = 0; x < dictionaries.size(); x++ ) {
    if ( dictionaries[ x ]->getId() == id.toUtf8().data() ) {
      if ( !dictionaries[ x ]->getDictionaryFilenames().empty() ) {
        QDesktopServices::openUrl( QUrl::fromLocalFile( dictionaries[ x ]->getContainingFolder() ) );
      }
      break;
    }
  }
}


void MainWindow::foundDictsContextMenuRequested( const QPoint & pos )
{
  QListWidgetItem * item = ui.dictsList->itemAt( pos );
  if ( !item ) {
    return;
  }

  QString id                = item->data( Qt::UserRole ).toString();
  Dictionary::Class * pDict = nullptr;

  for ( unsigned i = 0; i < dictionaries.size(); i++ ) {
    if ( id.compare( dictionaries[ i ]->getId().c_str() ) == 0 ) {
      pDict = dictionaries[ i ].get();
      break;
    }
  }

  if ( pDict == nullptr ) {
    return;
  }

  if ( !pDict->isLocalDictionary() ) {
    showDictionaryInfo( id );
    return;
  }

  QMenu menu( ui.dictsList );
  QAction * infoAction = menu.addAction( tr( "Dictionary info" ) );

  QAction * headwordsAction = nullptr;
  if ( pDict->getWordCount() > 0 ) {
    headwordsAction = menu.addAction( tr( "Dictionary headwords" ) );
  }

  QAction * openDictFolderAction = menu.addAction( tr( "Open dictionary folder" ) );

  QAction * result = menu.exec( ui.dictsList->mapToGlobal( pos ) );

  if ( !result ) {
    return;
  }

  if ( result == infoAction ) {
    showDictionaryInfo( id );
  }
  else if ( result == headwordsAction ) {
    showDictionaryHeadwords( pDict );
  }
  else if ( result == openDictFolderAction ) {
    openDictionaryFolder( id );
  }
}

void MainWindow::sendWordToInputLine( const QString & word )
{
  setInputLineText( word, WildcardPolicy::EscapeWildcards, NoPopupChange );
}

void MainWindow::storeResourceSavePath( const QString & newPath )
{
  cfg.resourceSavePath = newPath;
}


void MainWindow::showFullTextSearchDialog()
{
  if ( !ftsDlg ) {
    ftsDlg = new FTS::FullTextSearchDialog( this, cfg, dictionaries, groupInstances, ftsIndexing );

    addGlobalActionsToDialog( ftsDlg );
    addGroupComboBoxActionsToDialog( ftsDlg, groupList );

    connect( ftsDlg, &FTS::FullTextSearchDialog::showTranslationFor, this, &MainWindow::showTranslationForDicts );
    connect( ftsDlg,
             &FTS::FullTextSearchDialog::closeDialog,
             this,
             &MainWindow::closeFullTextSearchDialog,
             Qt::QueuedConnection );
    connect( GlobalBroadcaster::instance(),
             &GlobalBroadcaster::ftsStateChanged,
             ftsDlg,
             &FTS::FullTextSearchDialog::updateDictionaries );

    ftsDlg->setCurrentGroup( groupInstances.empty() ? 0 : groupInstances[ groupList->currentIndex() ].id );
  }
  ftsDlg->updateDictionaries();

  if ( !ftsDlg->isVisible() ) {
    ftsDlg->show();
  }
  else {
    ftsDlg->activateWindow();
    if ( headwordsDlg ) {
      headwordsDlg->lower();
    }
  }
}

void MainWindow::closeFullTextSearchDialog()
{
  if ( ftsDlg ) {
    ftsDlg->stopSearch();
    delete ftsDlg;
    ftsDlg = nullptr;
  }
}

void MainWindow::showFTSIndexingName( const QString & name )
{
  if ( name.isEmpty() ) {
    mainStatusBar->setBackgroundMessage( QString() );
  }
  else {
    mainStatusBar->setBackgroundMessage( tr( "Now indexing for full-text search: " ) + name );
  }
}



void MainWindow::addCurrentTabToFavorites()
{
  auto view = getCurrentArticleView();
  if ( !view ) {
    return;
  }
  auto headword = view->getCurrentWord();

  ui.favoritesPaneWidget->addWordToActiveFav( headword );

  addToFavorites->setIcon( blueStarIcon );
  addToFavorites->setToolTip( tr( "Remove current tab from Favorites" ) );
}

void MainWindow::handleAddToFavoritesButton()
{
  auto view = getCurrentArticleView();
  if ( !view ) {
    return;
  }
  auto headword = view->getCurrentWord();

  if ( ui.favoritesPaneWidget->isWordPresentInActiveFolder( headword ) ) {
    QMessageBox mb( QMessageBox::Question,
                    "MicroDict",
                    tr( "Remove headword \"%1\" from Favorites?" ).arg( headword ),
                    QMessageBox::Yes | QMessageBox::No,
                    this );
    if ( mb.exec() == QMessageBox::Yes ) {
      if ( ui.favoritesPaneWidget->removeWordFromActiveFav( headword ) ) {
        addToFavorites->setIcon( starIcon );
        addToFavorites->setToolTip( tr( "Add current tab to Favorites" ) );
      }
    }
  }
  else {
    ui.favoritesPaneWidget->addWordToActiveFav( headword );
    addToFavorites->setIcon( blueStarIcon );
    addToFavorites->setToolTip( tr( "Remove current tab from Favorites" ) );
  }
}


void MainWindow::addBookmarkToFavorite( const QString & text )
{
  auto view = getCurrentArticleView();
  if ( !view ) {
    return;
  }
  auto word           = view->getCurrentWord();
  const auto bookmark = QString( "%1~~~%2" ).arg( word, text );

  ui.favoritesPaneWidget->addWordToActiveFav( bookmark );
}

void MainWindow::addAllTabsToFavorites()
{
  for ( int i = 0; i < ui.tabWidget->count(); i++ ) {
    auto view = qobject_cast< ArticleView * >( ui.tabWidget->widget( i ) );
    if ( !view ) {
      continue;
    }
    auto headword = view->getCurrentWord();
    ui.favoritesPaneWidget->addWordToActiveFav( headword );
  }
  addToFavorites->setIcon( blueStarIcon );
  addToFavorites->setToolTip( tr( "Remove current tab from Favorites" ) );
}

bool MainWindow::updateFavIcon( const QString & word )
{

  if ( ui.favoritesPaneWidget->isWordPresentInActiveFolder( word ) ) {
    addToFavorites->setIcon( blueStarIcon );
    addToFavorites->setToolTip( tr( "Remove current tab from Favorites" ) );
  }
  else {
    addToFavorites->setIcon( starIcon );
    addToFavorites->setToolTip( tr( "Add current tab to Favorites" ) );
  }

  if ( word.isEmpty() ) {
    return false;
  }

  return ui.favoritesPaneWidget->isWordPresentInActiveFolder( word );
}


void MainWindow::updateFavIconSlot()
{
  updateFavIcon( getCurrentArticleView()->getCurrentWord() );
}

void MainWindow::setGroupByName( const QString & name )
{
  int i;

  for ( i = 0; i < groupList->count(); i++ ) {
    if ( groupList->itemText( i ) == name ) {
      groupList->setCurrentIndex( i );
      break;
    }
  }

  if ( i >= groupList->count() ) {
    qWarning( "Group \"%s\" for main window is not found", name.toUtf8().data() );
  }
}

void MainWindow::headwordFromFavorites( const QString & headword, const QString & favFolderFullPath )
{
  if ( !favFolderFullPath.isEmpty() ) {
    for ( Instances::Groups::size_type i = 0; i < groupInstances.size(); i++ ) {
      if ( groupInstances[ i ].favoritesFolder == favFolderFullPath ) {
        if ( groupList->currentIndex() != (int)i ) {
          groupList->setCurrentIndex( i );

          ui.favoritesPaneWidget->setFocusOnTree();
        }
        break;
      }
    }
  }

  auto words = headword.split( "~~~" );

  setInputLineText( words[ 0 ], WildcardPolicy::EscapeWildcards, DisablePopup );

  if ( words.size() > 1 ) {
    auto view = getCurrentArticleView();
    if ( view ) {
      view->setDelayedHighlightText( words[ 1 ] );
    }
  }

  showTranslationFor( words[ 0 ] );
}

