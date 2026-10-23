/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QSystemTrayIcon>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QToolButton>
#include <QActionGroup>
#include <QClipboard>
#include <functional>
#include "ui_mainwindow.h"
#include "config.hh"
#include "dict/dictionary.hh"
#include "lite/lite_resources.hh"
#include "audio/audioplayerfactory.hh"
#include "instances.hh"
#include "article_maker.hh"
#include "ui/articleview.hh"
#include "wordfinder.hh"
#include "dictionarybar.hh"
#include "history.hh"
#include "mainstatusbar.hh"
#include "mruqmenu.hh"
#include "translatebox.hh"
#include "dictheadwords.hh"
#include "fulltextsearch.hh"
#include "base_type.hh"
#include "hotkey/hotkeywrapper.hh"

using std::string;
using std::vector;

class MainWindow: public QMainWindow
{
  Q_OBJECT

public:

  MainWindow( Config::Class & cfg );
  ~MainWindow();

  void setGroupByName( const QString & name );

  enum class WildcardPolicy {
    EscapeWildcards,
    WildcardsAreAlreadyEscaped
  };
public slots:

  void messageFromAnotherInstanceReceived( const QString & );
  void showStatusBarMessage( const QString & message, int timeout = 5000, const QPixmap & pixmap = QPixmap() );
  void wordReceived( const QString & );
  void headwordFromFavorites( const QString & word, const QString & favFolderFullPath );
  void commitData();
  void performCleanup();
  void quitApp();

private:
  void addGlobalAction( QAction * action, const std::function< void() > & slotFunc );
  void addGlobalActionsToDialog( QDialog * dialog );
  void addGroupComboBoxActionsToDialog( QDialog * dialog, GroupComboBox * pGroupComboBox );
  void removeGroupComboBoxActionsFromDialog( QDialog * dialog, GroupComboBox * pGroupComboBox );

  bool handleStructuredMessage( const QString & message );

public:
  void showTranslation( const QString & word );

private:


  QSystemTrayIcon * trayIcon;


  Ui::MainWindow ui;

  QWidget searchPaneTitleBar;
  QHBoxLayout searchPaneTitleBarLayout;
  GroupComboBox *groupList, *groupListInToolbar, *groupListInDock;

  QAction * translateBoxToolBarAction;

  QWidget dictsPaneTitleBar;
  QHBoxLayout dictsPaneTitleBarLayout;
  QLabel foundInDictsLabel;

  TranslateBox * translateBox;

  QFont wordListDefaultFont, translateLineDefaultFont, groupListDefaultFont;

  QAction escAction, focusTranslateLineAction, addTabAction, closeCurrentTabAction, closeAllTabAction,
    closeRestTabAction, switchToNextTabAction, switchToPrevTabAction, showDictBarNamesAction, toggleMenuBarAction,
    lockPanelsAction, focusHeadwordsDlgAction, focusArticleViewAction, addAllTabToFavoritesAction;

  QAction useSmallIconsInToolbarsAction, useLargeIconsInToolbarsAction, useNormalIconsInToolbarsAction;

  QActionGroup * smallLargeIconGroup = new QActionGroup( this );

  QAction stopAudioAction;
  QToolBar * navToolbar;
  MainStatusBar * mainStatusBar;
  QAction *navBack, *navForward, *navPronounce;
  QAction * beforeOptionsSeparator;
  QAction *zoomIn, *zoomOut, *zoomBase;
  QAction * addToFavorites;
  QMenu trayIconMenu;
#ifdef Q_OS_MACOS
  QMenu dockMenu;
#endif
  QMenu * tabMenu;
  QAction * menuButtonAction;
  QToolButton * menuButton;
  MRUQMenu * tabListMenu;
  QList< QWidget * > mruList;
  QToolButton addTab, *tabListButton;
  Config::Class & cfg;
  History history;
  DictionaryBar dictionaryBar;
  vector< sptr< Dictionary::Class > > dictionaries;
  QMap< std::string, sptr< Dictionary::Class > > dictMap;
  vector< sptr< Dictionary::Class > > dictionariesUnmuted;
  Instances::Groups groupInstances;
  ArticleMaker articleMaker;
  ArticleResourceManager articleResMgr;
  AudioPlayerFactory audioPlayerFactory;

  QLineEdit * translateLine;

  WordFinder wordFinder;

  sptr< HotkeyWrapper > hotkeyWrapper;

  QKeySequence installedHotkey;

  bool hotkeysUnavailableReported = false;

  bool wordListSelChanged;

  bool wasMaximized;

  bool isQuitting = false;

  DictHeadwords * headwordsDlg;

  FTS::FtsIndexing ftsIndexing;

  QTimer ftsRestartTimer;
  bool ftsStateChanged = false;

  FTS::FullTextSearchDialog * ftsDlg;

  QIcon starIcon, blueStarIcon;

  void updateAppearances();

  void trayIconUpdateOrInit();

  void wheelEvent( QWheelEvent * ) override;
  void closeEvent( QCloseEvent * ) override;
  void hideEvent( QHideEvent * event ) override;

  void makeDictionaries();
  void updateStatusLine();
  void updateGroupList( bool reload = true );

  void updatePronounceAvailability();

  void updateBackForwardButtons();

  void updateWindowTitle();

  void applyMutedDictionariesState();

  bool eventFilter( QObject *, QEvent * ) override;

  const vector< sptr< Dictionary::Class > > & getActiveDicts();

  void toggleMainWindow( bool ensureShow );

  void installHotKeys();

  void applyZoomFactor();
  void adjustCurrentZoomFactor();

