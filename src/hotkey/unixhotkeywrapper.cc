/* Licensed under GPLv3 or later, see the LICENSE file */

#include "hotkeywrapper.hh"

#include "portalshortcuts.hh"

#include <QKeyCombination>
#include <QTimer>
#include <QKeySequence>

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )

namespace {

const QString shortcutName = QStringLiteral( "clipboard-translate" );

constexpr int secondKeyTimeoutMs = 500;

}

HotkeyWrapper::HotkeyWrapper( QObject * parent ):
  QThread( parent ),
  state2( false )
{
  portal = new PortalShortcuts( this );

  if ( !portal->initialize() )
    return;

  connect( portal, &PortalShortcuts::activated, this, &HotkeyWrapper::portalActivated );

  portalReady = true;
}

HotkeyWrapper::~HotkeyWrapper()
{
  unregister();
}

bool HotkeyWrapper::setGlobalKey( const QKeySequence & key, int handle )
{
  if ( !portalReady || key.isEmpty() )
    return false;

  const QKeyCombination first = key[ 0 ];

  if ( first.key() == Qt::Key_unknown )
    return false;

  const QKeyCombination second = key.count() > 1 ? key[ 1 ] : QKeyCombination();

  hotkeys.append( HotkeyStruct( quint32( first.key() ),
                                second.key() == Qt::Key_unknown ? 0 : quint32( second.key() ),
                                quint32( first.keyboardModifiers() ),
                                handle, 0 ) );

  portalIndex.insert( shortcutName, hotkeys.size() - 1 );

  portal->add( shortcutName, key, tr( "Show the main window and look up the clipboard contents" ) );

  return true;
}

void HotkeyWrapper::unregister()
{
  if ( portal )
    portal->clear();

  hotkeys.clear();
  portalIndex.clear();
  state2 = false;
}

void HotkeyWrapper::releaseDesktopShortcuts()
{
  PortalShortcuts::releaseDesktopShortcuts( { shortcutName } );
}

void HotkeyWrapper::waitKey2()
{
  state2 = false;
}

void HotkeyWrapper::portalActivated( const QString & id )
{
  const int index = portalIndex.value( id, -1 );

  if ( index < 0 || index >= hotkeys.size() )
    return;

  const HotkeyStruct & hs = hotkeys.at( index );

  if ( state2 ) {
    waitKey2();

    if ( state2waiter.key2 == hs.key && state2waiter.modifier == hs.modifier ) {
      emit hotkeyActivated( state2waiter.handle );
      return;
    }
  }

  if ( hs.key2 == 0 )
    emit hotkeyActivated( hs.handle );
  else {
    state2 = true;
    state2waiter = hs;

    QTimer::singleShot( secondKeyTimeoutMs, this, &HotkeyWrapper::waitKey2 );
  }
}

#endif
