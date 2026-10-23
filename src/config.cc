/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "config.hh"
#include "folding.hh"
#include <QtXml>
#include <QApplication>


#include <stdint.h>

#include <QStandardPaths>

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
  #define XDG_BASE_DIRECTORY_COMPLIANCE
#endif

namespace Config {

bool followsDesktopTheme( [[maybe_unused]] Dark setting )
{
#ifdef Q_OS_MACOS
  return setting == Dark::Auto;
#else
  return true;
#endif
}

namespace {
#ifdef XDG_BASE_DIRECTORY_COMPLIANCE
const char xdgSubdirName[] = "microdict";

QDir getDataDir()
{
  QDir dir = QStandardPaths::writableLocation( QStandardPaths::GenericDataLocation );
  dir.mkpath( xdgSubdirName );
  if ( !dir.cd( xdgSubdirName ) )
    throw exCantUseDataDir();

  return dir;
}
#endif

QString portableHomeDirPath()
{
  return QCoreApplication::applicationDirPath() + "/portable";
}

}

HotKey::HotKey( const QKeySequence & seq ):
  modifiers( seq[ 0 ].keyboardModifiers() ),
  key1( seq[ 0 ].key() ),
  key2( seq[ 1 ].key() )
{
}

QKeySequence HotKey::toKeySequence() const
{
  if ( key2 != 0 && key2 != Qt::Key::Key_unknown ) {
    return { QKeyCombination( modifiers, static_cast< Qt::Key >( key1 ) ),
             QKeyCombination( modifiers, static_cast< Qt::Key >( key2 ) ) };
  }
  return { QKeyCombination( modifiers, static_cast< Qt::Key >( key1 ) ) };
  ;
}

QString Preferences::sanitizeInputPhrase( const QString & inputWord ) const
{
  QString result = inputWord;
  if ( stripClipboard ) {
    auto parts = inputWord.split( QChar::LineFeed, Qt::SkipEmptyParts );
    if ( !parts.empty() ) {
      result = parts[ 0 ];
    }
  }

  if ( limitInputPhraseLength && result.size() > inputPhraseLengthLimit ) {
    qDebug( "Ignoring an input phrase %lld symbols long. The configured maximum input phrase length is %d symbols.",
            result.size(),
            inputPhraseLengthLimit );
    return {};
  }

  return Folding::trimWhitespace( result.simplified() );
}

Preferences::Preferences():
  newTabsOpenAfterCurrentOne( false ),
  newTabsOpenInBackground( true ),
  hideSingleTab( true ),
  mruTabOrder( false ),
  hideMenubar( false ),
  autoStart( false ),
  doubleClickTranslates( true ),
  selectWordBySingleClick( false ),
  autoScrollToTargetArticle( true ),
  escKeyHidesMainWindow( false ),
  alwaysOnTop( false ),
  searchInDock( false ),
#ifdef Q_OS_MACOS
  enableClipboardHotkey( false ),
#else
  enableClipboardHotkey( true ),
#endif
  // A single combination, so the desktop grabs it and the action fires on the
  // first press; a function key, so its keysym is the same in every keyboard
  // layout. See website/docs/ui_shortcuts.md.
  clipboardHotkey( QKeySequence( "F12" ) ),
  ignoreDiacritics( false ),
  ignorePunctuation( true ),
  pronounceOnLoadMain( false ),
  useInternalPlayer( InternalPlayerBackend::anyAvailable() ),
  zoomFactor( 1 ),
  helpZoomFactor( 1 ),
  maxStringsInHistory( 500 ),
  storeHistory( 1 ),
  alwaysExpandOptionalParts( true ),
  confirmFavoritesDeletion( true ),
  collapseBigArticles( false ),
  articleSizeLimit( 2000 ),
  limitInputPhraseLength( false ),
  inputPhraseLengthLimit( 1000 ),
  maxDictionaryRefsInContextMenu( 20 ),
  synonymSearchEnabled( true ),
  stripClipboard( false )
{
}

Chinese::Chinese():
  enable( false )
{
}

Romaji::Romaji():
  enable( false ),
  enableHiragana( true ),
  enableKatakana( true )
{
}

Group * Class::getGroup( unsigned id )
{
  for ( auto & group : groups ) {
    if ( group.id == id ) {
      return &group;
    }
  }
  return 0;
}

const Group * Class::getGroup( unsigned id ) const
{
  for ( const auto & group : groups ) {
    if ( group.id == id ) {
      return &group;
    }
  }
  return 0;
}

namespace {

Programs makeDefaultPrograms()
{
  Programs programs;

  programs.push_back(
    Program( false, Program::Audio, "428b4c2b905ef568a43d9a16f59559b0", "Festival", "festival --tts", "" ) );
  programs.push_back(
    Program( false, Program::Audio, "2cf8b3a60f27e1ac812de0b57c148340", "Espeak", "espeak %GDWORD%", "" ) );
  programs.push_back( Program( false,
                               Program::Html,
                               "4f898f7582596cea518c6b0bfdceb8b3",
                               "Manpages",
                               "man -a --html=/bin/cat %GDWORD%",
                               "" ) );

  return programs;
}

void applyBoolOption( bool & option, const QDomNode & node )
{
  QString value = node.toElement().text();

  if ( value == "1" ) {
    option = true;
  }
  else if ( value == "0" ) {
    option = false;
  }
}

Group loadGroup( QDomElement grp )
{
  Group g;

  if ( grp.hasAttribute( "id" ) ) {
    g.id = grp.attribute( "id" ).toUInt();
  }
  else {
    g.name = grp.attribute( "name" );
    g.id   = static_cast< unsigned >( qHash( g.name ) );
    if ( g.id == 0 ) {
      g.id = 1;
    }
  }

  g.name            = grp.attribute( "name" );
  g.icon            = grp.attribute( "icon" );
  g.favoritesFolder = grp.attribute( "favoritesFolder" );

  if ( !grp.attribute( "iconData" ).isEmpty() ) {
    g.iconData = QByteArray::fromBase64( grp.attribute( "iconData" ).toLatin1() );
  }

  if ( !grp.attribute( "shortcut" ).isEmpty() ) {
    g.shortcut = QKeySequence::fromString( grp.attribute( "shortcut" ) );
  }

  QDomNodeList dicts = grp.elementsByTagName( "dictionary" );

  for ( int y = 0; y < dicts.length(); ++y ) {
    g.dictionaries.push_back(
      DictionaryRef( dicts.item( y ).toElement().text(), dicts.item( y ).toElement().attribute( "name" ) ) );
  }

  QDomNode muted = grp.namedItem( "mutedDictionaries" );
  dicts          = muted.toElement().elementsByTagName( "mutedDictionary" );
  for ( int x = 0; x < dicts.length(); ++x ) {
    g.mutedDictionaries.insert( dicts.item( x ).toElement().text() );
  }


  return g;
}

QSet< QString > loadDictionaries( const QDomNode & mutedDictionaries, const QString & elementName = "mutedDictionary" )
{
  QSet< QString > result;

  if ( !mutedDictionaries.isNull() ) {
    QDomNodeList nl = mutedDictionaries.toElement().elementsByTagName( elementName );

    for ( int x = 0; x < nl.length(); ++x ) {
      result.insert( nl.item( x ).toElement().text() );
    }
  }

  return result;
}

void saveDictionaries( QDomDocument & dd,
                       QDomElement & muted,
                       const QSet< QString > & mutedDictionaries,
                       const QString & elementName = "mutedDictionary" )
{
  for ( const auto & mutedDictionarie : mutedDictionaries ) {
    QDomElement dict = dd.createElement( elementName );
    muted.appendChild( dict );

    QDomText value = dd.createTextNode( mutedDictionarie );
    dict.appendChild( value );
  }
}

}

Class load()
{
  QString configName = getConfigFileName();

  bool loadFromTemplate = false;

  if ( !QFile::exists( configName ) ) {
    Class c;

#ifdef Q_OS_LINUX
    if ( QDir( "/usr/share/stardict/dic" ).exists() )
      c.paths.push_back( Path( "/usr/share/stardict/dic", true ) );

    if ( QDir( "/usr/share/dictd" ).exists() )
      c.paths.push_back( Path( "/usr/share/dictd", true ) );

    if ( QDir( "/usr/share/opendict/dictionaries" ).exists() )
      c.paths.push_back( Path( "/usr/share/opendict/dictionaries", true ) );

    if ( QDir( "/usr/share/WyabdcRealPeopleTTS" ).exists() )
      c.soundDirs.push_back( SoundDir( "/usr/share/WyabdcRealPeopleTTS", "WyabdcRealPeopleTTS" ) );

    if ( QDir( "/usr/share/myspell/dicts" ).exists() )
      c.hunspell.dictionariesPath = "/usr/share/myspell/dicts";
#endif

    if ( isPortableVersion() ) {
      c.paths.push_back( Path( getPortableVersionDictionaryDir(), true ) );
    }

    QString possibleMorphologyPath = getProgramDataDir() + "/content/morphology";

    if ( QDir( possibleMorphologyPath ).exists() ) {
      c.hunspell.dictionariesPath = possibleMorphologyPath;
    }

    c.programs    = makeDefaultPrograms();

    configName       = getProgramDataDir() + "/content/defconfig";
    loadFromTemplate = QFile( configName ).exists();

    if ( !loadFromTemplate ) {
      save( c );

      return c;
    }
  }

  QFile configFile( configName );

  if ( !configFile.open( QFile::ReadOnly ) ) {
    throw exCantReadConfigFile();
  }

  QDomDocument dd;

  QString errorStr;
  int errorLine, errorColumn;

  if ( !loadFromTemplate ) {
    if ( !dd.setContent( &configFile, false, &errorStr, &errorLine, &errorColumn ) ) {
      qDebug( "Error: %s at %d,%d", errorStr.toLocal8Bit().constData(), errorLine, errorColumn );
      throw exMalformedConfigFile();
    }
  }
  else {
    QByteArray data = configFile.readAll();

    data.replace( "%PROGRAMDIR%", getProgramDataDir().toUtf8() );

    QBuffer bufferedData( &data );

    if ( !dd.setContent( &bufferedData, false, &errorStr, &errorLine, &errorColumn ) ) {
      qDebug( "Error: %s at %d,%d", errorStr.toLocal8Bit().constData(), errorLine, errorColumn );
      throw exMalformedConfigFile();
    }
  }

  configFile.close();

  QDomNode root = dd.namedItem( "config" );

  Class c;

  if ( isPortableVersion() ) {
    c.paths.push_back( Path( getPortableVersionDictionaryDir(), true ) );
  }

  QDomNode paths = root.namedItem( "paths" );

  if ( !paths.isNull() ) {
    QDomNodeList nl = paths.toElement().elementsByTagName( "path" );

    for ( int x = 0; x < nl.length(); ++x ) {
      c.paths.push_back(
        Path( nl.item( x ).toElement().text(), nl.item( x ).toElement().attribute( "recursive" ) == "1" ) );
    }
  }

  QDomNode soundDirs = root.namedItem( "sounddirs" );

  if ( !soundDirs.isNull() ) {
    QDomNodeList nl = soundDirs.toElement().elementsByTagName( "sounddir" );

    for ( int x = 0; x < nl.length(); ++x ) {
      c.soundDirs.push_back( SoundDir( nl.item( x ).toElement().text(),
                                       nl.item( x ).toElement().attribute( "name" ),
                                       nl.item( x ).toElement().attribute( "icon" ) ) );
    }
  }

  QDomNode dictionaryOrder = root.namedItem( "dictionaryOrder" );

  if ( !dictionaryOrder.isNull() ) {
    c.dictionaryOrder = loadGroup( dictionaryOrder.toElement() );
  }

  QDomNode inactiveDictionaries = root.namedItem( "inactiveDictionaries" );

  if ( !inactiveDictionaries.isNull() ) {
    c.inactiveDictionaries = loadGroup( inactiveDictionaries.toElement() );
  }

  QDomNode groups = root.namedItem( "groups" );

  if ( !groups.isNull() ) {
    QDomNodeList nl = groups.toElement().elementsByTagName( "group" );

    for ( int x = 0; x < nl.length(); ++x ) {
      QDomElement grp = nl.item( x ).toElement();

      c.groups.push_back( loadGroup( grp ) );
    }
  }

  QDomNode hunspell = root.namedItem( "hunspell" );

  if ( !hunspell.isNull() ) {
    c.hunspell.dictionariesPath = hunspell.toElement().attribute( "dictionariesPath" );

    QDomNodeList nl = hunspell.toElement().elementsByTagName( "enabled" );

    for ( int x = 0; x < nl.length(); ++x ) {
      c.hunspell.enabledDictionaries.push_back( nl.item( x ).toElement().text() );
    }
  }

  QDomNode transliteration = root.namedItem( "transliteration" );

  if ( !transliteration.isNull() ) {
    applyBoolOption( c.transliteration.enableRussianTransliteration,
                     transliteration.namedItem( "enableRussianTransliteration" ) );

    applyBoolOption( c.transliteration.enableGermanTransliteration,
                     transliteration.namedItem( "enableGermanTransliteration" ) );

    applyBoolOption( c.transliteration.enableGreekTransliteration,
                     transliteration.namedItem( "enableGreekTransliteration" ) );

    applyBoolOption( c.transliteration.enableBelarusianTransliteration,
                     transliteration.namedItem( "enableBelarusianTransliteration" ) );

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
    QDomNode chinese = transliteration.namedItem( "chinese" );

    if ( !chinese.isNull() ) {
      applyBoolOption( c.transliteration.chinese.enable, chinese.namedItem( "enable" ) );
      applyBoolOption( c.transliteration.chinese.enableSCToTWConversion,
                       chinese.namedItem( "enableSCToTWConversion" ) );
      applyBoolOption( c.transliteration.chinese.enableSCToHKConversion,
                       chinese.namedItem( "enableSCToHKConversion" ) );
      applyBoolOption( c.transliteration.chinese.enableTCToSCConversion,
                       chinese.namedItem( "enableTCToSCConversion" ) );
      applyBoolOption( c.transliteration.chinese.enableJapaneseConversion,
                       chinese.namedItem( "enableJapaneseConversion" ) );
    }
#endif

    QDomNode romaji = transliteration.namedItem( "romaji" );

    if ( !romaji.isNull() ) {
      applyBoolOption( c.transliteration.romaji.enable, romaji.namedItem( "enable" ) );
      applyBoolOption( c.transliteration.romaji.enableHiragana, romaji.namedItem( "enableHiragana" ) );
      applyBoolOption( c.transliteration.romaji.enableKatakana, romaji.namedItem( "enableKatakana" ) );
    }

    QDomNode customtrans = transliteration.namedItem( "customtrans" );

    if ( !customtrans.isNull() ) {
      applyBoolOption( c.transliteration.customTrans.enable, customtrans.namedItem( "enable" ) );
      c.transliteration.customTrans.context = customtrans.namedItem( "context" ).toElement().text();
    }
  }

  QDomNode programs = root.namedItem( "programs" );

  if ( !programs.isNull() ) {
    QDomNodeList nl = programs.toElement().elementsByTagName( "program" );

    for ( int x = 0; x < nl.length(); ++x ) {
      QDomElement pr = nl.item( x ).toElement();

      Program p;

      p.id           = pr.attribute( "id" );
      p.name         = pr.attribute( "name" );
      p.commandLine  = pr.attribute( "commandLine" );
      p.enabled      = ( pr.attribute( "enabled" ) == "1" );
      p.type         = (Program::Type)( pr.attribute( "type" ).toInt() );
      p.iconFilename = pr.attribute( "icon" );

      c.programs.push_back( p );
    }
  }
  else {
    c.programs = makeDefaultPrograms();
  }

  c.mutedDictionaries      = loadDictionaries( root.namedItem( "mutedDictionaries" ) );
  c.dictionariesToReindex  = loadDictionaries( root.namedItem( "dictionariesToReindex" ), "dictionary" );

  QDomNode preferences = root.namedItem( "preferences" );

  if ( !preferences.isNull() ) {
    c.preferences.newTabsOpenAfterCurrentOne =
      ( preferences.namedItem( "newTabsOpenAfterCurrentOne" ).toElement().text() == "1" );
    c.preferences.newTabsOpenInBackground =
      ( preferences.namedItem( "newTabsOpenInBackground" ).toElement().text() == "1" );
    c.preferences.hideSingleTab = ( preferences.namedItem( "hideSingleTab" ).toElement().text() == "1" );
    c.preferences.mruTabOrder   = ( preferences.namedItem( "mruTabOrder" ).toElement().text() == "1" );
    c.preferences.hideMenubar    = ( preferences.namedItem( "hideMenubar" ).toElement().text() == "1" );
    c.preferences.enableTrayIcon = ( preferences.namedItem( "enableTrayIcon" ).toElement().text() == "1" );

    if ( !preferences.namedItem( "panelsLocked" ).isNull() ) {
      c.preferences.panelsLocked = ( preferences.namedItem( "panelsLocked" ).toElement().text() == "1" );
    }
    c.preferences.startToTray    = ( preferences.namedItem( "startToTray" ).toElement().text() == "1" );
    c.preferences.closeToTray    = ( preferences.namedItem( "closeToTray" ).toElement().text() == "1" );
    if ( !preferences.namedItem( "showDockIcon" ).isNull() ) {
      c.preferences.showDockIcon = ( preferences.namedItem( "showDockIcon" ).toElement().text() == "1" );
    }
    c.preferences.autoStart    = ( preferences.namedItem( "autoStart" ).toElement().text() == "1" );
    c.preferences.alwaysOnTop  = ( preferences.namedItem( "alwaysOnTop" ).toElement().text() == "1" );
    c.preferences.searchInDock = ( preferences.namedItem( "searchInDock" ).toElement().text() == "1" );

    if ( !preferences.namedItem( "doubleClickTranslates" ).isNull() ) {
      c.preferences.doubleClickTranslates =
        ( preferences.namedItem( "doubleClickTranslates" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "selectWordBySingleClick" ).isNull() ) {
      c.preferences.selectWordBySingleClick =
        ( preferences.namedItem( "selectWordBySingleClick" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "autoScrollToTargetArticle" ).isNull() ) {
      c.preferences.autoScrollToTargetArticle =
        ( preferences.namedItem( "autoScrollToTargetArticle" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "escKeyHidesMainWindow" ).isNull() ) {
      c.preferences.escKeyHidesMainWindow =
        ( preferences.namedItem( "escKeyHidesMainWindow" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "darkMode" ).isNull() ) {
      c.preferences.darkMode = static_cast< Dark >( preferences.namedItem( "darkMode" ).toElement().text().toInt() );
    }

    if ( !preferences.namedItem( "darkReaderMode" ).isNull() ) {
      c.preferences.darkReaderMode =
        static_cast< Dark >( preferences.namedItem( "darkReaderMode" ).toElement().text().toInt() );
    }

    if ( !preferences.namedItem( "zoomFactor" ).isNull() ) {
      c.preferences.zoomFactor = preferences.namedItem( "zoomFactor" ).toElement().text().toDouble();
    }

    if ( !preferences.namedItem( "helpZoomFactor" ).isNull() ) {
      c.preferences.helpZoomFactor = preferences.namedItem( "helpZoomFactor" ).toElement().text().toDouble();
    }

    applyBoolOption( c.preferences.enableClipboardHotkey, preferences.namedItem( "enableClipboardHotkey" ) );
    if ( !preferences.namedItem( "clipboardHotkey" ).isNull() ) {
      c.preferences.clipboardHotkey =
        QKeySequence::fromString( preferences.namedItem( "clipboardHotkey" ).toElement().text() );
    }

    c.preferences.ignoreDiacritics = ( preferences.namedItem( "ignoreDiacritics" ).toElement().text() == "1" );
    if ( !preferences.namedItem( "ignorePunctuation" ).isNull() ) {
      c.preferences.ignorePunctuation = ( preferences.namedItem( "ignorePunctuation" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "sessionCollapse" ).isNull() ) {
      c.preferences.sessionCollapse = ( preferences.namedItem( "sessionCollapse" ).toElement().text() == "1" );
    }

    c.preferences.pronounceOnLoadMain  = ( preferences.namedItem( "pronounceOnLoadMain" ).toElement().text() == "1" );

    if ( InternalPlayerBackend::anyAvailable() ) {
      if ( !preferences.namedItem( "useInternalPlayer" ).isNull() ) {
        c.preferences.useInternalPlayer = ( preferences.namedItem( "useInternalPlayer" ).toElement().text() == "1" );
      }
    }
    else {
      c.preferences.useInternalPlayer = false;
    }

    if ( !preferences.namedItem( "internalPlayerBackend" ).isNull() ) {
      c.preferences.internalPlayerBackend.setName(
        preferences.namedItem( "internalPlayerBackend" ).toElement().text() );
    }

    if ( !preferences.namedItem( "audioPlaybackProgram" ).isNull() ) {
      c.preferences.audioPlaybackProgram = preferences.namedItem( "audioPlaybackProgram" ).toElement().text();
    }
    else {
      c.preferences.audioPlaybackProgram = "vlc --intf dummy --play-and-exit";
    }

    if ( !preferences.namedItem( "removeInvalidIndexOnExit" ).isNull() ) {
      c.preferences.removeInvalidIndexOnExit =
        ( preferences.namedItem( "removeInvalidIndexOnExit" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "enableApplicationLog" ).isNull() ) {
      c.preferences.enableApplicationLog =
        ( preferences.namedItem( "enableApplicationLog" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "maxStringsInHistory" ).isNull() ) {
      c.preferences.maxStringsInHistory = preferences.namedItem( "maxStringsInHistory" ).toElement().text().toUInt();
    }

    if ( !preferences.namedItem( "storeHistory" ).isNull() ) {
      c.preferences.storeHistory = preferences.namedItem( "storeHistory" ).toElement().text().toUInt();
    }

    if ( !preferences.namedItem( "alwaysExpandOptionalParts" ).isNull() ) {
      c.preferences.alwaysExpandOptionalParts =
        preferences.namedItem( "alwaysExpandOptionalParts" ).toElement().text().toUInt();
    }

    if ( !preferences.namedItem( "favoritesStoreInterval" ).isNull() ) {
      c.preferences.favoritesStoreInterval =
        preferences.namedItem( "favoritesStoreInterval" ).toElement().text().toUInt();
    }

    if ( !preferences.namedItem( "confirmFavoritesDeletion" ).isNull() ) {
      c.preferences.confirmFavoritesDeletion =
        ( preferences.namedItem( "confirmFavoritesDeletion" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "collapseBigArticles" ).isNull() ) {
      c.preferences.collapseBigArticles = ( preferences.namedItem( "collapseBigArticles" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "articleSizeLimit" ).isNull() ) {
      c.preferences.articleSizeLimit = preferences.namedItem( "articleSizeLimit" ).toElement().text().toInt();
    }

    if ( !preferences.namedItem( "limitInputPhraseLength" ).isNull() ) {
      c.preferences.limitInputPhraseLength =
        ( preferences.namedItem( "limitInputPhraseLength" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "inputPhraseLengthLimit" ).isNull() ) {
      c.preferences.inputPhraseLengthLimit =
        preferences.namedItem( "inputPhraseLengthLimit" ).toElement().text().toInt();
    }

    if ( !preferences.namedItem( "maxDictionaryRefsInContextMenu" ).isNull() ) {
      c.preferences.maxDictionaryRefsInContextMenu =
        preferences.namedItem( "maxDictionaryRefsInContextMenu" ).toElement().text().toUShort();
    }

    if ( !preferences.namedItem( "synonymSearchEnabled" ).isNull() ) {
      c.preferences.synonymSearchEnabled =
        ( preferences.namedItem( "synonymSearchEnabled" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "stripClipboard" ).isNull() ) {
      c.preferences.stripClipboard = ( preferences.namedItem( "stripClipboard" ).toElement().text() == "1" );
    }

    if ( !preferences.namedItem( "raiseWindowOnSearch" ).isNull() ) {
      c.preferences.raiseWindowOnSearch = ( preferences.namedItem( "raiseWindowOnSearch" ).toElement().text() == "1" );
    }

    QDomNode fts = preferences.namedItem( "fullTextSearch" );

    if ( !fts.isNull() ) {
      if ( !fts.namedItem( "searchMode" ).isNull() ) {
        c.preferences.fts.searchMode = fts.namedItem( "searchMode" ).toElement().text().toInt();
      }

      if ( !fts.namedItem( "dialogGeometry" ).isNull() ) {
        c.preferences.fts.dialogGeometry =
          QByteArray::fromBase64( fts.namedItem( "dialogGeometry" ).toElement().text().toLatin1() );
      }

      if ( !fts.namedItem( "disabledTypes" ).isNull() ) {
        c.preferences.fts.disabledTypes = fts.namedItem( "disabledTypes" ).toElement().text();
      }

      if ( !fts.namedItem( "enabled" ).isNull() ) {
        c.preferences.fts.enabled = ( fts.namedItem( "enabled" ).toElement().text() == "1" );
      }

      if ( !fts.namedItem( "maxDictionarySize" ).isNull() ) {
        c.preferences.fts.maxDictionarySize = fts.namedItem( "maxDictionarySize" ).toElement().text().toUInt();
      }

      if ( !fts.namedItem( "parallelThreads" ).isNull() ) {
        c.preferences.fts.parallelThreads = fts.namedItem( "parallelThreads" ).toElement().text().toUInt();
      }
    }
  }

  c.lastMainGroupId = root.namedItem( "lastMainGroupId" ).toElement().text().toUInt();

  QDomNode mainWindowState = root.namedItem( "mainWindowState" );

  if ( !mainWindowState.isNull() ) {
    c.mainWindowState = QByteArray::fromBase64( mainWindowState.toElement().text().toLatin1() );
  }

  QDomNode mainWindowGeometry = root.namedItem( "mainWindowGeometry" );

  if ( !mainWindowGeometry.isNull() ) {
    c.mainWindowGeometry = QByteArray::fromBase64( mainWindowGeometry.toElement().text().toLatin1() );
  }

  QDomNode dictInfoGeometry = root.namedItem( "dictInfoGeometry" );

  if ( !dictInfoGeometry.isNull() ) {
    c.dictInfoGeometry = QByteArray::fromBase64( dictInfoGeometry.toElement().text().toLatin1() );
  }

  QDomNode dictionariesDialogGeometry = root.namedItem( "dictionariesDialogGeometry" );

  if ( !dictionariesDialogGeometry.isNull() ) {
    c.dictionariesDialogGeometry = QByteArray::fromBase64( dictionariesDialogGeometry.toElement().text().toLatin1() );
  }

  c.showingDictBarNames = ( root.namedItem( "showingDictBarNames" ).toElement().text() == "1" );

  QDomNode usingToolbarsIconSize = root.namedItem( "usingToolbarsIconSize" );
  if ( !usingToolbarsIconSize.isNull() ) {
    c.usingToolbarsIconSize = static_cast< ToolbarsIconSize >( usingToolbarsIconSize.toElement().text().toInt() );
  }

  if ( !root.namedItem( "historyExportPath" ).isNull() ) {
    c.historyExportPath = root.namedItem( "historyExportPath" ).toElement().text();
  }

  if ( !root.namedItem( "resourceSavePath" ).isNull() ) {
    c.resourceSavePath = root.namedItem( "resourceSavePath" ).toElement().text();
  }

  if ( !root.namedItem( "articleSavePath" ).isNull() ) {
    c.articleSavePath = root.namedItem( "articleSavePath" ).toElement().text();
  }

  if ( !root.namedItem( "maxHeadwordSize" ).isNull() ) {
    unsigned int value = root.namedItem( "maxHeadwordSize" ).toElement().text().toUInt();
    if ( value != 0 ) {
      c.maxHeadwordSize = value;
    }
  }

  if ( !root.namedItem( "maxHeadwordsToExpand" ).isNull() ) {
    c.maxHeadwordsToExpand = root.namedItem( "maxHeadwordsToExpand" ).toElement().text().toUInt();
  }

  QDomNode headwordsDialog = root.namedItem( "headwordsDialog" );

  if ( !headwordsDialog.isNull() ) {
    if ( !headwordsDialog.namedItem( "searchMode" ).isNull() ) {
      c.headwordsDialog.searchMode = headwordsDialog.namedItem( "searchMode" ).toElement().text().toInt();
    }

    if ( !headwordsDialog.namedItem( "matchCase" ).isNull() ) {
      c.headwordsDialog.matchCase = ( headwordsDialog.namedItem( "matchCase" ).toElement().text() == "1" );
    }

    if ( !headwordsDialog.namedItem( "autoApply" ).isNull() ) {
      c.headwordsDialog.autoApply = ( headwordsDialog.namedItem( "autoApply" ).toElement().text() == "1" );
    }

    if ( !headwordsDialog.namedItem( "headwordsExportPath" ).isNull() ) {
      c.headwordsDialog.headwordsExportPath = headwordsDialog.namedItem( "headwordsExportPath" ).toElement().text();
    }

    if ( !headwordsDialog.namedItem( "headwordsDialogGeometry" ).isNull() ) {
      c.headwordsDialog.headwordsDialogGeometry =
        QByteArray::fromBase64( headwordsDialog.namedItem( "headwordsDialogGeometry" ).toElement().text().toLatin1() );
    }
  }

  return c;
}

namespace {
void saveGroup( const Group & data, QDomElement & group )
{
  QDomDocument dd = group.ownerDocument();

  QDomAttr id = dd.createAttribute( "id" );

  id.setValue( QString::number( data.id ) );

  group.setAttributeNode( id );

  QDomAttr name = dd.createAttribute( "name" );

  name.setValue( data.name );

  group.setAttributeNode( name );

  if ( data.favoritesFolder.size() ) {
    QDomAttr folder = dd.createAttribute( "favoritesFolder" );
    folder.setValue( data.favoritesFolder );
    group.setAttributeNode( folder );
  }

  if ( data.icon.size() ) {
    QDomAttr icon = dd.createAttribute( "icon" );

    icon.setValue( data.icon );

    group.setAttributeNode( icon );
  }

  if ( data.iconData.size() ) {
    QDomAttr iconData = dd.createAttribute( "iconData" );

    iconData.setValue( QString::fromLatin1( data.iconData.toBase64() ) );

    group.setAttributeNode( iconData );
  }

  if ( !data.shortcut.isEmpty() ) {
    QDomAttr shortcut = dd.createAttribute( "shortcut" );

    shortcut.setValue( data.shortcut.toString() );

    group.setAttributeNode( shortcut );
  }

  for ( const auto & dictionarie : data.dictionaries ) {
    QDomElement dictionary = dd.createElement( "dictionary" );

    group.appendChild( dictionary );

    QDomText value = dd.createTextNode( dictionarie.id );

    dictionary.appendChild( value );

    QDomAttr name = dd.createAttribute( "name" );

    name.setValue( dictionarie.name );

    dictionary.setAttributeNode( name );
  }

  QDomElement muted = dd.createElement( "mutedDictionaries" );
  group.appendChild( muted );

  for ( const auto & mutedDictionarie : data.mutedDictionaries ) {
    QDomElement dict = dd.createElement( "mutedDictionary" );
    muted.appendChild( dict );

    QDomText value = dd.createTextNode( mutedDictionarie );
    dict.appendChild( value );
  }
}

}

void save( const Class & c )
{

  QSaveFile configFile( getConfigFileName() );

  if ( !configFile.open( QFile::WriteOnly ) ) {
    throw exCantWriteConfigFile();
  }

  QDomDocument dd;

  QDomElement root = dd.createElement( "config" );
  dd.appendChild( root );

  {
    QDomElement paths = dd.createElement( "paths" );
    root.appendChild( paths );

    qsizetype pos = Config::isPortableVersion();

    for ( const auto & i : c.paths.mid( pos ) ) {
      QDomElement path = dd.createElement( "path" );
      paths.appendChild( path );

      QDomAttr recursive = dd.createAttribute( "recursive" );
      recursive.setValue( i.recursive ? "1" : "0" );
      path.setAttributeNode( recursive );

      QDomText value = dd.createTextNode( i.path );

      path.appendChild( value );
    }
  }

  {
    QDomElement soundDirs = dd.createElement( "sounddirs" );
    root.appendChild( soundDirs );

    for ( const auto & i : c.soundDirs ) {
      QDomElement soundDir = dd.createElement( "sounddir" );
      soundDirs.appendChild( soundDir );

      QDomAttr name = dd.createAttribute( "name" );
      name.setValue( i.name );
      soundDir.setAttributeNode( name );

      QDomAttr icon = dd.createAttribute( "icon" );
      icon.setValue( i.iconFilename );
      soundDir.setAttributeNode( icon );

      QDomText value = dd.createTextNode( i.path );

      soundDir.appendChild( value );
    }
  }

  {
    QDomElement dictionaryOrder = dd.createElement( "dictionaryOrder" );
    root.appendChild( dictionaryOrder );
    saveGroup( c.dictionaryOrder, dictionaryOrder );
  }

  {
    QDomElement inactiveDictionaries = dd.createElement( "inactiveDictionaries" );
    root.appendChild( inactiveDictionaries );
    saveGroup( c.inactiveDictionaries, inactiveDictionaries );
  }

  {
    QDomElement groups = dd.createElement( "groups" );
    root.appendChild( groups );

    for ( const auto & i : c.groups ) {
      QDomElement group = dd.createElement( "group" );
      groups.appendChild( group );

      saveGroup( i, group );
    }
  }

  {
    QDomElement hunspell = dd.createElement( "hunspell" );
    QDomAttr path        = dd.createAttribute( "dictionariesPath" );
    path.setValue( c.hunspell.dictionariesPath );
    hunspell.setAttributeNode( path );
    root.appendChild( hunspell );

    for ( const auto & enabledDictionarie : c.hunspell.enabledDictionaries ) {
      QDomElement en = dd.createElement( "enabled" );
      QDomText value = dd.createTextNode( enabledDictionarie );

      en.appendChild( value );
      hunspell.appendChild( en );
    }
  }

  {

    QDomElement transliteration = dd.createElement( "transliteration" );
    root.appendChild( transliteration );

    QDomElement opt = dd.createElement( "enableRussianTransliteration" );
    opt.appendChild( dd.createTextNode( c.transliteration.enableRussianTransliteration ? "1" : "0" ) );
    transliteration.appendChild( opt );

    opt = dd.createElement( "enableGermanTransliteration" );
    opt.appendChild( dd.createTextNode( c.transliteration.enableGermanTransliteration ? "1" : "0" ) );
    transliteration.appendChild( opt );

    opt = dd.createElement( "enableGreekTransliteration" );
    opt.appendChild( dd.createTextNode( c.transliteration.enableGreekTransliteration ? "1" : "0" ) );
    transliteration.appendChild( opt );

    opt = dd.createElement( "enableBelarusianTransliteration" );
    opt.appendChild( dd.createTextNode( c.transliteration.enableBelarusianTransliteration ? "1" : "0" ) );
    transliteration.appendChild( opt );

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT

    QDomElement chinese = dd.createElement( "chinese" );
    transliteration.appendChild( chinese );

    opt = dd.createElement( "enable" );
    opt.appendChild( dd.createTextNode( c.transliteration.chinese.enable ? "1" : "0" ) );
    chinese.appendChild( opt );

    opt = dd.createElement( "enableSCToTWConversion" );
    opt.appendChild( dd.createTextNode( c.transliteration.chinese.enableSCToTWConversion ? "1" : "0" ) );
    chinese.appendChild( opt );

    opt = dd.createElement( "enableSCToHKConversion" );
    opt.appendChild( dd.createTextNode( c.transliteration.chinese.enableSCToHKConversion ? "1" : "0" ) );
    chinese.appendChild( opt );

    opt = dd.createElement( "enableTCToSCConversion" );
    opt.appendChild( dd.createTextNode( c.transliteration.chinese.enableTCToSCConversion ? "1" : "0" ) );
    chinese.appendChild( opt );

    opt = dd.createElement( "enableJapaneseConversion" );
    opt.appendChild( dd.createTextNode( c.transliteration.chinese.enableJapaneseConversion ? "1" : "0" ) );
    chinese.appendChild( opt );
#endif

    QDomElement romaji = dd.createElement( "romaji" );
    transliteration.appendChild( romaji );

    opt = dd.createElement( "enable" );
    opt.appendChild( dd.createTextNode( c.transliteration.romaji.enable ? "1" : "0" ) );
    romaji.appendChild( opt );

    opt = dd.createElement( "enableHiragana" );
    opt.appendChild( dd.createTextNode( c.transliteration.romaji.enableHiragana ? "1" : "0" ) );
    romaji.appendChild( opt );

    opt = dd.createElement( "enableKatakana" );
    opt.appendChild( dd.createTextNode( c.transliteration.romaji.enableKatakana ? "1" : "0" ) );
    romaji.appendChild( opt );

    QDomElement customtrans = dd.createElement( "customtrans" );
    transliteration.appendChild( customtrans );

    opt = dd.createElement( "enable" );
    opt.appendChild( dd.createTextNode( c.transliteration.customTrans.enable ? "1" : "0" ) );
    customtrans.appendChild( opt );

    opt = dd.createElement( "context" );
    opt.appendChild( dd.createTextNode( c.transliteration.customTrans.context ) );
    customtrans.appendChild( opt );
  }

  {
    QDomElement programs = dd.createElement( "programs" );
    root.appendChild( programs );

    for ( const auto & program : c.programs ) {
      QDomElement p = dd.createElement( "program" );
      programs.appendChild( p );

      QDomAttr id = dd.createAttribute( "id" );
      id.setValue( program.id );
      p.setAttributeNode( id );

      QDomAttr name = dd.createAttribute( "name" );
      name.setValue( program.name );
      p.setAttributeNode( name );

      QDomAttr commandLine = dd.createAttribute( "commandLine" );
      commandLine.setValue( program.commandLine );
      p.setAttributeNode( commandLine );

      QDomAttr enabled = dd.createAttribute( "enabled" );
      enabled.setValue( program.enabled ? "1" : "0" );
      p.setAttributeNode( enabled );

      QDomAttr type = dd.createAttribute( "type" );
      type.setValue( QString::number( program.type ) );
      p.setAttributeNode( type );

      QDomAttr icon = dd.createAttribute( "icon" );
      icon.setValue( program.iconFilename );
      p.setAttributeNode( icon );
    }
  }
  {
    QDomElement muted = dd.createElement( "mutedDictionaries" );
    root.appendChild( muted );
    saveDictionaries( dd, muted, c.mutedDictionaries );
  }

  {
    QDomElement reindex = dd.createElement( "dictionariesToReindex" );
    root.appendChild( reindex );
    saveDictionaries( dd, reindex, c.dictionariesToReindex, "dictionary" );
  }

  {
    QDomElement preferences = dd.createElement( "preferences" );
    root.appendChild( preferences );

    QDomElement opt = dd.createElement( "newTabsOpenAfterCurrentOne" );
    opt.appendChild( dd.createTextNode( c.preferences.newTabsOpenAfterCurrentOne ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "newTabsOpenInBackground" );
    opt.appendChild( dd.createTextNode( c.preferences.newTabsOpenInBackground ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "hideSingleTab" );
    opt.appendChild( dd.createTextNode( c.preferences.hideSingleTab ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "mruTabOrder" );
    opt.appendChild( dd.createTextNode( c.preferences.mruTabOrder ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "hideMenubar" );
    opt.appendChild( dd.createTextNode( c.preferences.hideMenubar ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "panelsLocked" );
    opt.appendChild( dd.createTextNode( c.preferences.panelsLocked ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "enableTrayIcon" );
    opt.appendChild( dd.createTextNode( c.preferences.enableTrayIcon ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "startToTray" );
    opt.appendChild( dd.createTextNode( c.preferences.startToTray ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "closeToTray" );
    opt.appendChild( dd.createTextNode( c.preferences.closeToTray ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "showDockIcon" );
    opt.appendChild( dd.createTextNode( c.preferences.showDockIcon ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "autoStart" );
    opt.appendChild( dd.createTextNode( c.preferences.autoStart ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "doubleClickTranslates" );
    opt.appendChild( dd.createTextNode( c.preferences.doubleClickTranslates ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "selectWordBySingleClick" );
    opt.appendChild( dd.createTextNode( c.preferences.selectWordBySingleClick ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "autoScrollToTargetArticle" );
    opt.appendChild( dd.createTextNode( c.preferences.autoScrollToTargetArticle ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "escKeyHidesMainWindow" );
    opt.appendChild( dd.createTextNode( c.preferences.escKeyHidesMainWindow ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "darkMode" );
    opt.appendChild( dd.createTextNode( QString::number( static_cast< int >( c.preferences.darkMode ) ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "darkReaderMode" );
    opt.appendChild( dd.createTextNode( QString::number( static_cast< int >( c.preferences.darkReaderMode ) ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "zoomFactor" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.zoomFactor ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "helpZoomFactor" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.helpZoomFactor ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "enableClipboardHotkey" );
    opt.appendChild( dd.createTextNode( c.preferences.enableClipboardHotkey ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "clipboardHotkey" );
    opt.appendChild( dd.createTextNode( c.preferences.clipboardHotkey.toString() ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "ignoreDiacritics" );
    opt.appendChild( dd.createTextNode( c.preferences.ignoreDiacritics ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "ignorePunctuation" );
    opt.appendChild( dd.createTextNode( c.preferences.ignorePunctuation ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "sessionCollapse" );
    opt.appendChild( dd.createTextNode( c.preferences.sessionCollapse ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "pronounceOnLoadMain" );
    opt.appendChild( dd.createTextNode( c.preferences.pronounceOnLoadMain ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "useInternalPlayer" );
    opt.appendChild( dd.createTextNode( c.preferences.useInternalPlayer ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "internalPlayerBackend" );
    opt.appendChild( dd.createTextNode( c.preferences.internalPlayerBackend.getName() ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "audioPlaybackProgram" );
    opt.appendChild( dd.createTextNode( c.preferences.audioPlaybackProgram ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "alwaysOnTop" );
    opt.appendChild( dd.createTextNode( c.preferences.alwaysOnTop ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "searchInDock" );
    opt.appendChild( dd.createTextNode( c.preferences.searchInDock ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "favoritesStoreInterval" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.favoritesStoreInterval ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "confirmFavoritesDeletion" );
    opt.appendChild( dd.createTextNode( c.preferences.confirmFavoritesDeletion ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "removeInvalidIndexOnExit" );
    opt.appendChild( dd.createTextNode( c.preferences.removeInvalidIndexOnExit ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "enableApplicationLog" );
    opt.appendChild( dd.createTextNode( c.preferences.enableApplicationLog ? "1" : "0" ) );
    preferences.appendChild( opt );
    opt = dd.createElement( "maxStringsInHistory" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.maxStringsInHistory ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "storeHistory" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.storeHistory ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "alwaysExpandOptionalParts" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.alwaysExpandOptionalParts ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "collapseBigArticles" );
    opt.appendChild( dd.createTextNode( c.preferences.collapseBigArticles ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "articleSizeLimit" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.articleSizeLimit ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "limitInputPhraseLength" );
    opt.appendChild( dd.createTextNode( c.preferences.limitInputPhraseLength ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "inputPhraseLengthLimit" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.inputPhraseLengthLimit ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "maxDictionaryRefsInContextMenu" );
    opt.appendChild( dd.createTextNode( QString::number( c.preferences.maxDictionaryRefsInContextMenu ) ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "synonymSearchEnabled" );
    opt.appendChild( dd.createTextNode( c.preferences.synonymSearchEnabled ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "stripClipboard" );
    opt.appendChild( dd.createTextNode( c.preferences.stripClipboard ? "1" : "0" ) );
    preferences.appendChild( opt );

    opt = dd.createElement( "raiseWindowOnSearch" );
    opt.appendChild( dd.createTextNode( c.preferences.raiseWindowOnSearch ? "1" : "0" ) );
    preferences.appendChild( opt );

    {
      QDomNode hd = dd.createElement( "fullTextSearch" );
      preferences.appendChild( hd );

      QDomElement opt = dd.createElement( "searchMode" );
      opt.appendChild( dd.createTextNode( QString::number( c.preferences.fts.searchMode ) ) );
      hd.appendChild( opt );

      opt = dd.createElement( "dialogGeometry" );
      opt.appendChild( dd.createTextNode( QString::fromLatin1( c.preferences.fts.dialogGeometry.toBase64() ) ) );
      hd.appendChild( opt );

      opt = dd.createElement( "disabledTypes" );
      opt.appendChild( dd.createTextNode( c.preferences.fts.disabledTypes ) );
      hd.appendChild( opt );

      opt = dd.createElement( "enabled" );
      opt.appendChild( dd.createTextNode( c.preferences.fts.enabled ? "1" : "0" ) );
      hd.appendChild( opt );

      opt = dd.createElement( "maxDictionarySize" );
      opt.appendChild( dd.createTextNode( QString::number( c.preferences.fts.maxDictionarySize ) ) );
      hd.appendChild( opt );

      opt = dd.createElement( "parallelThreads" );
      opt.appendChild( dd.createTextNode( QString::number( c.preferences.fts.parallelThreads ) ) );
      hd.appendChild( opt );
    }
  }

  {
    QDomElement opt = dd.createElement( "lastMainGroupId" );
    opt.appendChild( dd.createTextNode( QString::number( c.lastMainGroupId ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "mainWindowState" );
    opt.appendChild( dd.createTextNode( QString::fromLatin1( c.mainWindowState.toBase64() ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "mainWindowGeometry" );
    opt.appendChild( dd.createTextNode( QString::fromLatin1( c.mainWindowGeometry.toBase64() ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "dictInfoGeometry" );
    opt.appendChild( dd.createTextNode( QString::fromLatin1( c.dictInfoGeometry.toBase64() ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "dictionariesDialogGeometry" );
    opt.appendChild( dd.createTextNode( QString::fromLatin1( c.dictionariesDialogGeometry.toBase64() ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "showingDictBarNames" );
    opt.appendChild( dd.createTextNode( c.showingDictBarNames ? "1" : "0" ) );
    root.appendChild( opt );

    opt = dd.createElement( "usingToolbarsIconSize" );
    opt.appendChild( dd.createTextNode( QString::number( static_cast< int >( c.usingToolbarsIconSize ) ) ) );
    root.appendChild( opt );

    if ( !c.historyExportPath.isEmpty() ) {
      opt = dd.createElement( "historyExportPath" );
      opt.appendChild( dd.createTextNode( c.historyExportPath ) );
      root.appendChild( opt );
    }

    if ( !c.resourceSavePath.isEmpty() ) {
      opt = dd.createElement( "resourceSavePath" );
      opt.appendChild( dd.createTextNode( c.resourceSavePath ) );
      root.appendChild( opt );
    }

    if ( !c.articleSavePath.isEmpty() ) {
      opt = dd.createElement( "articleSavePath" );
      opt.appendChild( dd.createTextNode( c.articleSavePath ) );
      root.appendChild( opt );
    }

    opt = dd.createElement( "maxHeadwordSize" );
    opt.appendChild( dd.createTextNode( QString::number( c.maxHeadwordSize ) ) );
    root.appendChild( opt );

    opt = dd.createElement( "maxHeadwordsToExpand" );
    opt.appendChild( dd.createTextNode( QString::number( c.maxHeadwordsToExpand ) ) );
    root.appendChild( opt );
  }

  {
    QDomNode hd = dd.createElement( "headwordsDialog" );
    root.appendChild( hd );

    QDomElement opt = dd.createElement( "searchMode" );
    opt.appendChild( dd.createTextNode( QString::number( c.headwordsDialog.searchMode ) ) );
    hd.appendChild( opt );

    opt = dd.createElement( "matchCase" );
    opt.appendChild( dd.createTextNode( c.headwordsDialog.matchCase ? "1" : "0" ) );
    hd.appendChild( opt );

    opt = dd.createElement( "autoApply" );
    opt.appendChild( dd.createTextNode( c.headwordsDialog.autoApply ? "1" : "0" ) );
    hd.appendChild( opt );

    opt = dd.createElement( "headwordsExportPath" );
    opt.appendChild( dd.createTextNode( c.headwordsDialog.headwordsExportPath ) );
    hd.appendChild( opt );

    opt = dd.createElement( "headwordsDialogGeometry" );
    opt.appendChild( dd.createTextNode( QString::fromLatin1( c.headwordsDialog.headwordsDialogGeometry.toBase64() ) ) );
    hd.appendChild( opt );
  }

  configFile.write( dd.toByteArray() );
  if ( !configFile.commit() ) {
    throw exCantWriteConfigFile();
  }
}

QString getConfigFileName()
{
  return getHomeDir().absoluteFilePath( "config" );
}

QString getConfigDir()
{
  return getHomeDir().path() + QDir::separator();
}

QString getIndexDir()
{
  QDir result = getHomeDir();

#ifdef XDG_BASE_DIRECTORY_COMPLIANCE
  if ( !isPortableVersion() && !result.exists( "index" ) ) {
    result.setPath( getCacheDir() );
  }
#endif

  result.mkpath( "index" );

  if ( !result.cd( "index" ) ) {
    throw exCantUseIndexDir();
  }

  return result.path() + QDir::separator();
}

QString getPidFileName()
{
  return getHomeDir().filePath( "pid" );
}

QDir getHomeDir()
{
  if ( isPortableVersion() ) {
    return QDir( portableHomeDirPath() );
  }

  QDir result;

  result = QDir::home();
  char const * pathInHome = ".microdict";
  #ifdef XDG_BASE_DIRECTORY_COMPLIANCE
  result.setPath( QStandardPaths::writableLocation( QStandardPaths::ConfigLocation ) );
  pathInHome = xdgSubdirName;
  #endif

  result.mkpath( pathInHome );

  if ( !result.cd( pathInHome ) ) {
    throw exCantUseHomeDir();
  }

  return result;
}
QString getHistoryFileName()
{
  QString homeHistoryPath = getHomeDir().filePath( "history" );

#ifdef XDG_BASE_DIRECTORY_COMPLIANCE
  if ( !isPortableVersion() && !QFile::exists( homeHistoryPath ) ) {
    return getDataDir().filePath( "history" );
  }
#endif

  return homeHistoryPath;
}

QString getFavoritiesFileName()
{
  return getHomeDir().filePath( "favorites" );
}

QString getProgramDataDir() noexcept
{
  if ( isPortableVersion() ) {
    return QCoreApplication::applicationDirPath();
  }
#ifdef Q_OS_MACOS
  return QCoreApplication::applicationDirPath();
#else
  return QDir::cleanPath( QCoreApplication::applicationDirPath() + "/../share/microdict" );
#endif
}

QString getLocDir() noexcept
{
  return QStringLiteral( ":/locale" );
}

QString getHelpDir() noexcept
{
  if ( QDir( getProgramDataDir() ).cd( "help" ) ) {
    return getProgramDataDir() + "/help";
  }
  else {
    return QCoreApplication::applicationDirPath() + "/help";
  }
}

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
QString getOpenCCDir() noexcept
{
  #ifdef Q_OS_MAC
  QString path = QCoreApplication::applicationDirPath() + "/opencc";
  if ( QDir( path ).exists() ) {
    return path;
  }

  return QString();
  #else
  const QString bundled = getProgramDataDir() + "/opencc";
  return QDir( bundled ).exists() ? bundled : QString();
  #endif
}
#endif

bool isPortableVersion() noexcept
{
  struct IsPortable
  {
    bool isPortable;

    IsPortable():
      isPortable( QFileInfo( portableHomeDirPath() ).isDir() )
    {
    }
  };

  static IsPortable p;

  return p.isPortable;
}

QString getPortableVersionDictionaryDir() noexcept
{
  if ( isPortableVersion() ) {
    return getProgramDataDir() + "/content";
  }
  else {
    return QString();
  }
}

QString getPortableVersionMorphoDir() noexcept
{
  if ( isPortableVersion() ) {
    return getPortableVersionDictionaryDir() + "/morphology";
  }
  else {
    return QString();
  }
}

QString getCacheDir() noexcept
{
  return isPortableVersion() ? portableHomeDirPath() + "/cache"
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
                             :
                             QStandardPaths::writableLocation( QStandardPaths::GenericCacheLocation ) + '/'
                               + xdgSubdirName;
#else
                             :
                             QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
#endif
}
}
