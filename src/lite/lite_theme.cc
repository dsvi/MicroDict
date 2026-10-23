#include "lite_theme.hh"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

namespace {

const QString kService = QStringLiteral( "org.freedesktop.portal.Desktop" );
const QString kPath = QStringLiteral( "/org/freedesktop/portal/desktop" );
const QString kSettingsIface = QStringLiteral( "org.freedesktop.portal.Settings" );
const QString kPropertiesIface = QStringLiteral( "org.freedesktop.DBus.Properties" );

struct PortalAccent
{
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
};

QDBusArgument & operator<<( QDBusArgument & arg, const PortalAccent & c )
{
  arg.beginStructure();
  arg << c.r << c.g << c.b;
  arg.endStructure();
  return arg;
}

const QDBusArgument & operator>>( const QDBusArgument & arg, PortalAccent & c )
{
  arg.beginStructure();
  arg >> c.r >> c.g >> c.b;
  arg.endStructure();
  return arg;
}

struct RegisterPortalAccent
{
  RegisterPortalAccent()
  {
    qDBusRegisterMetaType< PortalAccent >();
  }
};

const RegisterPortalAccent registerPortalAccent;

const QString kAppearance = QStringLiteral( "org.freedesktop.appearance" );
const QString kColorScheme = QStringLiteral( "color-scheme" );
const QString kAccentColor = QStringLiteral( "accent-color" );

constexpr int settingsReplyTimeoutMs = 1500;

QColor roleColor( const QString & text )
{
  const QStringList parts = text.split( ',', Qt::SkipEmptyParts );

  if ( parts.size() < 3 )
    return {};

  return QColor( parts.at( 0 ).trimmed().toInt(), parts.at( 1 ).trimmed().toInt(),
                 parts.at( 2 ).trimmed().toInt() );
}

double luminance( const QColor & c )
{
  return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

Qt::ColorScheme schemeFromName( const QString & name )
{
  const QString lower = name.toLower();

  if ( lower.contains( QStringLiteral( "dark" ) ) )
    return Qt::ColorScheme::Dark;

  if ( lower.contains( QStringLiteral( "light" ) ) )
    return Qt::ColorScheme::Light;

  return Qt::ColorScheme::Unknown;
}

Qt::ColorScheme schemeFromPreference( uint preference )
{
  switch ( preference ) {
    case 1:
      return Qt::ColorScheme::Dark;
    case 2:
      return Qt::ColorScheme::Light;
    default:
      return Qt::ColorScheme::Unknown;
  }
}

QVariant unwrap( const QVariant & value )
{
  QVariant v = value;

  while ( v.metaType() == QMetaType::fromType< QDBusVariant >() ) {
    v = v.value< QDBusVariant >().variant();
  }

  if ( v.metaType() == QMetaType::fromType< QDBusArgument >() ) {
    const QDBusArgument arg = v.value< QDBusArgument >();

    if ( arg.currentType() == QDBusArgument::StructureType ) {
      return QVariant::fromValue( qdbus_cast< PortalAccent >( arg ) );
    }

    return arg.asVariant();
  }

  return v;
}

QColor accentFromVariant( const QVariant & value )
{
  const QVariant v = unwrap( value );

  if ( v.metaType() == QMetaType::fromType< PortalAccent >() ) {
    const PortalAccent c = qvariant_cast< PortalAccent >( v );

    if ( c.r >= 0.0 && c.r <= 1.0 && c.g >= 0.0 && c.g <= 1.0 && c.b >= 0.0 && c.b <= 1.0 ) {
      return QColor::fromRgbF( c.r, c.g, c.b );
    }

    return {};
  }

  const QVariantList parts = v.toList();

  if ( parts.size() >= 3 ) {
    const double r = parts.at( 0 ).toDouble();
    const double g = parts.at( 1 ).toDouble();
    const double b = parts.at( 2 ).toDouble();

    if ( r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0 ) {
      return QColor::fromRgbF( r, g, b );
    }
  }

  return {};
}

QVariant readPortalProperty( const QString & iface, const QString & property )
{
  const QDBusConnection bus = QDBusConnection::sessionBus();

  if ( !bus.isConnected() ) {
    return {};
  }

  const QDBusMessage reply = bus.call(
    QDBusMessage::createMethodCall( kService, kPath, kPropertiesIface, QStringLiteral( "Get" ) ) << iface << property,
    QDBus::Block,
    settingsReplyTimeoutMs );

  if ( reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty() ) {
    return {};
  }

  return unwrap( reply.arguments().at( 0 ) );
}

QVariant readPortalSetting( const QString & space, const QString & key )
{
  const QDBusConnection bus = QDBusConnection::sessionBus();

  if ( !bus.isConnected() ) {
    return {};
  }

  const QDBusMessage reply = bus.call(
    QDBusMessage::createMethodCall( kService, kPath, kSettingsIface, QStringLiteral( "Read" ) ) << space << key,
    QDBus::Block,
    settingsReplyTimeoutMs );

  if ( reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty() ) {
    return {};
  }

  return unwrap( reply.arguments().at( 0 ) );
}

QVariantMap readIniGroups( const QString & path, const QStringList & wanted )
{
  QFile file( path );

  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return {};

  QVariantMap groups;
  QString group;

  while ( !file.atEnd() ) {
    const QString line = QString::fromUtf8( file.readLine() ).trimmed();

    if ( line.isEmpty() || line.startsWith( QLatin1Char( '#' ) ) || line.startsWith( QLatin1Char( ';' ) ) )
      continue;

    if ( line.startsWith( QLatin1Char( '[' ) ) && line.endsWith( QLatin1Char( ']' ) ) ) {
      group = line.mid( 1, line.length() - 2 );
      continue;
    }

    if ( !wanted.contains( group ) )
      continue;

    const int separator = line.indexOf( QLatin1Char( '=' ) );

    if ( separator <= 0 )
      continue;

    QVariantMap entries = groups.value( group ).toMap();
    const QString key = line.left( separator ).trimmed();

    if ( !entries.contains( key ) )
      entries.insert( key, line.mid( separator + 1 ).trimmed() );

    groups.insert( group, entries );
  }

  return groups;
}

}

bool isDarkPalette( const QPalette & p )
{
  const QColor b = p.color( QPalette::Base );

  return 0.2126 * b.redF() + 0.7152 * b.greenF() + 0.0722 * b.blueF() < 0.5;
}

SystemAppearance & SystemAppearance::instance()
{
  static SystemAppearance self;
  return self;
}

SystemAppearance::SystemAppearance()
{
  refresh();

#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
  if ( QStyleHints * hints = QGuiApplication::styleHints() ) {
    connect( hints,
             &QStyleHints::colorSchemeChanged,
             this,
             &SystemAppearance::onQtColorSchemeChanged );
  }
#endif

  listenToPortal();

  watchKdeGlobalsDir();
}

bool SystemAppearance::isDark() const
{
  return current == Qt::ColorScheme::Dark;
}

Qt::ColorScheme SystemAppearance::scheme() const
{
  return current;
}

bool SystemAppearance::publishesColors() const
{
  return haveDesktopColors;
}

QString SystemAppearance::describe() const
{
  const QString what = current == Qt::ColorScheme::Dark ? QStringLiteral( "dark" )
                     : current == Qt::ColorScheme::Light ? QStringLiteral( "light" )
                                                        : QStringLiteral( "unknown" );

  return QStringLiteral( "%1, from %2" ).arg( what, decidedBy.isEmpty() ? QStringLiteral( "nothing" ) : decidedBy );
}

void SystemAppearance::refresh()
{
  const Qt::ColorScheme previous = current;
  const QString previousBy = decidedBy;

  readAppearance();

  const QString configDir = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );

