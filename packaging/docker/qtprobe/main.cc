#include <QApplication>
#include <QAudioDevice>
#include <QDBusConnection>
#include <QBuffer>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QImageReader>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QPainter>
#include <QPaintEngine>
#include <QPainter>
#include <QTextDocument>
#include <QtSvg/QSvgRenderer>
#include <cstdio>

static int failures = 0;

static void check( bool ok, const char * what, const QByteArray & detail = {} )
{
  printf( "%-24s %s %s\n", what, ok ? "ok" : "FAIL", detail.constData() );
  if ( !ok )
    ++failures;
}

static int inked( const QImage & image )
{
  int n = 0;
  for ( int y = 0; y < image.height(); ++y )
    for ( int x = 0; x < image.width(); ++x )
      if ( qGray( image.pixel( x, y ) ) < 200 )
        ++n;
  return n;
}

int main( int argc, char ** argv )
{
  qputenv( "QT_QPA_PLATFORM", "offscreen" );
  QApplication app( argc, argv );

  check( QGuiApplication::platformName() == "offscreen", "platform plugin",
         QGuiApplication::platformName().toUtf8() );

  QStringList formats;
  for ( const auto & format : QImageReader::supportedImageFormats() )
    formats << QString::fromUtf8( format );
  formats.sort();
  for ( const char * needed : { "svg", "svgz", "jpeg", "gif", "ico" } )
    check( formats.contains( needed ), QString( "image format %1" ).arg( needed ).toUtf8().constData() );
  printf( "%-24s %s\n", "all formats", qPrintable( formats.join( ' ' ) ) );

  const auto families = QFontDatabase::families();
  check( families.size() > 0, "fontconfig fonts", QByteArray::number( families.size() ) );

  QImage text( 260, 60, QImage::Format_RGB32 );
  text.fill( Qt::white );
  {
    QPainter painter( &text );
    painter.setPen( Qt::black );
    painter.setFont( QFont( "DejaVu Sans", 20 ) );
    painter.drawText( text.rect(), Qt::AlignCenter, "Hello 中文 Ж" );
  }
  check( inked( text ) > 100, "drawText pixels", QByteArray::number( inked( text ) ) );

  QTextDocument document;
  document.setHtml( "<h2>apple</h2><p>A round fruit, <b>red</b> or green. 中文.</p>" );
  QImage page( 400, 200, QImage::Format_RGB32 );
  page.fill( Qt::white );
  {
    QPainter painter( &page );
    document.drawContents( &painter );
  }
  check( inked( page ) > 500, "QTextDocument pixels", QByteArray::number( inked( page ) ) );

  auto laidOutWidth = [ & ]( const char * family ) {
    QTextDocument styled;
    styled.setDefaultStyleSheet( QString( "p { font-family: '%1'; font-size: 24px; }" ).arg( family ) );
    styled.setHtml( "<p>Handglovesabcdefghijklmnopqrstuvwxyz</p>" );
    return styled.size().width();
  };
  const double serifWidth = laidOutWidth( "serif" );
  const double sansWidth  = laidOutWidth( "sans-serif" );
  const double monoWidth  = laidOutWidth( "monospace" );
  const double bogusWidth = laidOutWidth( "NoSuchFontAtAll" );
  check( serifWidth != sansWidth && serifWidth != bogusWidth, "generic serif resolves",
         QString( "serif %1 sans %2 bogus %3" ).arg( serifWidth ).arg( sansWidth ).arg( bogusWidth ).toUtf8() );
  check( monoWidth != sansWidth && monoWidth != bogusWidth, "generic monospace resolves",
         QString( "mono %1 sans %2" ).arg( monoWidth ).arg( sansWidth ).toUtf8() );
  check( QFontInfo( QFont( "monospace" ) ).fixedPitch(), "generic monospace pitch",
         QFontInfo( QFont( "monospace" ) ).family().toUtf8() );

  const QByteArray svg = "<svg width='64' height='64' viewBox='0 0 64 64'>"
                         "<circle cx='32' cy='32' r='28' fill='#123456'/></svg>";
  QSvgRenderer renderer( svg );
  QImage svgImage( 64, 64, QImage::Format_ARGB32 );
  svgImage.fill( Qt::transparent );
  {
    QPainter painter( &svgImage );
    renderer.render( &painter );
  }
  check( renderer.isValid() && inked( svgImage ) > 1000, "QSvgRenderer",
         QByteArray::number( inked( svgImage ) ) );

  QBuffer buffer( const_cast< QByteArray * >( &svg ) );
  buffer.open( QIODevice::ReadOnly );
  QImageReader reader( &buffer, "svg" );
  const QImage viaPlugin = reader.read();
  check( !viaPlugin.isNull() && inked( viaPlugin ) > 1000, "svg imageformats plugin",
         QString( "%1x%2" ).arg( viaPlugin.width() ).arg( viaPlugin.height() ).toUtf8() );

  QMediaPlayer player;
  check( player.error() == QMediaPlayer::Error::NoError, "QMediaPlayer created",
         player.errorString().toUtf8() );
  printf( "%-24s %d (needs a PulseAudio/PipeWire server)\n", "audio outputs",
          (int)QMediaDevices::audioOutputs().size() );

  printf( "\n--- D-Bus (how the hotkeys reach the desktop) ---\n" );
  check( QT_CONFIG( dbus ), "Qt built with D-Bus" );

  {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    printf( "  session bus: %s\n", bus.isConnected() ? "reachable" : "none here, and no crash" );
  }

  check( !QT_CONFIG( opengl ), "no OpenGL in QtGui" );
  check( !QT_CONFIG( egl ), "no EGL in QtGui" );
  check( !QT_CONFIG( vulkan ), "no Vulkan in QtGui" );

  QImage canvas( 16, 16, QImage::Format_ARGB32 );
  canvas.fill( Qt::transparent );
  {
    QPainter painter( &canvas );
    painter.setPen( Qt::black );
    painter.drawRect( 2, 2, 11, 11 );
    check( painter.paintEngine()->type() == QPaintEngine::Raster, "paint engine is raster" );
  }
  check( inked( canvas ) > 20, "raster engine painted", QByteArray::number( inked( canvas ) ) );

  printf( "\n%s\n", failures == 0 ? "QT PROBE OK" : "QT PROBE FAILED" );
  return failures == 0 ? 0 : 1;
}
