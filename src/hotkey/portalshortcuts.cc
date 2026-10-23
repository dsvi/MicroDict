#include "portalshortcuts.hh"

#include <QChar>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QKeyCombination>
#include <QTimer>
#include <QVariantMap>

namespace {

const QString kService = QStringLiteral( "org.freedesktop.portal.Desktop" );
const QString kDBusService = QStringLiteral( "org.freedesktop.DBus" );
const QString kDBusPath = QStringLiteral( "/org/freedesktop/DBus" );
const QString kPath = QStringLiteral( "/org/freedesktop/portal/desktop" );
const QString kIface = QStringLiteral( "org.freedesktop.portal.GlobalShortcuts" );
const QString kRequestIface = QStringLiteral( "org.freedesktop.portal.Request" );
const QString kSessionIface = QStringLiteral( "org.freedesktop.portal.Session" );
const QString kPropertiesIface = QStringLiteral( "org.freedesktop.DBus.Properties" );
const QString kAccelService    = QStringLiteral( "org.kde.kglobalaccel" );
const QString kAccelPath       = QStringLiteral( "/kglobalaccel" );
const QString kAccelIface      = QStringLiteral( "org.kde.KGlobalAccel" );
const QString kComponentIface  = QStringLiteral( "org.kde.kglobalaccel.Component" );

constexpr int createSessionTimeoutMs = 10000;
constexpr int methodReplyTimeoutMs = 5000;
constexpr int closeTimeoutMs = 1000;

constexpr int bindReplyTimeoutMs = 10 * 60 * 1000;

struct KeySymName {
  int key;
  const char * name;
};

const KeySymName keySymNames[] = {
  { Qt::Key_Space, "space" },
  { Qt::Key_Return, "Return" },
  { Qt::Key_Enter, "Return" },
  { Qt::Key_Tab, "Tab" },
  { Qt::Key_Backtab, "ISO_Left_Tab" },
  { Qt::Key_Escape, "Escape" },
  { Qt::Key_Backspace, "BackSpace" },
  { Qt::Key_Insert, "Insert" },
  { Qt::Key_Delete, "Delete" },
  { Qt::Key_Home, "home" },
  { Qt::Key_End, "end" },
  { Qt::Key_PageUp, "Prior" },
  { Qt::Key_PageDown, "Next" },
  { Qt::Key_Up, "Up" },
  { Qt::Key_Down, "Down" },
  { Qt::Key_Left, "Left" },
  { Qt::Key_Right, "Right" },
  { Qt::Key_Print, "Print" },
  { Qt::Key_ScrollLock, "Scroll_Lock" },
  { Qt::Key_NumLock, "Num_Lock" },
  { Qt::Key_CapsLock, "Caps_Lock" },
  { Qt::Key_Menu, "Menu" },
  { Qt::Key_Shift, "Shift_L" },
  { Qt::Key_Control, "Control_L" },
  { Qt::Key_Alt, "Alt_L" },
  { Qt::Key_Meta, "Super_L" },
  { Qt::Key_MediaPlay, "AudioPlay" },
  { Qt::Key_MediaTogglePlayPause, "AudioPlay" },
  { Qt::Key_MediaStop, "AudioStop" },
  { Qt::Key_MediaPrevious, "AudioPrev" },
  { Qt::Key_MediaNext, "AudioNext" },
  { Qt::Key_VolumeUp, "AudioVolumeUp" },
  { Qt::Key_VolumeDown, "AudioVolumeDown" },
  { Qt::Key_VolumeMute, "AudioMute" },
  { Qt::Key_Comma, "comma" },
  { Qt::Key_Period, "period" },
  { Qt::Key_Slash, "slash" },
  { Qt::Key_Minus, "minus" },
  { Qt::Key_Equal, "equal" },
  { Qt::Key_BracketLeft, "bracketleft" },
  { Qt::Key_BracketRight, "bracketright" },
  { Qt::Key_Semicolon, "semicolon" },
  { Qt::Key_Apostrophe, "apostrophe" },
  { Qt::Key_Backslash, "backslash" },
  { Qt::Key_QuoteLeft, "grave" },
  { Qt::Key_BraceLeft, "braceleft" },
  { Qt::Key_BraceRight, "braceright" },
  { Qt::Key_ParenLeft, "parenleft" },
  { Qt::Key_ParenRight, "parenright" },
  { Qt::Key_Question, "question" },
  { Qt::Key_Exclam, "exclam" },
  { Qt::Key_At, "at" },
  { Qt::Key_NumberSign, "numbersign" },
  { Qt::Key_Dollar, "dollar" },
  { Qt::Key_Percent, "percent" },
  { Qt::Key_AsciiCircum, "asciicircum" },
  { Qt::Key_Ampersand, "ampersand" },
  { Qt::Key_Asterisk, "asterisk" },
  { Qt::Key_Underscore, "underscore" },
  { Qt::Key_Plus, "plus" },
  { Qt::Key_Bar, "bar" },
  { Qt::Key_Colon, "colon" },
  { Qt::Key_QuoteDbl, "quotedbl" },
  { Qt::Key_Less, "less" },
  { Qt::Key_Greater, "greater" },
  { Qt::Key_AsciiTilde, "asciitilde" },
};

QString keySymName( int key )
{
  if ( key >= Qt::Key_A && key <= Qt::Key_Z )
    return QString( QChar( key ).toLower() );

  if ( key >= Qt::Key_0 && key <= Qt::Key_9 )
    return QString( QChar( key ) );

  if ( key >= Qt::Key_F1 && key <= Qt::Key_F35 )
    return QStringLiteral( "F%1" ).arg( key - Qt::Key_F1 + 1 );

  for ( const KeySymName & e : keySymNames )
    if ( e.key == key )
      return QString::fromLatin1( e.name );

  return {};
}

struct PortalShortcutEntry {
  QString id;
  QVariantMap properties;
};

QDataStream & operator<<( QDataStream & stream, const PortalShortcutEntry & e )
{
  return stream << e.id << e.properties;
}

QDataStream & operator>>( QDataStream & stream, PortalShortcutEntry & e )
{
  return stream >> e.id >> e.properties;
}

QDBusArgument & operator<<( QDBusArgument & arg, const PortalShortcutEntry & e )
{
  arg.beginStructure();
  arg << e.id << e.properties;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, PortalShortcutEntry & e )
{
  arg.beginStructure();
  arg >> e.id >> e.properties;
  arg.endStructure();
  return arg;
}

void registerPortalTypes()
{
  [[maybe_unused]] static const bool registered = [] {
    qDBusRegisterMetaType< PortalShortcutEntry >();
    qDBusRegisterMetaType< QList< PortalShortcutEntry > >();
    return true;
  }();
}

QVariant variantValue( const QVariant & value )
{
  if ( value.metaType() == QMetaType::fromType< QDBusVariant >() )
    return value.value< QDBusVariant >().variant();

  return value;
}

}

