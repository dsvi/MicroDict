/* Licensed under GPLv3 or later, see the LICENSE file */

#include <QApplication>
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QKeySequence>
#include <QFile>
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <QDebug>

#include "lite_theme.hh"
#include "portal_filechooser.hh"
#include "portalshortcuts.hh"

namespace {

const QString kDesktop = QStringLiteral( "org.freedesktop.portal.Desktop" );
const QString kDesktopPath = QStringLiteral( "/org/freedesktop/portal/desktop" );
const QString kGlobalShortcuts = QStringLiteral( "org.freedesktop.portal.GlobalShortcuts" );
const QString kProperties = QStringLiteral( "org.freedesktop.DBus.Properties" );
const QString kAppearance = QStringLiteral( "org.freedesktop.appearance" );
const QString kColorScheme = QStringLiteral( "color-scheme" );
const QString kKdeGlobals = QStringLiteral( "org.kde.kdeglobals" );
const QString kAccentColor = QStringLiteral( "accent-color" );
const QString kSettings        = QStringLiteral( "org.freedesktop.portal.Settings" );

int failures = 0;

void say( const QString & line )
{
  static const QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();

  fprintf( stdout, "[%6.2fs] %s\n", clock.elapsed() / 1000.0, line.toUtf8().constData() );
  fflush( stdout );
}

void check( bool condition, const char * what, int line )
{
  if ( condition )
    say( QStringLiteral( "  ok   %1" ).arg( QLatin1String( what ) ) );
  else {
    ++failures;
    say( QStringLiteral( "  FAIL %1 (line %2)" ).arg( QLatin1String( what ) ).arg( line ) );
  }
}

#define CHECK( condition ) check( condition, #condition, __LINE__ )

bool waitFor( const std::function< bool() > & condition, int timeoutMs = 4000 )
{
  QDeadlineTimer deadline( timeoutMs );

  while ( !condition() && !deadline.hasExpired() )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );

  return condition();
}

struct MockShortcut {
  QString id;
  QVariantMap properties;
};

QDBusArgument & operator<<( QDBusArgument & arg, const MockShortcut & e )
{
  arg.beginStructure();
  arg << e.id << e.properties;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, MockShortcut & e )
{
  arg.beginStructure();
  arg >> e.id >> e.properties;
  arg.endStructure();
  return arg;
}

struct MockFilterCondition {
  int type = 0;
  QString pattern;
};

struct MockFilter {
  QString name;
  QList< MockFilterCondition > conditions;
};

QDBusArgument & operator<<( QDBusArgument & arg, const MockFilterCondition & c )
{
  arg.beginStructure();
  arg << c.type << c.pattern;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, MockFilterCondition & c )
{
  arg.beginStructure();
  arg >> c.type >> c.pattern;
  arg.endStructure();
  return arg;
}

QDBusArgument & operator<<( QDBusArgument & arg, const MockFilter & f )
{
  arg.beginStructure();
  arg << f.name << f.conditions;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, MockFilter & f )
{
  arg.beginStructure();
  arg >> f.name >> f.conditions;
  arg.endStructure();
  return arg;
}

struct MockAccent {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
};

QDBusArgument & operator<<( QDBusArgument & arg, const MockAccent & c )
{
  arg.beginStructure();
  arg << c.r << c.g << c.b;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, MockAccent & c )
{
  arg.beginStructure();
  arg >> c.r >> c.g >> c.b;
  arg.endStructure();
  return arg;
}

struct ChooserState {
  int openCalls = 0;
  int saveCalls = 0;
  int directoryCalls = 0;
  QString lastTitle;
  QString lastParentWindow;
  QString lastCurrentFolder;
  QStringList lastOptionKeys;
  bool lastFiltersGiven = false;
  QString lastFilterType;
  bool lastDirectory = false;
  bool lastMultiple = false;

  QStringList answer = { QStringLiteral( "file:///tmp/portalmock-picked.txt" ) };
  QString answerFilter;
  uint response = 0;
};

struct PortalState {
  int sessionsCreated = 0;
  int bindCalls = 0;
  int sessionsClosed = 0;
  QStringList closedSessions;
  QStringList boundIds;
  QMap< QString, QString > preferredTriggers;
  QMap< QString, QString > descriptions;
  QString boundSession;
  QString lastSessionPath;

  uint bindResponse = 0;
};

}

class RequestAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.freedesktop.portal.Request" )

public:
  explicit RequestAdaptor( QObject * parent ):
    QDBusAbstractAdaptor( parent )
  {}

  void respond( uint response, const QVariantMap & results )
  {
    emit Response( response, results );
  }

  bool wasClosed = false;

public slots:
  void Close()
  {
    wasClosed = true;
  }

signals:
  void Response( uint response, const QVariantMap & results );
};

class SessionAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.freedesktop.portal.Session" )

public:
  SessionAdaptor( QObject * parent, PortalState * state, const QString & path ):
    QDBusAbstractAdaptor( parent ),
    state( state ),
    path( path )
  {}

public slots:
  void Close()
  {
    ++state->sessionsClosed;
    state->closedSessions << path;
    emit Closed();
  }

signals:
  void Closed();

private:
  PortalState * state;
  QString path;
};

class DesktopObject: public QObject, public QDBusContext
{
  Q_OBJECT

public:
  explicit DesktopObject( QObject * parent = nullptr ):
    QObject( parent )
  {}

  QString requestPath( const QString & token ) const
  {
    QString sender = message().service();

    sender.remove( QLatin1Char( ':' ) );
    sender.replace( QLatin1Char( '.' ), QLatin1Char( '_' ) );

    return QStringLiteral( "%1/request/%2/%3" ).arg( kDesktopPath, sender, token );
  }

  RequestAdaptor * newRequest( const QString & token )
  {
    QObject * object = new QObject();
    object->setParent( this );

    RequestAdaptor * adaptor = new RequestAdaptor( object );

    QDBusConnection::sessionBus().registerObject( requestPath( token ), object,
        QDBusConnection::ExportAdaptors );

    return adaptor;
  }
};

const char * kdeGlobalsDark()
{
  return "[General]\nColorScheme=BreezeDark\n\n"
         "[Colors:Window]\nBackgroundNormal=23,23,23\nForegroundNormal=239,239,239\n"
         "ForegroundInactive=125,125,125\nForegroundNegative=231,76,60\n\n"
         "[Colors:View]\nBackgroundNormal=42,42,42\nForegroundNormal=239,239,239\n"
         "BackgroundAlternate=48,48,48\nForegroundInactive=125,125,125\nForegroundLink=255,132,60\n\n"
         "[Colors:Button]\nBackgroundNormal=35,35,35\nForegroundNormal=239,239,239\n\n"
         "[Colors:Tooltip]\nBackgroundNormal=35,35,35\nForegroundNormal=239,239,239\n";
}