  fileGroups = readIniGroups( configDir + QStringLiteral( "/kdeglobals" ),
                              { QStringLiteral( "Colors:Window" ),
                                QStringLiteral( "Colors:View" ),
                                QStringLiteral( "Colors:Button" ),
                                QStringLiteral( "Colors:Tooltip" ),
                                QStringLiteral( "General" ),
                                QStringLiteral( "KDE" ) } );

  watchKdeGlobalsDir();

  Qt::ColorScheme found = Qt::ColorScheme::Unknown;
  QString from;

  if ( portalScheme.isValid() ) {
    found = schemeFromPreference( portalScheme.toUInt() );

    if ( found != Qt::ColorScheme::Unknown )
      from = QStringLiteral( "the desktop portal's %1 %2 setting" ).arg( kAppearance, kColorScheme );
  }

  if ( found == Qt::ColorScheme::Unknown ) {
    QString name = role( QStringLiteral( "General" ), QStringLiteral( "ColorScheme" ) );

    if ( name.isEmpty() )
      name = role( QStringLiteral( "KDE" ), QStringLiteral( "ColorScheme" ) );

    found = schemeFromName( name );

    if ( found != Qt::ColorScheme::Unknown )
      from = QStringLiteral( "the colour scheme \"%1\" named in kdeglobals" ).arg( name );
  }