PortalShortcuts::PortalShortcuts( QObject * parent ):
  QObject( parent )
{
  registerPortalTypes();
}

PortalShortcuts::~PortalShortcuts()
{
  closeSession();
}

bool PortalShortcuts::initialize()
{
  QDBusConnection bus = QDBusConnection::sessionBus();

  if ( !bus.isConnected() ) {
    qInfo() << "Global hotkeys: no session bus, so the desktop cannot be asked for a shortcut.";
    return false;
  }

  const QDBusMessage owner = bus.call( QDBusMessage::createMethodCall( kDBusService, kDBusPath, kDBusService,
                                         QStringLiteral( "NameHasOwner" ) )
      << kService,
      QDBus::Block, methodReplyTimeoutMs );

  if ( owner.type() == QDBusMessage::ErrorMessage || !owner.arguments().at( 0 ).toBool() ) {
    qInfo() << "Global hotkeys: no" << kService << "on this bus - this desktop offers no global shortcuts.";
    return false;
  }

  const QDBusMessage reply = bus.call( QDBusMessage::createMethodCall( kService, kPath, kPropertiesIface,
                                         QStringLiteral( "Get" ) )
      << kIface << QStringLiteral( "version" ),
      QDBus::Block, methodReplyTimeoutMs );

  if ( reply.type() == QDBusMessage::ErrorMessage ) {
    qInfo() << "Global hotkeys:" << reply.errorName() << "-" << reply.errorMessage()
            << "- this desktop offers no global-shortcut portal.";
    return false;
  }

  const uint version = variantValue( reply.arguments().at( 0 ) ).toUInt();

  if ( version < 2 ) {
    qInfo() << "Global hotkeys: the portal implements version" << version
            << "of org.freedesktop.portal.GlobalShortcuts; version 2 (xdg-desktop-portal >= 1.16) is needed.";
    return false;
  }

  if ( !bus.connect( kService, kPath, kIface, QStringLiteral( "Activated" ), this,
      SLOT( handleActivated( QDBusObjectPath, QString, quint64, QVariantMap ) ) ) )
    qWarning() << "Global hotkeys: could not listen for the portal's Activated signal.";

  ready = true;
  return true;
}