const char * kdeGlobalsLight()
{
  return "[General]\nColorScheme=BreezeLight\n\n"
         "[Colors:Window]\nBackgroundNormal=239,240,241\nForegroundNormal=23,38,41\n"
         "ForegroundInactive=100,116,121\nForegroundNegative=217,0,0\n\n"
         "[Colors:View]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n"
         "BackgroundAlternate=247,247,247\nForegroundInactive=100,116,121\nForegroundLink=192,93,0\n\n"
         "[Colors:Button]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n\n"
         "[Colors:Tooltip]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n";
}

const char * kdeGlobalsPlain()
{
  return "[Colors:Window]\nBackgroundNormal=239,240,241\nForegroundNormal=23,38,41\n\n"
         "[Colors:View]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n\n"
         "[Colors:Button]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n\n"
         "[Colors:Tooltip]\nBackgroundNormal=252,252,252\nForegroundNormal=23,38,41\n";
}

bool writeKdeGlobals( const QString & dir, const char * text )
{
  if ( dir.isEmpty() ) {
    return false;
  }

  QDir().mkpath( dir );

  const QString path = dir + QStringLiteral( "/kdeglobals" );
  const QString tmp  = path + QStringLiteral( ".mock" );

  {
    QFile file( tmp );

    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
      return false;
    }

    file.write( text );
    file.close();
  }

  QFile::remove( path );
  return QFile::rename( tmp, path );
}

class SettingsAdaptor: public QDBusAbstractAdaptor, public QDBusContext
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.freedesktop.portal.Settings" )
  Q_PROPERTY( quint32 version READ version )

public:
  explicit SettingsAdaptor( QObject * parent ):
    QDBusAbstractAdaptor( parent )
  {}

  quint32 version() const
  {
    return 2;
  }

  QString kdeGlobalsPath() const
  {
    return configDir + QStringLiteral( "/kdeglobals" );
  }

  void writeTo( const QString & dir )
  {
    configDir = dir;
  }

  void makeDark( bool dark )
  {
    colorScheme = dark ? 1u : 2u;
    groups = dark ? darkGroups() : lightGroups();

    if ( !writeKdeGlobals( configDir, dark ? kdeGlobalsDark() : kdeGlobalsLight() ) ) {
      say( QStringLiteral( "  could not write %1/kdeglobals - the file stayed unchecked" ).arg( configDir ) );
    }

    emitChanged( kAppearance, kColorScheme, uint( colorScheme ) );
    emitChanged( kAppearance, kAccentColor, accentOrange() );

    for ( auto it = groups.constBegin(); it != groups.constEnd(); ++it ) {
      if ( !it.key().startsWith( kKdeGlobals + QLatin1Char( '.' ) ) )
        continue;

      const QVariantMap entries = it.value().toMap();

      if ( entries.isEmpty() )
        continue;

      QVariant value = entries.first();

      if ( value.metaType() == QMetaType::fromType< QDBusVariant >() )
        value = value.value< QDBusVariant >().variant();

      emitChanged( it.key(), QStringLiteral( "BackgroundNormal" ), value );
    }
  }

  void makeAccentOnly()
  {
    colorScheme = 2;
    groups      = appearanceOnlyGroups( true );

    writeKdeGlobals( configDir, kdeGlobalsPlain() );

    emitChanged( kAppearance, kAccentColor, greenAccent() );
  }

  void makeNoAccent()
  {
    colorScheme = 2;
    groups      = appearanceOnlyGroups( false );

    writeKdeGlobals( configDir, kdeGlobalsPlain() );

    emitChanged( kAppearance, kColorScheme, uint( 2 ) );
  }

  void emitChanged( const QString & space, const QString & key, const QVariant & value )
  {
    emit SettingChanged( space, key, QDBusVariant( value ) );
  }

  int readCalls() const
  {
    return reads;
  }

public slots:
  QDBusVariant Read( const QString & space, const QString & key )
  {
    return readValue( space, key );
  }

  QDBusVariant ReadOne( const QString & space, const QString & key )
  {
    return readValue( space, key );
  }

signals:
  void SettingChanged( const QString & space, const QString & key, const QDBusVariant & value );

