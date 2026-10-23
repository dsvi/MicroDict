/* This file is (c) 2018 Igor Kushnir <igorkuo@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#ifdef MAKE_QTMULTIMEDIA_PLAYER

  #include "audioplayerinterface.hh"
  #include <QAudioOutput>
  #include <QBuffer>
  #include <QMediaDevices>
  #include <QMediaPlayer>
  #include <memory>

class MultimediaAudioPlayer: public AudioPlayerInterface
{
  Q_OBJECT

public:
  MultimediaAudioPlayer();

  virtual QString play( const char * data, int size );
  virtual void stop();

private slots:
  void onMediaPlayerError();
  void audioOutputChange();


private:
  std::unique_ptr< QBuffer > audioBuffer;
  QMediaPlayer player;
  QAudioOutput audioOutput;
  QMediaDevices mediaDevices;
};

#endif
