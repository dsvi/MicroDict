#include "portal_filechooser.hh"

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QString>
#include <QVariant>

#include <cstdlib>

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )

namespace {

const QString kService          = QStringLiteral( "org.freedesktop.portal.Desktop" );
const QString kPath             = QStringLiteral( "/org/freedesktop/portal/desktop" );
const QString kPropertiesIface  = QStringLiteral( "org.freedesktop.DBus.Properties" );
const QString kFileChooserIface = QStringLiteral( "org.freedesktop.portal.FileChooser" );

const char kPortalTheme[] = "xdgdesktopportal";

constexpr int probeTimeoutMs = 3000;

}

namespace PortalFileChooser {

bool portalOffersFileChooser()
{
  if ( !QDBusConnection::sessionBus().isConnected() ) {
    return false;
  }

  QDBusMessage message = QDBusMessage::createMethodCall( kService, kPath, kPropertiesIface, QStringLiteral( "Get" ) );
  message << kFileChooserIface << QStringLiteral( "version" );

  const QDBusReply< QVariant > version = QDBusConnection::sessionBus().call( message, QDBus::Block, probeTimeoutMs );

  return version.isValid() && version.value().toUInt() >= static_cast< uint >( minChooserVersion );
}

bool preferPortalFileChooser()
{
  if ( qEnvironmentVariableIsSet( "QT_QPA_PLATFORMTHEME" ) ) {
    return false;
  }

  if ( !portalOffersFileChooser() ) {
    return false;
  }

  return setenv( "QT_QPA_PLATFORMTHEME", kPortalTheme, 1 ) == 0;
}

}

#else

namespace PortalFileChooser {

bool portalOffersFileChooser()
{
  return false;
}

bool preferPortalFileChooser()
{
  return false;
}

}

#endif
