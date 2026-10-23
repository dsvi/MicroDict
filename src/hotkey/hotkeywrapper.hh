#pragma once

#include <QGuiApplication>
#include <QHash>
#include "config.hh"
#include "ex.hh"
#include "utils.hh"

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
class PortalShortcuts;
#endif

#ifdef Q_OS_MAC
  #import <Carbon/Carbon.h>
#endif

struct HotkeyStruct
{
  HotkeyStruct() = default;
  HotkeyStruct( quint32 key, quint32 key2, quint32 modifier, int handle, int id ):
    key( key ),
    key2( key2 ),
    modifier( modifier ),
    handle( handle ),
    id( id ) {};

  quint32 key      = 0;
  quint32 key2     = 0;
  quint32 modifier = 0;
  int handle       = 0;
  int id           = 0;
#ifdef Q_OS_MAC
  EventHotKeyRef hkRef  = nullptr;
  EventHotKeyRef hkRef2 = nullptr;
#endif
};

class HotkeyWrapper: public QThread
{
  Q_OBJECT

public:

  explicit HotkeyWrapper( QObject * parent );
  ~HotkeyWrapper() override;

  bool setGlobalKey( const QKeySequence &, int handle );

  void unregister();

  static void releaseDesktopShortcuts();

  bool hotkeysAvailable() const
  {
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
    return portalReady;
#else
    return true;
#endif
  }

signals:
  void hotkeyActivated( int handle );

protected slots:
  void waitKey2();

private:
  void init();
  quint32 nativeKey( int key );

  QList< HotkeyStruct > hotkeys;

  bool state2;
  HotkeyStruct state2waiter;

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
  PortalShortcuts * portal = nullptr;

  bool portalReady = false;

  QHash< QString, int > portalIndex;

  void portalActivated( const QString & id );
#endif

#ifdef Q_OS_MAC

private:
  void sendCmdC();

  static EventHandlerUPP hotKeyFunction;
  quint32 keyC;
  EventHandlerRef handlerRef;

public:
  void activated( int hkId );
  void suspendHotkeys();
  void resumeHotkeys();

#endif

};
