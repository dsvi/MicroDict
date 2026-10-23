/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "audio/internalplayerbackend.hh"
#include "ex.hh"
#include <QDateTime>
#include <QDomDocument>
#include <QKeySequence>
#include <QList>
#include <QLocale>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThread>
#include <thread>
#include <optional>
#include <QDir>

enum GroupId : unsigned {
  AllGroupId = UINT_MAX - 1,
  NoGroupId  = 0,
};

namespace Config {

enum class Dark : std::uint8_t {
  Off  = 0,
  On   = 1,
  Auto = 2,
};

bool followsDesktopTheme( Dark setting );

struct Path
{
  QString path;
  bool recursive;

  Path():
    recursive( false )
  {
  }
  Path( const QString & path_, bool recursive_ ):
    path( path_ ),
    recursive( recursive_ )
  {
  }

  bool operator==( const Path & other ) const
  {
    return path == other.path && recursive == other.recursive;
  }
};

using Paths = QList< Path >;

struct SoundDir
{
  QString path, name;
  QString iconFilename;

  SoundDir() {}

  SoundDir( const QString & path_, const QString & name_, QString iconFilename_ = "" ):
    path( path_ ),
    name( name_ ),
    iconFilename( iconFilename_ )
  {
  }

  bool operator==( const SoundDir & other ) const
  {
    return path == other.path && name == other.name && iconFilename == other.iconFilename;
  }
};

using SoundDirs = QList< SoundDir >;

struct DictionaryRef
{
  QString id;
  QString name;

  DictionaryRef() {}

  DictionaryRef( const QString & id_, const QString & name_ ):
    id( id_ ),
    name( name_ )
  {
  }

  bool operator==( const DictionaryRef & other ) const
  {
    return id == other.id && name == other.name;
  }
};

struct Group
{
  unsigned id;
  QString name, icon;
  QByteArray iconData;
  QKeySequence shortcut;
  QString favoritesFolder;
  QList< DictionaryRef > dictionaries;
  QSet< QString > mutedDictionaries;

  Group():
    id( 0 )
  {
  }

  bool operator==( const Group & other ) const
  {
    return id == other.id && name == other.name && icon == other.icon && favoritesFolder == other.favoritesFolder
      && dictionaries == other.dictionaries && shortcut == other.shortcut
      && mutedDictionaries == other.mutedDictionaries && iconData == other.iconData;
  }

  bool operator!=( const Group & other ) const
  {
    return !operator==( other );
  }
};

using Groups = QList< Group >;

struct HotKey
{
  Qt::KeyboardModifiers modifiers;
  int key1, key2;

  HotKey( const QKeySequence & );

  QKeySequence toKeySequence() const;
};

struct FullTextSearch
{
  int searchMode;
  bool enabled;

  quint32 maxDictionarySize;
  quint32 parallelThreads = std::thread::hardware_concurrency() / 3 + 1;
  QByteArray dialogGeometry;
  QString disabledTypes;

  FullTextSearch():
    searchMode( 0 ),
    enabled( true ),
    maxDictionarySize( 0 )
  {
  }
};

struct Preferences
{
  bool newTabsOpenAfterCurrentOne;
  bool newTabsOpenInBackground;
  bool hideSingleTab;
  bool mruTabOrder;
  bool hideMenubar;
  bool panelsLocked = false;

  bool enableTrayIcon = true;
  bool closeToTray    = true;
  bool startToTray    = false;
  bool showDockIcon   = false;

  bool autoStart;
  bool doubleClickTranslates;
  bool selectWordBySingleClick;
  bool autoScrollToTargetArticle;
  bool escKeyHidesMainWindow;
  bool alwaysOnTop;

  bool searchInDock;

  bool enableClipboardHotkey;

  QKeySequence clipboardHotkey;

  bool ignoreDiacritics;
  bool ignorePunctuation;
  bool sessionCollapse = false;

