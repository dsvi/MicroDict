/* This file is (c) 2018 Igor Kushnir <igorkuo@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "externalaudioplayer.hh"
#include "externalviewer.hh"

ExternalAudioPlayer::ExternalAudioPlayer():
  exitingViewer( 0 )
{
}

ExternalAudioPlayer::~ExternalAudioPlayer()
{

  stopAndDestroySynchronously( viewer.release() );

  stopAndDestroySynchronously( exitingViewer );
}

void ExternalAudioPlayer::setPlayerCommandLine( const QString & playerCommandLine_ )
{
  playerCommandLine = playerCommandLine_;
}

QString ExternalAudioPlayer::play( const char * data, int size )
{
  stop();

  Q_ASSERT( !viewer && "viewer must be null at this point for exception safety." );
  try {
    viewer.reset( new ExternalViewer( data, size, "wav", playerCommandLine, this ) );
  }
  catch ( const ExternalViewer::Ex & e ) {
    return e.what();
  }

  if ( exitingViewer ) {
    return QString();
  }
  return startViewer();
}

void ExternalAudioPlayer::stop()
{
  if ( !exitingViewer && viewer && !viewer->stop() ) {
    exitingViewer = viewer.release();
  }
  else {
    viewer.reset();
  }
}

void ExternalAudioPlayer::onViewerDestroyed( QObject * destroyedViewer )
{
  if ( exitingViewer == destroyedViewer ) {
    exitingViewer = 0;
    if ( viewer ) {
      QString errorMessage = startViewer();
      if ( !errorMessage.isEmpty() ) {
        emit error( errorMessage );
      }
    }
  }
  else if ( viewer.get() == destroyedViewer ) {
    viewer.reset( nullptr );
  }
}

QString ExternalAudioPlayer::startViewer()
{
  Q_ASSERT( !exitingViewer && viewer );
  connect( viewer.get(), &QObject::destroyed, this, &ExternalAudioPlayer::onViewerDestroyed );
  try {
    viewer->start();
  }
  catch ( const ExternalViewer::Ex & e ) {
    viewer.reset();
    return e.what();
  }
  return QString();
}
