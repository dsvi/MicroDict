#include "internalplayerbackend.hh"
#include "multimediaaudioplayer.hh"

#ifdef MAKE_QTMULTIMEDIA_PLAYER
constexpr auto qtmultimedia = "Qt Multimedia";
#endif

bool InternalPlayerBackend::anyAvailable()
{
#ifdef MAKE_QTMULTIMEDIA_PLAYER
  return true;
#else
  static_assert( false, "No audio player backend. Please enable one." );
  return false;
#endif
}

QStringList InternalPlayerBackend::availableBackends()
{
  QStringList result;
#ifdef MAKE_QTMULTIMEDIA_PLAYER
  result.push_back( qtmultimedia );
#endif
  return result;
}

AudioPlayerInterface * InternalPlayerBackend::getActualPlayer()
{
  if ( name.isEmpty() || !availableBackends().contains( name ) ) {
    name = availableBackends().constFirst();
  }

#ifdef MAKE_QTMULTIMEDIA_PLAYER
  if ( name == qtmultimedia ) {
    return new MultimediaAudioPlayer();
  };
#endif
  qCritical( "Impossible situation. If ever reached, fix elsewhere. " );
  return nullptr;
}