  void mousePressEvent( QMouseEvent * event ) override;

  bool handleBackForwardMouseButtons( QMouseEvent * ev );

  ArticleView * getCurrentArticleView();
  void ctrlTabPressed();

  void respondToTranslationRequest( const QString & word,
                                    bool checkModifiers,
                                    const QString & scrollTo = QString(),
                                    bool focus               = true );

  void updateSuggestionList();
  void updateSuggestionList( const QString & text );

  enum TranslateBoxPopup {
    NoPopupChange,
    EnablePopup,
    DisablePopup
  };

  void setInputLineText( QString text, WildcardPolicy wildcardPolicy, TranslateBoxPopup popupAction );

  void updateArticleFonts() const;

  void errorMessageOnStatusBar( const QString & errStr );
  int getIconSize();
  DictionaryBar::IconSize getIconSizeLogical();


  bool updateFavIcon( const QString & word );

private slots:
  void updateFavIconSlot();

#ifdef Q_OS_MACOS
  void handleApplicationStateChanged( Qt::ApplicationState state );
#endif


  void hotKeyActivated( int );

  void updateFoundInDictsList();

  void foundDictsPaneClicked( QListWidgetItem * item );

  void foundDictsContextMenuRequested( const QPoint & pos );

  void showDictionaryInfo( const QString & id );

  void showDictionaryHeadwords( Dictionary::Class * dict );

  void openDictionaryFolder( const QString & id );

  void showFTSIndexingName( const QString & name );

  void handleAddToFavoritesButton();

  void addCurrentTabToFavorites();

  void addAllTabsToFavorites();

  void addNewTab();
  void tabCloseRequested( int );
  void closeCurrentTab();
  void closeAllTabs();
  void closeRestTabs();
  void switchToNextTab();
  void switchToPrevTab();

  void createTabList();
  void fillWindowsMenu();
  void switchToWindow( QAction * act );

  void backClicked();
  void forwardClicked();

  void titleChanged( ArticleView *, const QString & );
  void iconChanged( ArticleView *, const QIcon & );

  void pageLoaded( ArticleView * );
  void tabSwitched( int );
  void tabMenuRequested( QPoint pos );

  void dictionaryBarToggled( bool checked );

  void zoomin();
  void zoomout();
  void unzoom();

  void scaleArticlesByCurrentZoomFactor();

  void editDictionaries( unsigned editDictionaryGroup = GroupId::NoGroupId );
  void editCurrentGroup();
  void editPreferences();

  void currentGroupChanged( int );
  void translateInputChanged( const QString & );
  void translateInputFinished( bool checkModifiers );

  void handleEsc();

  void focusTranslateLine();

  void wordListItemActivated( QListWidgetItem * );
  void wordListSelectionChanged();

  void dictsListItemActivated( QListWidgetItem * );
  void dictsListSelectionChanged();

  void jumpToDictionary( QListWidgetItem *, bool force = false );

  void showDictsPane();
  void dictsPaneVisibilityChanged( bool );

  ArticleView * createNewTab( bool switchToIt, const QString & name );

  void openLinkInNewTab( const QUrl &, const QUrl &, const QString &, const Contexts & contexts );
  void showDefinitionInNewTab( const QString & word,
                               unsigned group,
                               const QString & fromArticle,
                               const Contexts & contexts );
  void typingEvent( const QString & );

  void activeArticleChanged( const ArticleView *, const QString & id );

  void mutedDictionariesChanged();

  void showTranslationFor( const QString &, unsigned inGroup = 0, const QString & scrollTo = QString() );

  void showTranslationForDicts( const QString &,
                                const QStringList & dictIDs,
                                const QString & searchRegExp,
                                bool ignoreDiacritics );

  void showHistoryItem( const QString & );

  void trayIconActivated( QSystemTrayIcon::ActivationReason );

  void setAutostart( bool );

  void visitHomepage();
  void visitForum();
  void openConfigFolder();
  void showAbout();

  void showDictBarNamesTriggered();
  void iconSizeActionTriggered( QAction * action );
  void toggleMenuBarTriggered( bool announce = true );

  void on_clearHistory_triggered();
  void on_clearFavorites_triggered();

  void on_newTab_triggered();

  void on_actionCloseToTray_triggered();

  void on_saveArticle_triggered();

  void on_rescanFiles_triggered();

  void toggle_favoritesPane();
  void toggle_historyPane();
  void on_exportHistory_triggered();
  void on_importHistory_triggered();
  void on_alwaysOnTop_triggered( bool checked );
  void focusWordList();

  void on_exportFavorites_triggered();
  void on_importFavorites_triggered();

  void onLockPanelsToggled( bool locked );

  void updateSearchPaneAndBar( bool searchInDock );

  void updateFavoritesMenu();
  void updateHistoryMenu();

  void addWordToHistory( const QString & word );
  void forceAddWordToHistory( const QString & word );


  void addBookmarkToFavorite( const QString & text );

  void sendWordToInputLine( const QString & word );

  void storeResourceSavePath( const QString & );

  void closeHeadwordsDialog();

  void handleTranslateSelectedText( const QString & word, const QUrl & url, const QString & currentArticle );

  void focusHeadwordsDialog();

  void focusArticleView();
  void stopAudio();


  void showFullTextSearchDialog();
  void closeFullTextSearchDialog();

  void prefixMatchUpdated();
  void prefixMatchFinished();
  void updateMatchResults( bool finished );
  void refreshTranslateLine();
  QString clipboardTextForLookup();

  void refreshAppearances();

signals:
  void clickOnDictPane( const QString & id );
};