private:
  QDBusVariant readValue( const QString & space, const QString & key )
  {
    ++reads;

    const QVariantMap entries = groups.value( space ).toMap();

    if ( entries.contains( key ) ) {
      const QVariant wrappedValue = entries.value( key );

      if ( wrappedValue.metaType() == QMetaType::fromType< QDBusVariant >() )
        return wrappedValue.value< QDBusVariant >();

      return QDBusVariant( wrappedValue );
    }

    return QDBusVariant( QString() );
  }

  static QVariantMap wrapped( const QVariantMap & keys )
  {
    QVariantMap out;

    for ( auto it = keys.constBegin(); it != keys.constEnd(); ++it )
      out.insert( it.key(), QVariant::fromValue( QDBusVariant( it.value() ) ) );

    return out;
  }

  static QVariant accentOrange()
  {
    return QVariant::fromValue( MockAccent { 1.0, 132.0 / 255.0, 60.0 / 255.0 } );
  }

  static QVariant greenAccent()
  {
    return QVariant::fromValue( MockAccent{ 0.0, 1.0, 0.0 } );
  }

  static QVariantMap appearanceOnlyGroups( bool withAccent )
  {
    QVariantMap appearance;
    appearance.insert( kColorScheme, uint( 2 ) );

    if ( withAccent ) {
      appearance.insert( kAccentColor, greenAccent() );
    }

    QVariantMap g;
    g.insert( kAppearance, wrapped( appearance ) );

    return g;
  }

  static QVariantMap darkGroups()
  {
    QVariantMap g;

    g.insert( kAppearance, wrapped( {
      { kColorScheme, uint( 1 ) },
      { kAccentColor, accentOrange() } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Window" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "9,9,9" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "250,250,250" ) },
                         { QStringLiteral( "ForegroundInactive" ), QStringLiteral( "7,7,7" ) },
                         { QStringLiteral( "ForegroundNegative" ), QStringLiteral( "6,6,6" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:View" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "11,11,11" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "250,250,250" ) },
                         { QStringLiteral( "BackgroundAlternate" ), QStringLiteral( "10,10,10" ) },
                         { QStringLiteral( "ForegroundInactive" ), QStringLiteral( "8,8,8" ) },
                         { QStringLiteral( "ForegroundLink" ), QStringLiteral( "1,2,3" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Selection" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "38,120,196" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "23,23,23" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Button" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "13,13,13" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "250,250,250" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Tooltip" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "13,13,13" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "250,250,250" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".General" ),
              wrapped( { { QStringLiteral( "ColorScheme" ), QStringLiteral( "DecoyDark" ) } } ) );

    return g;
  }

  static QVariantMap lightGroups()
  {
    QVariantMap g;

    g.insert( kAppearance, wrapped( {
      { kColorScheme, uint( 2 ) },
      { kAccentColor, accentOrange() } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Window" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "250,250,250" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "9,9,9" ) },
                         { QStringLiteral( "ForegroundInactive" ), QStringLiteral( "7,7,7" ) },
                         { QStringLiteral( "ForegroundNegative" ), QStringLiteral( "6,6,6" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:View" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "251,251,251" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "11,11,11" ) },
                         { QStringLiteral( "BackgroundAlternate" ), QStringLiteral( "250,250,250" ) },
                         { QStringLiteral( "ForegroundInactive" ), QStringLiteral( "8,8,8" ) },
                         { QStringLiteral( "ForegroundLink" ), QStringLiteral( "4,5,6" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Selection" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "38,120,196" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "23,38,41" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Button" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "249,249,249" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "13,13,13" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".Colors:Tooltip" ),
              wrapped( { { QStringLiteral( "BackgroundNormal" ), QStringLiteral( "249,249,249" ) },
                         { QStringLiteral( "ForegroundNormal" ), QStringLiteral( "13,13,13" ) } } ) );
    g.insert( kKdeGlobals + QStringLiteral( ".General" ),
              wrapped( { { QStringLiteral( "ColorScheme" ), QStringLiteral( "DecoyLight" ) } } ) );

    return g;
  }

  uint colorScheme   = 1;
  QVariantMap groups = darkGroups();
  int reads = 0;
  QString configDir  = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );
};

struct AccelState
{
  int cleanUpCalls = 0;
  QStringList cleanedPaths;
  QStringList componentPaths;
};

class ComponentAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.kde.kglobalaccel.Component" )

public:
  ComponentAdaptor( QObject * parent, const QString & path, const QStringList & names, AccelState * state ):
    QDBusAbstractAdaptor( parent ),
    path( path ),
    names( names ),
    state( state )
  {
  }

public slots:
  QStringList shortcutNames() const
  {
    return names;
  }

  bool cleanUp()
  {
    ++state->cleanUpCalls;
    state->cleanedPaths << path;
    return true;
  }

private:
  QString path;
  QStringList names;
  AccelState * state;
};

class KdeAccelAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.kde.KGlobalAccel" )

public:
  explicit KdeAccelAdaptor( QObject * parent, AccelState * state ):
    QDBusAbstractAdaptor( parent ),
    state( state )
  {
  }

public slots:
  QList< QDBusObjectPath > allComponents() const
  {
    QList< QDBusObjectPath > paths;

    for ( const QString & path : state->componentPaths ) {
      paths << QDBusObjectPath( path );
    }

    return paths;
  }

private:
  AccelState * state;
};

bool serveKdeAccel( AccelState & accel )
{
  accel.componentPaths = QStringList() << QStringLiteral( "/component/io_github_dsvi_microdict" )
                                       << QStringLiteral( "/component/some_other_app" );

  QDBusConnection bus = QDBusConnection::sessionBus();

  QObject * accelObject = new QObject();
  new KdeAccelAdaptor( accelObject, &accel );

  if ( !bus.registerObject( QStringLiteral( "/kglobalaccel" ), accelObject, QDBusConnection::ExportAdaptors ) ) {
    return false;
  }

  QObject * ours = new QObject( accelObject );
  new ComponentAdaptor( ours,
                        accel.componentPaths.at( 0 ),
                        QStringList() << QStringLiteral( "clipboard-translate" ),
                        &accel );

  if ( !bus.registerObject( accel.componentPaths.at( 0 ), ours, QDBusConnection::ExportAdaptors ) ) {
    return false;
  }

  QObject * theirs = new QObject( accelObject );
  new ComponentAdaptor( theirs,
                        accel.componentPaths.at( 1 ),
                        QStringList() << QStringLiteral( "show-barcode" ),
                        &accel );

  return bus.registerObject( accel.componentPaths.at( 1 ), theirs, QDBusConnection::ExportAdaptors )
    && bus.registerService( QStringLiteral( "org.kde.kglobalaccel" ) );
}

class GlobalShortcutsAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts" )
  Q_PROPERTY( quint32 version READ version WRITE setVersion )

public:
  explicit GlobalShortcutsAdaptor( QObject * parent, PortalState * state ):
    QDBusAbstractAdaptor( parent ),
    state( state )
  {}

  quint32 version() const
  {
    return m_version;
  }

  void setVersion( quint32 version )
  {
    m_version = version;
  }

  void fireActivated( const QString & sessionPath, const QString & id )
  {
    emit Activated( QDBusObjectPath( sessionPath ), id, quint64( 1234 ), QVariantMap() );
  }

public slots:
  QDBusObjectPath CreateSession( const QVariantMap & options )
  {
    ++state->sessionsCreated;

    const QString token = options.value( QStringLiteral( "handle_token" ) ).toString();

    state->lastSessionPath = QStringLiteral( "%1/session/%2" ).arg( kDesktopPath,
        options.value( QStringLiteral( "session_handle_token" ), QStringLiteral( "session0" ) ).toString() );

    QObject * sessionObject = new QObject();
    sessionObject->setParent( parent() );
    new SessionAdaptor( sessionObject, state, state->lastSessionPath );

    QDBusConnection::sessionBus().registerObject( state->lastSessionPath, sessionObject,
        QDBusConnection::ExportAdaptors );

    RequestAdaptor * request = newRequest( token );
    const QString sessionPath = state->lastSessionPath;

    QTimer::singleShot( 0, request, [ request, sessionPath ] {
      request->respond( 0, { { QStringLiteral( "session_handle" ), sessionPath } } );
    } );

    return QDBusObjectPath( requestPath( token ) );
  }

  QDBusObjectPath BindShortcuts( const QDBusObjectPath & sessionHandle,
                                 const QList< MockShortcut > & shortcuts,
                                 const QString & parentWindow,
                                 const QVariantMap & options )
  {
    Q_UNUSED( parentWindow );

    ++state->bindCalls;
    state->boundSession = sessionHandle.path();
    state->boundIds.clear();
    state->preferredTriggers.clear();
    state->descriptions.clear();

    for ( const MockShortcut & e : shortcuts ) {
      state->boundIds << e.id;
      state->preferredTriggers.insert( e.id, e.properties.value( QStringLiteral( "preferred_trigger" ) ).toString() );
      state->descriptions.insert( e.id, e.properties.value( QStringLiteral( "description" ) ).toString() );
    }

    const QString token = options.value( QStringLiteral( "handle_token" ) ).toString();
    RequestAdaptor * request = newRequest( token );

    const uint response = state->bindResponse;
    const QList< MockShortcut > reported = reportBack();

    QTimer::singleShot( 0, request, [ request, response, reported ] {
      request->respond( response, { { QStringLiteral( "shortcuts" ), QVariant::fromValue( reported ) } } );
    } );

    return QDBusObjectPath( requestPath( token ) );
  }

signals:
  void Activated( const QDBusObjectPath & sessionHandle, const QString & shortcutId,
                  quint64 timestamp, const QVariantMap & options );

private:
  QString requestPath( const QString & token ) const
  {
    return static_cast< DesktopObject * >( parent() )->requestPath( token );
  }

  RequestAdaptor * newRequest( const QString & token )
  {
    return static_cast< DesktopObject * >( parent() )->newRequest( token );
  }

  QList< MockShortcut > reportBack() const
  {
    QList< MockShortcut > list;

    for ( const QString & id : state->boundIds ) {
      QVariantMap properties;

      properties[ QStringLiteral( "description" ) ] = state->descriptions.value( id );
      properties[ QStringLiteral( "trigger_description" ) ]
          = QStringLiteral( "Ctrl+%1" ).arg( state->preferredTriggers.value( id ).section( QLatin1Char( '+' ), -1 ) );

      list.append( MockShortcut{ id, properties } );
    }

    return list;
  }

  PortalState * state;
  quint32 m_version = 2;
};

class FileChooserAdaptor: public QDBusAbstractAdaptor
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.freedesktop.portal.FileChooser" )
  Q_PROPERTY( quint32 version READ version WRITE setVersion )

public:
  FileChooserAdaptor( QObject * parent, ChooserState * state ):
    QDBusAbstractAdaptor( parent ),
    state( state )
  {}

  quint32 version() const
  {
    return m_version;
  }

  void setVersion( quint32 version )
  {
    m_version = version;
  }

  void answerNextWith( const QStringList & uris, const QString & filterName = QString() )
  {
    state->answer = uris;
    state->answerFilter = filterName;
    state->response = 0;
  }

  void cancelNext()
  {
    state->response = 1;
  }

public slots:
  QDBusObjectPath OpenFile( const QString & parentWindow, const QString & title, const QVariantMap & options )
  {
    ++state->openCalls;
    return ask( parentWindow, title, options, false );
  }

  QDBusObjectPath SaveFile( const QString & parentWindow, const QString & title, const QVariantMap & options )
  {
    ++state->saveCalls;
    return ask( parentWindow, title, options, false );
  }

  QDBusObjectPath OpenDirectory( const QString & parentWindow, const QString & title, const QVariantMap & options )
  {
    ++state->directoryCalls;
    return ask( parentWindow, title, options, true );
  }

private:
  QDBusObjectPath ask( const QString & parentWindow, const QString & title, const QVariantMap & options,
                       bool forcedDirectory )
  {
    state->lastTitle = title;
    state->lastParentWindow = parentWindow;
    state->lastDirectory = options.value( QStringLiteral( "directory" ) ).toBool() || forcedDirectory;
    state->lastMultiple = options.value( QStringLiteral( "multiple" ) ).toBool();

    state->lastCurrentFolder
        = QString::fromLocal8Bit( options.value( QStringLiteral( "current_folder" ) ).toByteArray().constData() );

    state->lastOptionKeys = options.keys();
    state->lastFiltersGiven = options.contains( QStringLiteral( "filters" ) );
    state->lastFilterType = options.value( QStringLiteral( "filters" ) ).metaType().name();

    const QString token = options.value( QStringLiteral( "handle_token" ) ).toString();
    RequestAdaptor * request = static_cast< DesktopObject * >( parent() )->newRequest( token );

    const uint response = state->response;
    const QStringList uris = response == 0 ? state->answer : QStringList();
    const QString filterName = response == 0 ? state->answerFilter : QString();

    QTimer::singleShot( 200, request, [ request, response, uris, filterName ] {
      QVariantMap results;

      if ( response == 0 ) {
        results.insert( QStringLiteral( "uris" ), uris );

        if ( !filterName.isEmpty() ) {
          MockFilter chosen;
          chosen.name = filterName;
          chosen.conditions = { { 1, QStringLiteral( "*" ) } };
          results.insert( QStringLiteral( "current_filter" ), QVariant::fromValue( chosen ) );
        }
      }

      request->respond( response, results );
    } );

    return QDBusObjectPath(
      static_cast< DesktopObject * >( parent() )->requestPath( token ) );
  }

  ChooserState * state;
  quint32 m_version = 3;
};

void registerWireTypes();

QVariant probeProperty( const QString & iface )
{
  const QDBusMessage reply = QDBusConnection::sessionBus().call(
    QDBusMessage::createMethodCall( kDesktop, kDesktopPath, kProperties, QStringLiteral( "Get" ) )
      << iface << QStringLiteral( "version" ),
    QDBus::Block,
    3000 );

  if ( reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty() ) {
    return {};
  }

  return reply.arguments().at( 0 ).value< QDBusVariant >().variant();
}

QVariant probeSetting( const QString & space, const QString & key )
{
  const QDBusMessage reply = QDBusConnection::sessionBus().call(
    QDBusMessage::createMethodCall( kDesktop, kDesktopPath, kSettings, QStringLiteral( "Read" ) ) << space << key,
    QDBus::Block,
    3000 );

  if ( reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty() ) {
    return {};
  }

  QVariant v = reply.arguments().at( 0 ).value< QDBusVariant >().variant();

  if ( v.metaType() == QMetaType::fromType< QDBusVariant >() ) {
    v = v.value< QDBusVariant >().variant();
  }

  if ( v.metaType() == QMetaType::fromType< QDBusArgument >()
       && v.value< QDBusArgument >().currentType() == QDBusArgument::StructureType ) {
    return QVariant::fromValue( qdbus_cast< MockAccent >( v.value< QDBusArgument >() ) );
  }

  return v;
}

int probeOnly()
{
  registerWireTypes();

  PortalShortcuts shortcuts;

  say( QStringLiteral( "  portal usable: %1" ).arg( shortcuts.initialize() ? QStringLiteral( "yes" )
                                                                          : QStringLiteral( "no" ) ) );

  say( QStringLiteral( "  file chooser: %1" )
         .arg( PortalFileChooser::portalOffersFileChooser() ? QStringLiteral( "the portal's" ) :
                                                              QStringLiteral( "Qt's own" ) ) );

  const QVariant settingsVersion = probeProperty( kSettings );

  say( QStringLiteral( "  Settings version: %1" )
         .arg( settingsVersion.isValid() ? settingsVersion.toString() : QStringLiteral( "no Settings" ) ) );

  const QVariant scheme = probeSetting( kAppearance, kColorScheme );

  say( QStringLiteral( "  color-scheme: %1" )
         .arg( scheme.isValid() ? scheme.toString() : QStringLiteral( "not published" ) ) );

  const QVariant accent = probeSetting( kAppearance, kAccentColor );

  if ( accent.metaType() == QMetaType::fromType< MockAccent >() ) {
    const MockAccent c = qvariant_cast< MockAccent >( accent );

    say( QStringLiteral( "  accent-color: %1" ).arg( QColor::fromRgbF( c.r, c.g, c.b ).name() ) );
  }
  else {
    say( QStringLiteral( "  accent-color: %1" )
           .arg( accent.isValid() ? accent.toString() : QStringLiteral( "not published" ) ) );
  }

  const QString globalsPath =
    QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation ) + QStringLiteral( "/kdeglobals" );

  say(
    QStringLiteral( "  kdeglobals: %1" ).arg( QFile::exists( globalsPath ) ? globalsPath : QStringLiteral( "none" ) ) );

  return 0;
}

int noPortal()
{
  PortalShortcuts shortcuts;

  const bool usable = shortcuts.initialize();

  say( QStringLiteral( "  portal usable: %1" ).arg( usable ? QStringLiteral( "yes" ) : QStringLiteral( "no" ) ) );
  CHECK( !usable );

  shortcuts.add( QStringLiteral( "main-window" ), QKeySequence( QStringLiteral( "Ctrl+Alt+F11" ) ),
    QStringLiteral( "Show or hide the main window" ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 300 );

  CHECK( !SystemAppearance::instance().isDark() );
  CHECK( SystemAppearance::instance().scheme() == Qt::ColorScheme::Unknown );
  CHECK( !SystemAppearance::instance().publishesColors() );

  CHECK( !PortalFileChooser::portalOffersFileChooser() );
  CHECK( !PortalFileChooser::preferPortalFileChooser() );
  CHECK( qgetenv( "QT_QPA_PLATFORMTHEME" ).isEmpty() );

  const QString configDir = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );
  const QString globalsPath = configDir + QStringLiteral( "/kdeglobals" );

  if ( QFile::exists( globalsPath ) )
    QFile::remove( globalsPath );

  {
    QFile file( globalsPath );

    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
      say( QStringLiteral( "  could not write %1 - the file fallback went unchecked" ).arg( globalsPath ) );
      return failures == 0 ? 0 : 1;
    }

    file.write( "[General]\nColorScheme=BreezeDark\n\n"
                "[Colors:Window]\nBackgroundNormal=26,26,26\nForegroundNormal=236,236,236\n\n"
                "[Colors:View]\nBackgroundNormal=34,34,34\nForegroundNormal=232,232,232\n\n"
                "[Colors:Selection]\nBackgroundNormal=255,132,60\nForegroundNormal=20,20,20\n\n"
                "[Colors:Button]\nBackgroundNormal=30,30,30\nForegroundNormal=236,236,236\n" );
    file.close();
  }

  CHECK( waitFor( [] { return SystemAppearance::instance().scheme() == Qt::ColorScheme::Dark; } ) );
  CHECK( SystemAppearance::instance().isDark() );
  CHECK( SystemAppearance::instance().publishesColors() );

  QPalette fromFile;
  CHECK( SystemAppearance::instance().desktopPalette( fromFile ) );
  CHECK( fromFile.color( QPalette::Window ) == QColor( 26, 26, 26 ) );
  CHECK( fromFile.color( QPalette::Base ) == QColor( 34, 34, 34 ) );
  CHECK( fromFile.color( QPalette::Text ) == QColor( 232, 232, 232 ) );
  CHECK( fromFile.color( QPalette::Highlight ) == QPalette().color( QPalette::Highlight ) );
  CHECK( fromFile.color( QPalette::Highlight ) != QColor( 255, 132, 60 ) );
  CHECK( fromFile.color( QPalette::Link ) == fromFile.color( QPalette::Highlight ) );

  QFile::remove( globalsPath );

  say( QStringLiteral( "  kdeglobals read: %1" ).arg( SystemAppearance::instance().describe() ) );

  return failures == 0 ? 0 : 1;
}

int serve( GlobalShortcutsAdaptor * adaptor, PortalState & state, const QString & fireId )
{
  say( QStringLiteral( "  portal exported; waiting up to 20 s for a client to bind shortcuts" ) );

  if ( !waitFor( [ &state ] { return state.bindCalls > 0; }, 20000 ) ) {
    say( QStringLiteral( "  no client bound any shortcut" ) );
    return 1;
  }

  for ( const QString & id : state.boundIds )
    say( QStringLiteral( "  bound %1: trigger %2, described as %3" )
        .arg( id, state.preferredTriggers.value( id ), state.descriptions.value( id ) ) );

  const QString which = fireId.isEmpty() && !state.boundIds.isEmpty() ? state.boundIds.first() : fireId;

  for ( int i = 0; i < 2; ++i )
    QTimer::singleShot( 3000 + i * 10000, [ adaptor, &state, which ] {
      say( QStringLiteral( "  firing Activated for %1" ).arg( which ) );
      adaptor->fireActivated( state.boundSession, which );
    } );

  QTimer::singleShot( 20000, [] { QCoreApplication::exit( 0 ); } );

  QCoreApplication::exec();

  say( QStringLiteral( "  sessions created %1, BindShortcuts calls %2, sessions closed %3" )
      .arg( state.sessionsCreated )
      .arg( state.bindCalls )
      .arg( state.sessionsClosed ) );

  return 0;
}

int serveTheme( SettingsAdaptor * settingsAdaptor, int seconds )
{
  const int step = qMax( seconds, 5 ) * 1000;

  QTemporaryDir configDir;

  if ( !configDir.isValid() ) {
    say( QStringLiteral( "  could not make a temporary config directory" ) );
    return 1;
  }

  settingsAdaptor->writeTo( configDir.path() );
  settingsAdaptor->makeDark( true );

  say(
    QStringLiteral( "  portal exported; the desktop starts dark, accent #ff843c from"
                    " accent-color; its portal colour groups are decoys" ) );
  say( QStringLiteral( "  colours: %1/kdeglobals -- run the application with"
                       " XDG_CONFIG_HOME=%1" )
         .arg( configDir.path() ) );

  QTimer::singleShot( step, [ settingsAdaptor ] {
    say( QStringLiteral( "  desktop -> light (BreezeLight, color-scheme 2)" ) );
    settingsAdaptor->makeDark( false );
  } );

  QTimer::singleShot( step * 2, [ settingsAdaptor ] {
    say( QStringLiteral( "  desktop -> dark again (BreezeDark, color-scheme 1)" ) );
    settingsAdaptor->makeDark( true );
  } );

  QTimer::singleShot( step * 3, [] { QCoreApplication::exit( 0 ); } );

  QCoreApplication::exec();

  say( QStringLiteral( "  Read calls answered: %1" ).arg( settingsAdaptor->readCalls() ) );

  return 0;
}

void registerWireTypes()
{
  qDBusRegisterMetaType< MockShortcut >();
  qDBusRegisterMetaType< QList< MockShortcut > >();
  qDBusRegisterMetaType< MockFilterCondition >();
  qDBusRegisterMetaType< QList< MockFilterCondition > >();
  qDBusRegisterMetaType< MockFilter >();
  qDBusRegisterMetaType< QList< MockFilter > >();
  qDBusRegisterMetaType< MockAccent >();
}

int serveChooser( ChooserState & chooser, int seconds )
{
  say( QStringLiteral( "  portal exported; waiting up to %1 s for a client to open a chooser" ).arg( seconds ) );

  if ( !waitFor( [ &chooser ] { return chooser.openCalls + chooser.saveCalls + chooser.directoryCalls > 0; },
         1000 * seconds ) ) {
    say( QStringLiteral( "  no client asked for a chooser" ) );
    return 1;
  }
  if ( !waitFor( [ &chooser ] { return chooser.openCalls + chooser.saveCalls + chooser.directoryCalls > 0; }, 40000 ) ) {
    say( QStringLiteral( "  no client asked for a chooser" ) );
    return 1;
  }

  say( QStringLiteral( "  title: %1" ).arg( chooser.lastTitle ) );
  say( QStringLiteral( "  parent window: %1" ).arg( chooser.lastParentWindow ) );
  say( QStringLiteral( "  starting folder: %1" ).arg( chooser.lastCurrentFolder ) );
  say( QStringLiteral( "  filters offered: %1, arriving as %2" )
      .arg( chooser.lastFiltersGiven ? QStringLiteral( "yes" ) : QStringLiteral( "no" ) )
      .arg( chooser.lastFilterType ) );
  say( QStringLiteral( "  answering with: %1, filter left selected: %2" )
      .arg( chooser.answer.join( ", " ), chooser.answerFilter ) );

  QTimer::singleShot( 8000, [] { QCoreApplication::exit( 0 ); } );
  QCoreApplication::exec();

  say( QStringLiteral( "  OpenFile %1, SaveFile %2, OpenDirectory %3" )
      .arg( chooser.openCalls )
      .arg( chooser.saveCalls )
      .arg( chooser.directoryCalls ) );

  return 0;
}

int filesMode( int argc, char ** argv )
{
  registerWireTypes();

  qputenv( "QT_QPA_PLATFORM", "offscreen" );

  QDBusConnection bus = QDBusConnection::sessionBus();
  DesktopObject * desktop = new DesktopObject();
  ChooserState chooser;

  auto * chooserAdaptor = new FileChooserAdaptor( desktop, &chooser );
  new SettingsAdaptor( desktop );

  if ( !bus.registerObject( kDesktopPath, desktop, QDBusConnection::ExportAdaptors )
      || !bus.registerService( kDesktop ) ) {
    say( QStringLiteral( "portalmock --files: could not take over %1" ).arg( kDesktop ) );
    return 2;
  }

  say( QStringLiteral( "the application's own decision:" ) );
  CHECK( PortalFileChooser::preferPortalFileChooser() );
  CHECK( qgetenv( "QT_QPA_PLATFORMTHEME" ) == QByteArray( "xdgdesktopportal" ) );

  QApplication app( argc, argv );

  {
    QEventLoop settle;
    QTimer::singleShot( 500, &settle, &QEventLoop::quit );
    settle.exec();
  }

  auto run = [ & ]( QFileDialog & d ) {
    int result = -1;

    QObject::connect( &d, &QFileDialog::finished, [ & ]( int r ) { result = r; } );
    d.open();

    if ( !waitFor( [ & ] { return result >= 0; }, 4000 ) )
      say( QStringLiteral( "  the portal was never asked, or never answered" ) );

    return result;
  };

  say( QStringLiteral( "a save dialog through the portal:" ) );

  QFileDialog save;
  save.setAcceptMode( QFileDialog::AcceptSave );
  save.setWindowTitle( QStringLiteral( "Export history to file" ) );
  save.setDirectory( QStringLiteral( "/tmp" ) );
  save.setNameFilters( { QStringLiteral( "Text files (*.txt)" ), QStringLiteral( "All files (*.*)" ) } );

  chooserAdaptor->answerNextWith( { QStringLiteral( "file:///tmp/portalmock-picked.txt" ) } );

  CHECK( run( save ) == QDialog::Accepted );
  CHECK( chooser.saveCalls == 1 );
  CHECK( save.selectedFiles() == QStringList( { QStringLiteral( "/tmp/portalmock-picked.txt" ) } ) );
  CHECK( chooser.lastTitle == QStringLiteral( "Export history to file" ) );
  CHECK( chooser.lastCurrentFolder == QStringLiteral( "/tmp" ) );
  say( QStringLiteral( "  the portal saw options: %1" ).arg( chooser.lastOptionKeys.join( ", " ) ) );
  CHECK( chooser.lastFiltersGiven );
  CHECK( !chooser.lastDirectory );

  say( QStringLiteral( "a directory dialog through the portal:" ) );

  chooserAdaptor->answerNextWith( { QStringLiteral( "file:///tmp/portalmock-folder" ) } );

  QFileDialog dir;
  dir.setFileMode( QFileDialog::Directory );
  dir.setOption( QFileDialog::ShowDirsOnly );
  dir.setWindowTitle( QStringLiteral( "Choose a directory" ) );
  dir.setDirectory( QStringLiteral( "/tmp" ) );

  CHECK( run( dir ) == QDialog::Accepted );
  CHECK( chooser.openCalls == 1 );
  CHECK( chooser.lastDirectory );
  CHECK( dir.selectedFiles() == QStringList( { QStringLiteral( "/tmp/portalmock-folder" ) } ) );

  say( QStringLiteral( "a chooser the user closes:" ) );

  chooserAdaptor->cancelNext();

  QFileDialog cancelled;
  cancelled.setAcceptMode( QFileDialog::AcceptSave );
  cancelled.setWindowTitle( QStringLiteral( "Export history to file" ) );
  cancelled.setDirectory( QStringLiteral( "/tmp" ) );
  cancelled.setNameFilters( { QStringLiteral( "Text files (*.txt)" ) } );

  CHECK( run( cancelled ) == QDialog::Rejected );
  CHECK( chooser.saveCalls == 2 );
  CHECK( cancelled.selectedFiles().isEmpty() );

  say( QString::fromUtf8( failures == 0 ? "PORTAL FILES OK" : "PORTAL FILES FAILED" ) );

  return failures == 0 ? 0 : 1;
}

int main( int argc, char ** argv )
{
  const char * mode = argc > 1 ? argv[ 1 ] : "";

  if ( qstrcmp( mode, "--files" ) == 0 )
    return filesMode( argc, argv );

  QCoreApplication app( argc, argv );

  if ( qstrcmp( mode, "--probe" ) == 0 )
    return probeOnly();

  registerWireTypes();

  PortalState state;
  ChooserState chooser;
  AccelState accel;
  GlobalShortcutsAdaptor * adaptor = nullptr;
  SettingsAdaptor * settingsAdaptor = nullptr;
  FileChooserAdaptor * chooserAdaptor = nullptr;

  if ( qstrcmp( mode, "--no-portal" ) != 0 ) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    DesktopObject * desktop = new DesktopObject();

    adaptor = new GlobalShortcutsAdaptor( desktop, &state );
    settingsAdaptor = new SettingsAdaptor( desktop );
    chooserAdaptor = new FileChooserAdaptor( desktop, &chooser );

    if ( !bus.registerObject( kDesktopPath, desktop, QDBusConnection::ExportAdaptors )
        || !bus.registerService( kDesktop ) ) {
      say( QStringLiteral( "portalmock: could not take over %1" ).arg( kDesktop ) );
      return 2;
    }

    if ( !serveKdeAccel( accel ) ) {
      say( QStringLiteral( "portalmock: could not take over org.kde.kglobalaccel" ) );
      return 2;
    }

    const QDBusMessage probe = bus.call( QDBusMessage::createMethodCall( kDesktop, kDesktopPath, kProperties,
        QStringLiteral( "Get" ) )
        << kGlobalShortcuts << QStringLiteral( "version" ), QDBus::Block, 3000 );

    const QDBusMessage chooserProbe = bus.call( QDBusMessage::createMethodCall( kDesktop, kDesktopPath, kProperties,
        QStringLiteral( "Get" ) )
        << QStringLiteral( "org.freedesktop.portal.FileChooser" ) << QStringLiteral( "version" ), QDBus::Block, 3000 );
    say( QStringLiteral( "  Properties.Get version -> %1" ).arg( probe.type() == QDBusMessage::ErrorMessage
        ? probe.errorName() + QStringLiteral( ": " ) + probe.errorMessage()
        : probe.arguments().isEmpty() ? QStringLiteral( "no arguments" )
                                      : probe.arguments().at( 0 ).value< QDBusVariant >().variant().toString() ) );

    say( QStringLiteral( "  Properties.Get FileChooser version -> %1" ).arg( chooserProbe.type()
        == QDBusMessage::ErrorMessage
            ? chooserProbe.errorName() + QStringLiteral( ": " ) + chooserProbe.errorMessage()
            : chooserProbe.arguments().isEmpty() ? QStringLiteral( "no arguments" )
                                                 : chooserProbe.arguments().at( 0 ).value< QDBusVariant >().variant().toString() ) );
  }
  else
    return noPortal();

  if ( qstrcmp( mode, "--serve" ) == 0 )
    return serve( adaptor, state, argc > 2 ? QString::fromLocal8Bit( argv[ 2 ] ) : QString() );

  if ( qstrcmp( mode, "--serve-theme" ) == 0 )
    return serveTheme( settingsAdaptor, argc > 2 ? QString::fromLocal8Bit( argv[ 2 ] ).toInt() : 12 );

  if ( qstrcmp( mode, "--serve-chooser" ) == 0 )
    return serveChooser( chooser, argc > 2 ? QString::fromLocal8Bit( argv[ 2 ] ).toInt() : 40 );

  say( QStringLiteral( "trigger strings:" ) );

  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "Ctrl+Alt+F11" ) ) )
      == QStringLiteral( "CTRL+ALT+F11" ) );
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "Ctrl+F11, Ctrl+F11" ) ) )
         == QStringLiteral( "CTRL+F11" ) );
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "Meta+Shift+A" ) ) )
      == QStringLiteral( "SHIFT+LOGO+a" ) );
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "Ctrl+Space" ) ) )
      == QStringLiteral( "CTRL+space" ) );
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "Insert" ) ) )
      == QStringLiteral( "Insert" ) );
  // the shipped default: a bare function key, no modifier at all
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence( QStringLiteral( "F12" ) ) )
      == QStringLiteral( "F12" ) );
  CHECK( PortalShortcuts::toPortalTrigger( QKeySequence() ).isEmpty() );

  say( QStringLiteral( "binding two hotkeys:" ) );

  PortalShortcuts shortcuts;

  CHECK( shortcuts.initialize() );

  QStringList activatedIds;

  QObject::connect( &shortcuts, &PortalShortcuts::activated,
  [ &activatedIds ]( const QString & id ) { activatedIds << id; } );

  shortcuts.add( QStringLiteral( "main-window" ), QKeySequence( QStringLiteral( "Ctrl+Alt+F11" ) ),
    QStringLiteral( "Show or hide the main window" ) );
  shortcuts.add( QStringLiteral( "clipboard-translate" ),
                 QKeySequence( QStringLiteral( "Ctrl+F11, Ctrl+F11" ) ),
                 QStringLiteral( "Show the main window and look up the clipboard contents" ) );

  CHECK( waitFor( [ &state ] { return state.bindCalls == 1; } ) );
  CHECK( state.sessionsCreated == 1 );
  CHECK( state.boundIds
      == QStringList( { QStringLiteral( "main-window" ), QStringLiteral( "clipboard-translate" ) } ) );
  CHECK( state.preferredTriggers.value( QStringLiteral( "main-window" ) ) == QStringLiteral( "CTRL+ALT+F11" ) );
  CHECK( state.preferredTriggers.value( QStringLiteral( "clipboard-translate" ) ) == QStringLiteral( "CTRL+F11" ) );
  CHECK( state.descriptions.value( QStringLiteral( "main-window" ) )
      == QStringLiteral( "Show or hide the main window" ) );

  say( QStringLiteral( "what the desktop reports back:" ) );

  CHECK( waitFor( [ &shortcuts ] { return !shortcuts.assignedTrigger( QStringLiteral( "main-window" ) ).isEmpty(); } ) );
  CHECK( shortcuts.assignedTrigger( QStringLiteral( "main-window" ) ) == QStringLiteral( "Ctrl+F11" ) );

  say( QStringLiteral( "Activated:" ) );

  adaptor->fireActivated( state.boundSession, QStringLiteral( "main-window" ) );
  CHECK( waitFor( [ &activatedIds ] { return activatedIds.size() == 1; } ) );
  CHECK( activatedIds == QStringList( { QStringLiteral( "main-window" ) } ) );

  adaptor->fireActivated( QStringLiteral( "/org/freedesktop/portal/desktop/session/99" ),
    QStringLiteral( "main-window" ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );
  CHECK( activatedIds.size() == 1 );

  say( QStringLiteral( "re-binding takes a new session and closes the old one:" ) );

  const QString firstSession = state.boundSession;

  shortcuts.add( QStringLiteral( "main-window" ), QKeySequence( QStringLiteral( "Ctrl+F12" ) ),
    QStringLiteral( "Show or hide the main window" ) );

  CHECK( waitFor( [ &state ] { return state.bindCalls == 2 && state.sessionsClosed == 1; } ) );
  CHECK( state.closedSessions == QStringList( { firstSession } ) );
  CHECK( state.sessionsCreated == 2 );
  CHECK( state.boundSession != firstSession );
  CHECK( state.boundIds
      == QStringList( { QStringLiteral( "main-window" ), QStringLiteral( "clipboard-translate" ) } ) );

  const QString secondSession = state.boundSession;

  adaptor->fireActivated( secondSession, QStringLiteral( "clipboard-translate" ) );
  CHECK( waitFor( [ &activatedIds ] { return activatedIds.size() == 2; } ) );
  CHECK( !activatedIds.isEmpty() && activatedIds.last() == QStringLiteral( "clipboard-translate" ) );

  say( QStringLiteral( "clearing closes the session and stops the hotkeys:" ) );

  shortcuts.clear();

  CHECK( state.sessionsClosed == 2 );
  CHECK( !state.closedSessions.isEmpty() && state.closedSessions.last() == secondSession );

  adaptor->fireActivated( secondSession, QStringLiteral( "main-window" ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );
  CHECK( activatedIds.size() == 2 );

  say( QStringLiteral( "releasing the desktop's grabs for our shortcut id:" ) );

  PortalShortcuts::releaseDesktopShortcuts( QStringList() << QStringLiteral( "clipboard-translate" ) );

  CHECK( accel.cleanUpCalls == 1 );
  CHECK( accel.cleanedPaths == QStringList( { QStringLiteral( "/component/io_github_dsvi_microdict" ) } ) );

  PortalShortcuts::releaseDesktopShortcuts( QStringList() << QStringLiteral( "no-such-shortcut" ) );
  CHECK( accel.cleanUpCalls == 1 );

  say( QStringLiteral( "a desktop that refuses the shortcuts:" ) );

  state.bindResponse = 1;
  shortcuts.add( QStringLiteral( "main-window" ), QKeySequence( QStringLiteral( "Ctrl+F12" ) ),
    QStringLiteral( "Show or hide the main window" ) );

  CHECK( waitFor( [ &state ] { return state.bindCalls == 3; } ) );
  CHECK( shortcuts.assignedTrigger( QStringLiteral( "main-window" ) ).isEmpty() );

  say( QStringLiteral( "a portal too old for this client:" ) );

  adaptor->setVersion( 1 );

  PortalShortcuts oldStyle;

  CHECK( !oldStyle.initialize() );

  say( QStringLiteral( "the desktop's colour scheme and colours:" ) );

  settingsAdaptor->makeDark( true );
  CHECK( SystemAppearance::instance().isDark() );
  CHECK( SystemAppearance::instance().scheme() == Qt::ColorScheme::Dark );

  QPalette darkPalette;
  CHECK( SystemAppearance::instance().desktopPalette( darkPalette ) );
  CHECK( darkPalette.color( QPalette::Window ) == QColor( 23, 23, 23 ) );
  CHECK( darkPalette.color( QPalette::Window ) != QColor( 9, 9, 9 ) );
  CHECK( darkPalette.color( QPalette::Base ) == QColor( 42, 42, 42 ) );
  CHECK( darkPalette.color( QPalette::Text ) == QColor( 239, 239, 239 ) );
  CHECK( darkPalette.color( QPalette::Highlight ) == QColor( 255, 132, 60 ) );
  CHECK( darkPalette.color( QPalette::HighlightedText ) == QColor( 0, 0, 0 ) );
  CHECK( darkPalette.color( QPalette::Link ) == QColor( 255, 132, 60 ) );
  CHECK( darkPalette.color( QPalette::Disabled, QPalette::Text ) == QColor( 125, 125, 125 ) );
  CHECK( darkPalette.color( QPalette::Button ) == QColor( 35, 35, 35 ) );
  CHECK( darkPalette.color( QPalette::AlternateBase ) == QColor( 48, 48, 48 ) );
  CHECK( darkPalette.color( QPalette::Inactive, QPalette::Highlight ) == QColor( 23, 23, 23 ) );
  CHECK( SystemAppearance::instance().publishesColors() );

  int appearanceChanges = 0;

  QObject::connect( &SystemAppearance::instance(), &SystemAppearance::changed,
  [ &appearanceChanges ] { ++appearanceChanges; } );

  settingsAdaptor->makeDark( false );

  CHECK( waitFor( [ &appearanceChanges ] { return appearanceChanges >= 1; } ) );
  CHECK( !SystemAppearance::instance().isDark() );

  QPalette lightPalette;
  CHECK( SystemAppearance::instance().desktopPalette( lightPalette ) );
  CHECK( lightPalette.color( QPalette::Window ) == QColor( 239, 240, 241 ) );
  CHECK( lightPalette.color( QPalette::Window ) != QColor( 250, 250, 250 ) );
  CHECK( lightPalette.color( QPalette::Base ) == QColor( 252, 252, 252 ) );
  CHECK( lightPalette.color( QPalette::Text ) == QColor( 23, 38, 41 ) );
  CHECK( lightPalette.color( QPalette::Highlight ) == QColor( 255, 132, 60 ) );
  CHECK( lightPalette.color( QPalette::Link ) == QColor( 192, 93, 0 ) );

  settingsAdaptor->emitChanged( kAppearance, QStringLiteral( "contrast" ), uint( 1 ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 300 );
  CHECK( !SystemAppearance::instance().isDark() );
  CHECK( SystemAppearance::instance().scheme() == Qt::ColorScheme::Light );
  CHECK( appearanceChanges == 1 );

  say( QStringLiteral( "the file chooser:" ) );

  CHECK( PortalFileChooser::portalOffersFileChooser() );

  qunsetenv( "QT_QPA_PLATFORMTHEME" );
  CHECK( PortalFileChooser::preferPortalFileChooser() );
  CHECK( qgetenv( "QT_QPA_PLATFORMTHEME" ) == QByteArray( "xdgdesktopportal" ) );

  qputenv( "QT_QPA_PLATFORMTHEME", "gnome" );
  CHECK( !PortalFileChooser::preferPortalFileChooser() );
  CHECK( qgetenv( "QT_QPA_PLATFORMTHEME" ) == QByteArray( "gnome" ) );
  qunsetenv( "QT_QPA_PLATFORMTHEME" );

  chooserAdaptor->setVersion( 2 );
  CHECK( !PortalFileChooser::portalOffersFileChooser() );
  CHECK( !PortalFileChooser::preferPortalFileChooser() );
  CHECK( qgetenv( "QT_QPA_PLATFORMTHEME" ).isEmpty() );
  chooserAdaptor->setVersion( 3 );
  CHECK( PortalFileChooser::portalOffersFileChooser() );

  say( QStringLiteral( "the standard portal accent:" ) );

  settingsAdaptor->makeAccentOnly();

  QPalette accentPalette;

  CHECK( waitFor( [ &accentPalette ] {
    return SystemAppearance::instance().desktopPalette( accentPalette )
        && accentPalette.color( QPalette::Highlight ) == QColor( 0, 255, 0 );
  } ) );
  CHECK( accentPalette.color( QPalette::Link ) == QColor( 0, 255, 0 ) );
  CHECK( accentPalette.color( QPalette::HighlightedText ) == QColor( 0, 0, 0 ) );
  CHECK( accentPalette.color( QPalette::Window ) == QColor( 239, 240, 241 ) );
  CHECK( SystemAppearance::instance().publishesColors() );

  say( QStringLiteral( "a desktop that publishes no accent:" ) );

  settingsAdaptor->makeNoAccent();

  QPalette barePalette;

  CHECK( waitFor( [ &barePalette ] {
    return SystemAppearance::instance().desktopPalette( barePalette )
      && barePalette.color( QPalette::Highlight ) == QPalette().color( QPalette::Highlight );
  } ) );
  CHECK( barePalette.color( QPalette::Highlight ) != QColor( 0, 255, 0 ) );
  CHECK( barePalette.color( QPalette::Window ) == QColor( 239, 240, 241 ) );

  QFile::remove( settingsAdaptor->kdeGlobalsPath() );

  say( QString::fromUtf8( failures == 0 ? "PORTAL MOCK OK" : "PORTAL MOCK FAILED" ) );

  return failures == 0 ? 0 : 1;
}

#include "main.moc"