  bool pronounceOnLoadMain;
  bool useInternalPlayer;
  InternalPlayerBackend internalPlayerBackend{};
  QString audioPlaybackProgram;


  bool removeInvalidIndexOnExit = false;
  bool enableApplicationLog = false;

  qreal zoomFactor;
  qreal helpZoomFactor;

  unsigned maxStringsInHistory;
  unsigned storeHistory;
  bool alwaysExpandOptionalParts;

  unsigned favoritesStoreInterval = 15;

  bool confirmFavoritesDeletion;

  bool collapseBigArticles;
  int articleSizeLimit;

  bool limitInputPhraseLength;
  int inputPhraseLengthLimit;
  QString sanitizeInputPhrase( const QString & inputWord ) const;

  unsigned short maxDictionaryRefsInContextMenu;

  bool synonymSearchEnabled;
  bool stripClipboard;
  bool raiseWindowOnSearch = true;

  FullTextSearch fts;

  Dark darkMode = Dark::Off;
  Dark darkReaderMode =
#if defined( Q_OS_MACOS )
    Dark::Auto;
#else
    Dark::Off;
#endif


  Preferences();
};

struct Hunspell
{
  QString dictionariesPath;

  using Dictionaries = QList< QString >;

  Dictionaries enabledDictionaries;

  bool operator==( const Hunspell & other ) const
  {
    return dictionariesPath == other.dictionariesPath && enabledDictionaries == other.enabledDictionaries;
  }

  bool operator!=( const Hunspell & other ) const
  {
    return !operator==( other );
  }
};

struct Chinese
{
  bool enable;

  bool enableSCToTWConversion   = true;
  bool enableSCToHKConversion   = true;
  bool enableTCToSCConversion   = true;
  bool enableJapaneseConversion = true;

  Chinese();

  bool operator==( const Chinese & other ) const
  {
    return enable == other.enable && enableSCToTWConversion == other.enableSCToTWConversion
      && enableSCToHKConversion == other.enableSCToHKConversion
      && enableTCToSCConversion == other.enableTCToSCConversion
      && enableJapaneseConversion == other.enableJapaneseConversion;
  }

  bool operator!=( const Chinese & other ) const
  {
    return !operator==( other );
  }
};


struct CustomTrans
{
  bool enable = false;

  QString context;

  bool operator==( const CustomTrans & other ) const
  {
    return enable == other.enable && context == other.context;
  }

  bool operator!=( const CustomTrans & other ) const
  {
    return !operator==( other );
  }
};

struct Romaji
{
  bool enable;

  bool enableHiragana;
  bool enableKatakana;

  Romaji();

  bool operator==( const Romaji & other ) const
  {
    return enable == other.enable && enableHiragana == other.enableHiragana && enableKatakana == other.enableKatakana;
  }

  bool operator!=( const Romaji & other ) const
  {
    return !operator==( other );
  }
};

struct Transliteration
{
  bool enableRussianTransliteration;
  bool enableGermanTransliteration;
  bool enableGreekTransliteration;
  bool enableBelarusianTransliteration;

  CustomTrans customTrans;
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  Chinese chinese;
#endif
  Romaji romaji;

  bool operator==( const Transliteration & other ) const
  {
    return enableRussianTransliteration == other.enableRussianTransliteration
      && enableGermanTransliteration == other.enableGermanTransliteration
      && enableGreekTransliteration == other.enableGreekTransliteration
      && enableBelarusianTransliteration == other.enableBelarusianTransliteration && customTrans == other.customTrans &&
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
      chinese == other.chinese &&
#endif
      romaji == other.romaji;
  }

  bool operator!=( const Transliteration & other ) const
  {
    return !operator==( other );
  }

  Transliteration():
    enableRussianTransliteration( false ),
    enableGermanTransliteration( false ),
    enableGreekTransliteration( false ),
    enableBelarusianTransliteration( false )
  {
  }
};

struct Program
{
  bool enabled;
  enum Type {
    Invalid      = -1,
    Audio        = 0,
    PlainText    = 1,
    Html         = 2,
    PrefixMatch  = 3,
    MaxTypeValue = 4
  };
  Type type = Invalid;
  QString id, name, commandLine;
  QString iconFilename;

