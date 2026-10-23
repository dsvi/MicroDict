/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "config.hh"
#include "lite/portal_filechooser.hh"
#include "logger.hh"
#include "mainwindow.hh"
#include "version.hh"
#include <QByteArray>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QSessionManager>
#include <QString>
#include <QThreadPool>
#include <QTranslator>
#include <QLibraryInfo>
#include <stdio.h>
#if defined( Q_OS_UNIX )
  #include "unix/ksignalhandler.hh"
#endif

#ifdef Q_OS_MACOS
  #include "macos/mac_url_handler.hh"
  #include <QDesktopServices>
#endif

#include "qtsingleapplication.h"
using MD_QApplication = QtSingleApplication;

struct GDOptions
{
  bool logFile = false;
  QString word, groupName;

  inline bool needSetGroup() const
  {
    return !groupName.isEmpty();
  }

  inline QString getGroupName() const
  {
    return groupName;
  }

  inline bool needTranslateWord() const
  {
    return !word.isEmpty();
  }

  inline QString wordToTranslate() const
  {
    return word;
  }

  bool resetState = false;
};

void processCommandLine( QCoreApplication * app, GDOptions * result )
{
  QCommandLineParser qcmd;

  qcmd.setApplicationDescription( QObject::tr( "A dictionary lookup program." ) );
  qcmd.addHelpOption();

  qcmd.addPositionalArgument( "word", QObject::tr( "Word or sentence to query." ), "[word]" );

  QCommandLineOption logFileOption( QStringList() << "l"
                                                  << "log-to-file",
                                    QObject::tr( "Save debug messages to microdict_log.txt in the config folder." ) );

  QCommandLineOption resetState( QStringList() << "r"
                                               << "reset-window-state",
                                 QObject::tr( "Reset window state." ) );

  QCommandLineOption groupNameOption( QStringList() << "g"
                                                    << "group-name",
                                      QObject::tr( "Change the group of main window." ),
                                      "groupName" );
  QCommandLineOption printVersion( QStringList() << "v"
                                                 << "version",
                                   QObject::tr( "Print version and diagnosis info." ) );

  qcmd.addOption( logFileOption );
  qcmd.addOption( groupNameOption );
  qcmd.addOption( resetState );
  qcmd.addOption( printVersion );
  qcmd.process( *app );

  if ( qcmd.isSet( logFileOption ) ) {
    result->logFile = true;
  }

  if ( qcmd.isSet( groupNameOption ) ) {
    result->groupName = qcmd.value( groupNameOption );
  }


  if ( qcmd.isSet( resetState ) ) {
    result->resetState = true;
  }

  if ( qcmd.isSet( printVersion ) ) {
    qInfo() << qPrintable( Version::everything() );
    std::exit( 0 );
  }

  const QStringList posArgs = qcmd.positionalArguments();
  if ( !posArgs.empty() ) {
    QString originalArg = posArgs.at( 0 );

#ifdef Q_OS_LINUX
    auto schemePos = originalArg.indexOf( "://" );
    if ( schemePos != -1 ) {
      result->word = Utils::Url::extractWordFromUrl( originalArg );
    }
    else {
      result->word = originalArg;
    }
#else
    result->word = originalArg;
#endif
  }
}

int main( int argc, char ** argv )
{

  if ( qEnvironmentVariableIsSet( "MICRODICT_FORCE_XCB" ) ) {
    setenv( "QT_QPA_PLATFORM", "xcb", 1 );
  }
  else if ( qEnvironmentVariableIsSet( "MICRODICT_FORCE_WAYLAND" ) ) {
    setenv( "QT_QPA_PLATFORM", "wayland", 1 );
  }

  if ( const QByteArray requested = qgetenv( "QT_QPA_PLATFORM" ); !requested.isEmpty() ) {
    const auto colon = requested.indexOf( ':' );
    const auto key   = colon < 0 ? requested : requested.left( colon );
    if ( key == "wayland-egl" || key == "wayland-brcm" ) {
      const QByteArray plain = QByteArray( "wayland" ) + requested.mid( key.size() );
      qWarning(
        "QT_QPA_PLATFORM=%s asks for an EGL-backed Wayland surface, which this "
        "build has no EGL for: using the plain Wayland plugin instead (%s)",
        requested.constData(),
        plain.constData() );
      setenv( "QT_QPA_PLATFORM", plain.constData(), 1 );
    }
  }

  const bool portalChooser = PortalFileChooser::preferPortalFileChooser();

  qInfo() << "File chooser:" << ( portalChooser ? "the desktop's portal one" : "Qt's own" );

#ifdef Q_OS_MAC
  setenv( "LANG", "en_US.UTF-8", 1 );
#endif

  QGuiApplication::setHighDpiScaleFactorRoundingPolicy( Qt::HighDpiScaleFactorRoundingPolicy::PassThrough );

  MD_QApplication app( "MicroDict", argc, argv );

  QThreadPool::globalInstance()->setStackSize( 8u * 1024u * 1024u );

  app.setDesktopFileName( "io.github.dsvi.microdict" );
  MD_QApplication::setApplicationName( "MicroDict" );
  MD_QApplication::setOrganizationDomain( "dsvi.github.io" );
#ifndef Q_OS_MACOS
  MD_QApplication::setWindowIcon( QIcon( ":/icons/programicon.png" ) );
#endif

  GDOptions gdcl{};

  if ( argc > 1 ) {
    processCommandLine( &app, &gdcl );
  }

  QFont f = QApplication::font();
  f.setStyleStrategy( QFont::PreferAntialias );
  QApplication::setFont( f );

  if ( app.isRunning() ) {
    bool wasMessage = false;

    if ( gdcl.needTranslateWord() ) {
      QString message = "action:translate";
      if ( gdcl.needSetGroup() ) {
        message += "|group:" + gdcl.getGroupName();
      }
      QString encodedWord = QUrl::toPercentEncoding( gdcl.wordToTranslate() );
      message += "|word:" + encodedWord;
      app.sendMessage( message );
      wasMessage = true;
    }
    else if ( gdcl.needSetGroup() ) {
      app.sendMessage( QString( "setGroup: " ) + gdcl.getGroupName() );
      wasMessage = true;
    }

    if ( !wasMessage ) {
      app.sendMessage( "bringToFront" );
    }

    return 0;
  }

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  QDir::setCurrent( Config::getProgramDataDir() );
#endif

  Config::Class cfg;
  for ( ;; ) {
    try {
      cfg = Config::load();
    }
    catch ( Config::exError & ) {
      QMessageBox mb(
        QMessageBox::Warning,
        MD_QApplication::applicationName(),
        MD_QApplication::translate( "Main", "Error in configuration file. Continue with default settings?" ),
        QMessageBox::Yes | QMessageBox::No );
      mb.exec();
      if ( mb.result() != QMessageBox::Yes ) {
        return -1;
      }

      QString configFile = Config::getConfigFileName();
      QFile::rename( configFile,
                     configFile % QStringLiteral( "." )
                       % QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) )
                       % QStringLiteral( ".bad" ) );
      continue;
    }
    break;
  }