void PortalShortcuts::add( const QString & id, const QKeySequence & sequence, const QString & description )
{
  if ( !ready || sequence.isEmpty() )
    return;

  bool replaced = false;

  for ( Request & r : requests )
    if ( r.id == id ) {
      r.sequence = sequence;
      r.description = description;
      replaced = true;
      break;
    }

  if ( !replaced )
    requests.append( Request{ id, sequence, description } );

  if ( !commitScheduled ) {
    commitScheduled = true;
    QTimer::singleShot( 0, this, &PortalShortcuts::rebind );
  }
}

void PortalShortcuts::clear()
{
  requests.clear();
  commitScheduled = false;
  closeSession();
}

void PortalShortcuts::releaseDesktopShortcuts( const QStringList & ids )
{
  if ( ids.isEmpty() ) {
    return;
  }

  QDBusConnection bus = QDBusConnection::sessionBus();

  if ( !bus.isConnected() ) {
    return;
  }

  const QDBusMessage listed = bus.call(
    QDBusMessage::createMethodCall( kAccelService, kAccelPath, kAccelIface, QStringLiteral( "allComponents" ) ),
    QDBus::Block,
    methodReplyTimeoutMs );

  if ( listed.type() == QDBusMessage::ErrorMessage ) {
    return;
  }

  const QList< QDBusObjectPath > components = qdbus_cast< QList< QDBusObjectPath > >( listed.arguments().at( 0 ) );

  QStringList released;

  for ( const QDBusObjectPath & component : components ) {
    const QDBusMessage names = bus.call( QDBusMessage::createMethodCall( kAccelService,
                                                                         component.path(),
                                                                         kComponentIface,
                                                                         QStringLiteral( "shortcutNames" ) ),
                                         QDBus::Block,
                                         methodReplyTimeoutMs );

    if ( names.type() == QDBusMessage::ErrorMessage ) {
      continue;
    }

    const QStringList owned = qdbus_cast< QStringList >( names.arguments().at( 0 ) );

    bool ours = false;

    for ( const QString & id : ids ) {
      if ( owned.contains( id ) ) {
        ours = true;
        break;
      }
    }

    if ( !ours ) {
      continue;
    }

    const QDBusMessage cleaned = bus.call(
      QDBusMessage::createMethodCall( kAccelService, component.path(), kComponentIface, QStringLiteral( "cleanUp" ) ),
      QDBus::Block,
      methodReplyTimeoutMs );

    if ( cleaned.type() != QDBusMessage::ErrorMessage && cleaned.arguments().value( 0 ).toBool() ) {
      released << component.path();
    }
  }

  if ( !released.isEmpty() ) {
    qInfo() << "Global hotkeys: released the desktop's grabs held by" << released;
  }
}

QString PortalShortcuts::assignedTrigger( const QString & id ) const
{
  return assigned.value( id );
}