  if ( found == Qt::ColorScheme::Unknown ) {
    const QColor window = roleColor( role( QStringLiteral( "Colors:Window" ),
                                           QStringLiteral( "BackgroundNormal" ) ) );

    if ( window.isValid() ) {
      found = luminance( window ) < 0.5 ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
      from = QStringLiteral( "the desktop's own window colour %1" ).arg( window.name() );
    }
  }

  if ( found == Qt::ColorScheme::Unknown ) {
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
    if ( QStyleHints * hints = QGuiApplication::styleHints() ) {
      found = hints->colorScheme();

      if ( found != Qt::ColorScheme::Unknown )
        from = QStringLiteral( "the desktop's own theme, as Qt reads it" );
    }
#endif
  }

  current = found;
  decidedBy = from;

  QPalette palette;
  const bool colorsAvailable = desktopPalette( palette );
  const bool colorsMoved = colorsAvailable && palette != lastPalette;

  haveDesktopColors = colorsAvailable;

  if ( colorsAvailable ) {
    if ( colorsMoved ) {
      if ( QGuiApplication * gui = qobject_cast< QGuiApplication * >( qApp ) )
        gui->setPalette( palette );
    }

    lastPalette = palette;
  }

  if ( current != previous || colorsMoved || decidedBy != previousBy )
    emit changed();
}

void SystemAppearance::readAppearance()
{
  portalScheme = QVariant();
  portalAccent = QColor();

  const QDBusConnection bus = QDBusConnection::sessionBus();

  if ( !bus.isConnected() )
    return;

  const QVariant settingsVersion = readPortalProperty( kSettingsIface, QStringLiteral( "version" ) );

  if ( !settingsVersion.isValid() ) {
    qInfo() << "The desktop publishes no settings over D-Bus"
            << "- reading the desktop's colours falls back to its own files.";
    return;
  }

  portalScheme = readPortalSetting( kAppearance, kColorScheme );
  portalAccent = accentFromVariant( readPortalSetting( kAppearance, kAccentColor ) );
}

QString SystemAppearance::role( const QString & group, const QString & key ) const
{
  return fileGroups.value( group ).toMap().value( key ).toString();
}

QColor SystemAppearance::semanticForeground( const QString & key ) const
{
  QColor c = roleColor( role( QStringLiteral( "Colors:View" ), key ) );

  if ( !c.isValid() )
    c = roleColor( role( QStringLiteral( "Colors:Window" ), key ) );

  return c;
}