#ifdef Q_OS_MACOS
  cfg.preferences.enableTrayIcon = true;
  cfg.preferences.closeToTray    = true;
#endif


  cfg.resetState = gdcl.resetState;

  Logger::switchLoggingMethod( gdcl.logFile || cfg.preferences.enableApplicationLog );

  QApplication::setLayoutDirection( QLocale().textDirection() );

  {
    auto loadTranslation_qlocale = []( QTranslator & qtranslator,
                                       const QString & filename,
                                       const QString & prefix,
                                       const QString & directory ) -> bool {
      if ( qtranslator.load( QLocale(), filename, prefix, directory ) ) {
        qDebug() << "TS found: " << qtranslator.filePath();
        return true;
      }
      else {
        qDebug() << "TS failed to load: " << QLocale().uiLanguages() << filename << prefix << " from " << directory;
        return false;
      }
    };

    auto * md_ts = new QTranslator( &app );
    auto * qt_ts = new QTranslator( &app );

    bool loaded = loadTranslation_qlocale( *md_ts, QString(), QString(), Config::getLocDir() );

    if ( loaded ) {
      QCoreApplication::installTranslator( md_ts );
      qDebug() << "TS found: " << md_ts->filePath();

      if ( loadTranslation_qlocale( *qt_ts, "qt", "_", QLibraryInfo::path( QLibraryInfo::TranslationsPath ) )
           && qt_ts->language().startsWith( md_ts->language().first( 2 ) ) ) {
        QCoreApplication::installTranslator( qt_ts );
      }
    }
    else {
      qDebug() << "MicroDict translations not loaded.";
    }
  }

  app.setQuitOnLastWindowClosed( false );

  MainWindow m( cfg );

  QObject::connect(
    &app,
    &QGuiApplication::commitDataRequest,
    &m,
    [ &m ]( QSessionManager & ) {
      m.commitData();
    },
    Qt::DirectConnection );

  QObject::connect(
    &app,
    &QGuiApplication::saveStateRequest,
    &app,
    []( QSessionManager & mgr ) {
      mgr.setRestartHint( QSessionManager::RestartNever );
    },
    Qt::DirectConnection );

  QObject::connect( &app, &QtSingleApplication::messageReceived, &m, &MainWindow::messageFromAnotherInstanceReceived );

#ifdef Q_OS_MACOS
  auto macUrlHandler = std::make_unique< MacUrlHandler >( &m );
  QDesktopServices::setUrlHandler( "microdict", macUrlHandler.get(), "processURL" );
  QObject::connect( macUrlHandler.get(),
                    &MacUrlHandler::wordReceived,
                    &m,
                    &MainWindow::messageFromAnotherInstanceReceived );
#endif

  if ( gdcl.needSetGroup() ) {
    m.setGroupByName( gdcl.getGroupName() );
  }

  if ( gdcl.needTranslateWord() ) {
    m.showTranslation( gdcl.wordToTranslate() );
  }

#ifdef Q_OS_UNIX
  KSignalHandler::self()->watchSignal( SIGINT );
  KSignalHandler::self()->watchSignal( SIGTERM );
  QObject::connect( KSignalHandler::self(), &KSignalHandler::signalReceived, &m, &MainWindow::quitApp );
#endif
  int r = app.exec();
  Logger::closeLogFile();

  return r;
}