QString PortalShortcuts::toPortalTrigger( const QKeySequence & sequence )
{
  if ( sequence.isEmpty() )
    return {};

  const QKeyCombination combo = sequence[ 0 ];
  const QString key = keySymName( combo.key() );

  if ( key.isEmpty() )
    return {};

  QStringList parts;
  const Qt::KeyboardModifiers modifiers = combo.keyboardModifiers();

  if ( modifiers & Qt::SHIFT )
    parts << QStringLiteral( "SHIFT" );
  if ( modifiers & Qt::CTRL )
    parts << QStringLiteral( "CTRL" );
  if ( modifiers & Qt::ALT )
    parts << QStringLiteral( "ALT" );
  if ( modifiers & Qt::KeypadModifier )
    parts << QStringLiteral( "NUM" );
  if ( modifiers & Qt::META )
    parts << QStringLiteral( "LOGO" );

  parts << key;

  return parts.join( QLatin1Char( '+' ) );
}

void PortalShortcuts::rebind()
{
  commitScheduled = false;

  if ( !ready || requests.isEmpty() )
    return;

  closeSession();
  createSession();
}

QString PortalShortcuts::requestPathFor( const QString & token )
{
  QString sender = QDBusConnection::sessionBus().baseService();

  sender.remove( QLatin1Char( ':' ) );
  sender.replace( QLatin1Char( '.' ), QLatin1Char( '_' ) );

  return QStringLiteral( "%1/request/%2/%3" ).arg( kPath, sender, token );
}

void PortalShortcuts::watchRequest( const QString & token )
{
  unwatchRequest();

  pendingRequestPath = requestPathFor( token );

  if ( !QDBusConnection::sessionBus().connect( kService, pendingRequestPath, kRequestIface,
      QStringLiteral( "Response" ), this, SLOT( handleResponse( uint, QVariantMap ) ) ) )
    qWarning() << "Global hotkeys: could not listen for the portal's answer.";
}

void PortalShortcuts::unwatchRequest()
{
  if ( pendingRequestPath.isEmpty() )
    return;

  QDBusConnection::sessionBus().disconnect( kService, pendingRequestPath, kRequestIface,
    QStringLiteral( "Response" ), this, SLOT( handleResponse( uint, QVariantMap ) ) );

  pendingRequestPath.clear();
}

void PortalShortcuts::createSession()
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  const QString token = QStringLiteral( "gd%1" ).arg( ++tokenCounter );

  stage = Stage::CreatingSession;
  watchRequest( token );
  sessionPending = true;

  const QString sessionToken = QStringLiteral( "gd%1" ).arg( ++tokenCounter );

  QTimer::singleShot( createSessionTimeoutMs, this, [ this ] {
    if ( sessionPending ) {
      sessionPending = false;
      stage = Stage::None;
      unwatchRequest();
      qWarning() << "Global hotkeys: the portal never answered CreateSession.";
    }
  } );

  QVariantMap options;
  options[ QStringLiteral( "handle_token" ) ] = token;
  options[ QStringLiteral( "session_handle_token" ) ] = sessionToken;

  const QDBusMessage reply = bus.call( QDBusMessage::createMethodCall( kService, kPath, kIface,
                                         QStringLiteral( "CreateSession" ) )
      << options,
      QDBus::Block, methodReplyTimeoutMs );

  if ( reply.type() == QDBusMessage::ErrorMessage ) {
    sessionPending = false;
    stage = Stage::None;
    unwatchRequest();
    qWarning() << "Global hotkeys: CreateSession failed:" << reply.errorName() << "-" << reply.errorMessage();
  }
}

