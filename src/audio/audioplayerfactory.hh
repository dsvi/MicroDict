/* This file is (c) 2018 Igor Kushnir <igorkuo@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "audioplayerinterface.hh"
#include "internalplayerbackend.hh"

class ExternalAudioPlayer;

class AudioPlayerFactory
{
  AudioPlayerFactory( const AudioPlayerFactory & ) = delete;
  AudioPlayerFactory & operator=( const AudioPlayerFactory & ) = delete;

public:
  explicit AudioPlayerFactory( bool useInternalPlayer,
                               InternalPlayerBackend internalPlayerBackend,
                               QString audioPlaybackProgram );
  void setPreferences( bool new_useInternalPlayer,
                       const InternalPlayerBackend & new_internalPlayerBackend,
                       const QString & new_audioPlaybackProgram );
  const AudioPlayerPtr & player() const
  {
    return playerPtr;
  }

private:
  void reset();
  void setAudioPlaybackProgram( ExternalAudioPlayer & externalPlayer );

  bool useInternalPlayer;
  InternalPlayerBackend internalPlayerBackend;
  QString audioPlaybackProgram;
  AudioPlayerPtr playerPtr;
};