bool SystemAppearance::desktopPalette( QPalette & out ) const
{
  auto roleColorOf = [ this ]( const QString & group, const QString & key ) -> QColor {
    return roleColor( role( group, key ) );
  };

  const QString windowGroup = QStringLiteral( "Colors:Window" );
  const QString viewGroup    = QStringLiteral( "Colors:View" );
  const QString buttonGroup = QStringLiteral( "Colors:Button" );
  const QString tooltipGroup = QStringLiteral( "Colors:Tooltip" );

  const QColor windowBg = roleColorOf( windowGroup, QStringLiteral( "BackgroundNormal" ) );
  const QColor windowFg = roleColorOf( windowGroup, QStringLiteral( "ForegroundNormal" ) );
  const QColor viewBg = roleColorOf( viewGroup, QStringLiteral( "BackgroundNormal" ) );
  const QColor viewFg = roleColorOf( viewGroup, QStringLiteral( "ForegroundNormal" ) );

  if ( !windowBg.isValid() || !windowFg.isValid() || !viewBg.isValid() || !viewFg.isValid() )
    return false;

  QColor windowInactive = roleColorOf( windowGroup, QStringLiteral( "ForegroundInactive" ) );

  if ( !windowInactive.isValid() )
    windowInactive = windowFg.darker( 160 );

  QColor viewInactive = roleColorOf( viewGroup, QStringLiteral( "ForegroundInactive" ) );

  if ( !viewInactive.isValid() )
    viewInactive = viewFg.darker( 160 );

  QColor accent = portalAccent;

  if ( !accent.isValid() )
    accent = QPalette().color( QPalette::Highlight );

  const QColor accentFg = luminance( accent ) < 0.5 ? Qt::white : Qt::black;

  QColor buttonBg = roleColorOf( buttonGroup, QStringLiteral( "BackgroundNormal" ) );

  if ( !buttonBg.isValid() )
    buttonBg = windowBg;

  QColor buttonFg = roleColorOf( buttonGroup, QStringLiteral( "ForegroundNormal" ) );

  if ( !buttonFg.isValid() )
    buttonFg = windowFg;

  QColor tooltipBg = roleColorOf( tooltipGroup, QStringLiteral( "BackgroundNormal" ) );
  QColor tooltipFg = roleColorOf( tooltipGroup, QStringLiteral( "ForegroundNormal" ) );

  if ( !tooltipBg.isValid() )
    tooltipBg = windowBg;

  if ( !tooltipFg.isValid() )
    tooltipFg = windowFg;

  QColor link = roleColorOf( viewGroup, QStringLiteral( "ForegroundLink" ) );

  if ( !link.isValid() )
    link = accent;

  QPalette p;

  p.setColor( QPalette::Window, windowBg );
  p.setColor( QPalette::WindowText, windowFg );
  p.setColor( QPalette::Base, viewBg );
  p.setColor( QPalette::AlternateBase,
              roleColorOf( viewGroup, QStringLiteral( "BackgroundAlternate" ) ).isValid()
                ? roleColorOf( viewGroup, QStringLiteral( "BackgroundAlternate" ) )
                : viewBg );
  p.setColor( QPalette::ToolTipBase, tooltipBg );
  p.setColor( QPalette::ToolTipText, tooltipFg );
  p.setColor( QPalette::Text, viewFg );
  p.setColor( QPalette::Button, buttonBg );
  p.setColor( QPalette::ButtonText, buttonFg );
  p.setColor( QPalette::BrightText,
              roleColorOf( windowGroup, QStringLiteral( "ForegroundNegative" ) ).isValid()
                ? roleColorOf( windowGroup, QStringLiteral( "ForegroundNegative" ) )
                : Qt::red );
  p.setColor( QPalette::Link, link );
  p.setColor( QPalette::LinkVisited,
              roleColorOf( viewGroup, QStringLiteral( "ForegroundVisited" ) ).isValid()
                ? roleColorOf( viewGroup, QStringLiteral( "ForegroundVisited" ) )
                : link );
  p.setColor( QPalette::Highlight, accent );
  p.setColor( QPalette::HighlightedText, accentFg );
  p.setColor( QPalette::PlaceholderText, viewInactive );

  p.setColor( QPalette::Disabled, QPalette::WindowText, windowInactive );
  p.setColor( QPalette::Disabled, QPalette::Text, viewInactive );
  p.setColor( QPalette::Disabled, QPalette::ButtonText, windowInactive );
  p.setColor( QPalette::Disabled, QPalette::HighlightedText, windowInactive );

  p.setColor( QPalette::Inactive, QPalette::WindowText, windowInactive );
  p.setColor( QPalette::Inactive, QPalette::Text, viewInactive );
  p.setColor( QPalette::Inactive, QPalette::ButtonText, windowInactive );
  p.setColor( QPalette::Inactive, QPalette::Highlight, windowBg );
  p.setColor( QPalette::Inactive, QPalette::HighlightedText, windowInactive );

  QColor shade1 = roleColorOf( windowGroup, QStringLiteral( "BackgroundShade1" ) );
  QColor shade2 = roleColorOf( windowGroup, QStringLiteral( "BackgroundShade2" ) );
  QColor shade3 = roleColorOf( windowGroup, QStringLiteral( "BackgroundShade3" ) );

  if ( !shade1.isValid() )
    shade1 = windowBg.darker( 115 );

  if ( !shade2.isValid() )
    shade2 = windowBg.darker( 140 );

  if ( !shade3.isValid() )
    shade3 = windowBg.darker( 180 );

  p.setColor( QPalette::Midlight, windowBg.lighter( 115 ) );
  p.setColor( QPalette::Mid, shade1 );
  p.setColor( QPalette::Dark, shade2 );
  p.setColor( QPalette::Shadow, shade3 );
  p.setColor( QPalette::Accent, accent );

  out = p;
  return true;
}