void PortalShortcuts::bindShortcuts()
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  QList< PortalShortcutEntry > entries;

  for ( const Request & r : requests ) {
    const QString trigger = toPortalTrigger( r.sequence );

    if ( trigger.isEmpty() ) {
      qWarning() << "Global hotkey" << r.sequence.toString()
                 << "cannot be written the way the desktop portal spells shortcuts - not registered.";
      continue;
    }

    QVariantMap properties;
    properties[ QStringLiteral( "description" ) ] = r.description;
    properties[ QStringLiteral( "preferred_trigger" ) ] = trigger;
    entries.append( PortalShortcutEntry{ r.id, properties } );
  }

  if ( entries.isEmpty() ) {
    stage = Stage::None;
    return;
  }

  const QString token = QStringLiteral( "gd%1" ).arg( ++tokenCounter );

  stage = Stage::Binding;
  watchRequest( token );

  QVariantMap options;
  options[ QStringLiteral( "handle_token" ) ] = token;

  const QDBusMessage bind = QDBusMessage::createMethodCall( kService, kPath, kIface,
                                            QStringLiteral( "BindShortcuts" ) )
      << QDBusObjectPath( sessionHandle ) << QVariant::fromValue( entries ) << QString() << options;

  QDBusPendingCallWatcher * watcher = new QDBusPendingCallWatcher( bus.asyncCall( bind, bindReplyTimeoutMs ), this );

  connect( watcher, &QDBusPendingCallWatcher::finished, this,
  [ this ]( QDBusPendingCallWatcher * w ) {
    if ( w->isError() ) {
      stage = Stage::None;
      unwatchRequest();
      qWarning() << "Global hotkeys: BindShortcuts failed:" << w->error().name() << "-" << w->error().message();
    }

    w->deleteLater();
  } );
}

void PortalShortcuts::handleResponse( uint response, const QVariantMap & results )
{
  const Stage answered = stage;

  stage = Stage::None;
  sessionPending = false;
  unwatchRequest();

  if ( answered == Stage::CreatingSession ) {
    if ( response != 0 ) {
      qWarning() << "Global hotkeys: the portal refused the shortcut session (response" << response << ").";
      return;
    }

    sessionHandle = results.value( QStringLiteral( "session_handle" ) ).toString();

    if ( sessionHandle.isEmpty() ) {
      qWarning() << "Global hotkeys: the portal created a session without naming it.";
      return;
    }

    bindShortcuts();
    return;
  }

  if ( answered != Stage::Binding )
    return;

  if ( response != 0 ) {
    qWarning() << "Global hotkeys: the desktop did not accept the shortcuts (response" << response
               << "; 1 = dismissed, 2 = shortcut taken, 3 = not allowed)."
               << "The desktop's own shortcut settings say why.";
    return;
  }

  const QList< PortalShortcutEntry > boundShortcuts = qdbus_cast< QList< PortalShortcutEntry > >(
    variantValue( results.value( QStringLiteral( "shortcuts" ) ) ) );

  QStringList what;

  for ( const PortalShortcutEntry & e : boundShortcuts ) {
    const QString trigger = e.properties.value( QStringLiteral( "trigger_description" ) ).toString();

    if ( !trigger.isEmpty() ) {
      assigned.insert( e.id, trigger );
      what << QStringLiteral( "%1 = %2" ).arg( e.id, trigger );
    }
  }

  qInfo() << "Global hotkeys registered with the desktop:"
          << ( what.isEmpty() ? QStringLiteral( "accepted" ) : what.join( QStringLiteral( ", " ) ) );
}

void PortalShortcuts::handleActivated( const QDBusObjectPath & session, const QString & id,
                                       [[maybe_unused]] quint64 timestamp, [[maybe_unused]] const QVariantMap & options )
{

  if ( !sessionHandle.isEmpty() && session.path() == sessionHandle )
    emit activated( id );
}

void PortalShortcuts::closeSession()
{
  stage = Stage::None;
  sessionPending = false;
  unwatchRequest();

  if ( !sessionHandle.isEmpty() ) {
    QDBusConnection::sessionBus().call( QDBusMessage::createMethodCall( kService, sessionHandle,
                                        kSessionIface, QStringLiteral( "Close" ) ),
        QDBus::Block, closeTimeoutMs );
    sessionHandle.clear();
  }

  assigned.clear();
}