  Program():
    enabled( false )
  {
  }

  Program( bool enabled_,
           Type type_,
           const QString & id_,
           const QString & name_,
           const QString & commandLine_,
           const QString & iconFilename_ ):
    enabled( enabled_ ),
    type( type_ ),
    id( id_ ),
    name( name_ ),
    commandLine( commandLine_ ),
    iconFilename( iconFilename_ )
  {
  }

  bool operator==( const Program & other ) const
  {
    return enabled == other.enabled && type == other.type && name == other.name && commandLine == other.commandLine
      && iconFilename == other.iconFilename;
  }

  bool operator!=( const Program & other ) const
  {
    return !operator==( other );
  }
};

using Programs = QList< Program >;

struct HeadwordsDialog
{
  int searchMode;
  bool matchCase;
  bool autoApply;
  QString headwordsExportPath;
  QByteArray headwordsDialogGeometry;

  HeadwordsDialog():
    searchMode( 0 ),
    matchCase( false ),
    autoApply( false )
  {
  }
};

enum class ToolbarsIconSize : std::uint8_t {
  Small  = 0,
  Normal = 1,
  Large  = 2,
};

struct GroupBackup
{
  Group dictionaryOrder;
  Group inactiveDictionaries;
  Groups groups;
};

struct Class
{
  Paths paths;
  SoundDirs soundDirs;
  Group dictionaryOrder;
  Group inactiveDictionaries;
  Groups groups;
  Preferences preferences;
  Hunspell hunspell;
  Transliteration transliteration;
  Programs programs;

  bool dirty = false;

  unsigned lastMainGroupId;

  QByteArray dictInfoGeometry;
  QByteArray dictionariesDialogGeometry;

  QString historyExportPath;
  QString resourceSavePath;
  QString articleSavePath;

  QByteArray mainWindowState;
  QByteArray mainWindowGeometry;

  QSet< QString > mutedDictionaries;
  QSet< QString > dictionariesToReindex;

  bool showingDictBarNames;

  ToolbarsIconSize usingToolbarsIconSize = ToolbarsIconSize::Normal;

  unsigned int maxHeadwordSize;

  unsigned int maxHeadwordsToExpand;

  HeadwordsDialog headwordsDialog;

  Class():
    lastMainGroupId( 0 ),
    showingDictBarNames( false ),
    maxHeadwordSize( 256U ),
    maxHeadwordsToExpand( 0 )
  {
  }
  Group * getGroup( unsigned id );
  const Group * getGroup( unsigned id ) const;
  bool resetState = false;
};

DEF_EX( exError, "Error with the program's configuration", std::exception )
DEF_EX( exCantUseDataDir, "Can't use XDG_DATA_HOME directory to store MicroDict data", exError )
DEF_EX( exCantUseHomeDir, "Can't use home directory to store MicroDict preferences", exError )
DEF_EX( exCantUseIndexDir, "Can't use index directory to store MicroDict index files", exError )
DEF_EX( exCantReadConfigFile, "Can't read the configuration file", exError )
DEF_EX( exCantWriteConfigFile, "Can't write the configuration file", exError )
DEF_EX( exMalformedConfigFile, "The configuration file is malformed", exError )

Class load();

void save( const Class & );

QString getConfigFileName();

QString getConfigDir();

QString getIndexDir();

QDir getHomeDir();

QString getPidFileName();

QString getHistoryFileName();

QString getFavoritiesFileName();

QString getProgramDataDir() noexcept;

QString getLocDir() noexcept;

QString getHelpDir() noexcept;

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
QString getOpenCCDir() noexcept;
#endif

bool isPortableVersion() noexcept;

QString getPortableVersionDictionaryDir() noexcept;

QString getPortableVersionMorphoDir() noexcept;

QString getCacheDir() noexcept;

}