QString articleThemeCss( const QPalette & p, bool dark )
{
  const QColor base = p.color( QPalette::Base );
  const QColor text = p.color( QPalette::Text );

  QColor alt = p.color( QPalette::AlternateBase );
  if ( !alt.isValid() || alt == base )
    alt = base.lighter( luminance( base ) < 0.5 ? 118 : 88 );

  QColor meta = p.color( QPalette::PlaceholderText );
  if ( !meta.isValid() || meta == text )
    meta = text.lighter( luminance( base ) < 0.5 ? 150 : 62 );

  QColor link = p.color( QPalette::Link );
  if ( !link.isValid() )
    link = p.color( QPalette::Accent ).isValid() ? p.color( QPalette::Accent ) : text;

  const QColor accent     = p.color( QPalette::Highlight );
  const QColor accentText = p.color( QPalette::HighlightedText );

  QColor negative = p.color( QPalette::BrightText );
  if ( !negative.isValid() )
    negative = SystemAppearance::instance().semanticForeground( QStringLiteral( "ForegroundNegative" ) );
  if ( !negative.isValid() )
    negative = text;

  QColor positive = SystemAppearance::instance().semanticForeground( QStringLiteral( "ForegroundPositive" ) );
  if ( !positive.isValid() )
    positive = text;

  QColor neutral = SystemAppearance::instance().semanticForeground( QStringLiteral( "ForegroundNeutral" ) );
  if ( !neutral.isValid() )
    neutral = text;

  QString css = QStringLiteral(
    "html { background-color: %1; }\n"
    "body { background-color: %1; color: %2; }\n"
    ".gdarticle { background-color: %1; color: %2; }\n"
    ".gdarticlebody { color: %2; }\n"
    ".gddictname { background-color: %3; color: %2; }\n"
    ".gdactivearticle .gddictname { background-color: %4; color: %5; }\n"
    ".gdfromprefix, .gddicttitle { color: %2; }\n"
    "a, .xdxf_kref, .dsl_ref, .dsl_url, .dictd_article a:link, .welcome-container a,"
    " .hidden_expand_opt { color: %6; }\n"
    ".xdxf_c, .xdxf_co, .xdxf_co_old, .xdxf_headwords, .xdxf_def, .xdxf_dtrn, .xdxf_ex_orig,"
    " .xdxf_ex_tran, .welcome-container, gd-section-head { color: %2; }\n"
    ".xdxf_num { color: %8; }\n"
    ".xdxf_abbr, .xdxf_opt, .xdxf_tr, .xdxf_tr_old, .xdxf_ex,"
    " .xdxf_ex_old, .dictd_phonetic, .dsl_opt .dsl_ex, .dsl_ex .dsl_opt, .welcome-footer,"
    " .menu-path > span { color: %9; }\n"
    ".xdxf_gr, .xdxf_gr_old { color: %7; }\n"
    ".dsl_article .c_default_color, .dsl_p { color: %10; }\n" )
                  .arg( base.name() )
                  .arg( text.name() )
                  .arg( alt.name() )
                  .arg( accent.name() )
                  .arg( accentText.name() )
                  .arg( link.name() )
                  .arg( neutral.name() )
                  .arg( negative.name() )
                  .arg( meta.name() )
                  .arg( positive.name() );

  if ( dark )
    css += QStringLiteral( "img { background-color: white; }\n" );

  return css;
}

void SystemAppearance::listenToPortal()
{
  if ( !QDBusConnection::sessionBus().isConnected() )
    return;

  if ( !QDBusConnection::sessionBus().connect( kService, kPath, kSettingsIface,
        QStringLiteral( "SettingChanged" ), this,
        SLOT( onPortalSettingChanged( QString, QString, QDBusVariant ) ) ) )
    qWarning() << "Could not listen for the desktop's settings changing.";
}

void SystemAppearance::onPortalSettingChanged( const QString & space, const QString & key,
                                               const QDBusVariant & value )
{
  if ( space != kAppearance ) {
    return;
  }

  const QVariant v = unwrap( value.variant() );

  qInfo() << "The desktop changed" << space << key << "-"
          << ( v.metaType() == QMetaType::fromType< PortalAccent >() ? QColor::fromRgbF( v.value< PortalAccent >().r,
                                                                                         v.value< PortalAccent >().g,
                                                                                         v.value< PortalAccent >().b )
                                                                         .name() :
                                                                       v.toString() );

  refresh();
}

void SystemAppearance::watchKdeGlobalsDir()
{
  if ( watcher )
    return;

  const QString configDir = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation );

  if ( configDir.isEmpty() || !QFile::exists( configDir ) )
    return;

  watcher = new QFileSystemWatcher( this );
  watcher->addPath( configDir );

  connect( watcher, &QFileSystemWatcher::directoryChanged,
           this, &SystemAppearance::onKdeGlobalsDirChanged );
}

void SystemAppearance::onKdeGlobalsDirChanged( const QString & )
{
  QTimer::singleShot( 250, this, [ this ] { refresh(); } );
}

void SystemAppearance::onQtColorSchemeChanged( Qt::ColorScheme scheme )
{
  if ( portalScheme.isValid() || !fileGroups.isEmpty() || scheme == Qt::ColorScheme::Unknown || scheme == current ) {
    return;
  }

  current = scheme;
  decidedBy = QStringLiteral( "the desktop's own theme, as Qt reads it" );

  emit changed();
}
