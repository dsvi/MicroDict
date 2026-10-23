/* This file is (c) 2018 Igor Kushnir <igorkuo@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "audioplayerinterface.hh"
#include <memory>

class ExternalViewer;

class ExternalAudioPlayer: public AudioPlayerInterface
{
  Q_OBJECT

public:
  ExternalAudioPlayer();
  ~ExternalAudioPlayer();
  void setPlayerCommandLine( const QString & playerCommandLine_ );

  virtual QString play( const char * data, int size );
  virtual void stop();

private slots:
  void onViewerDestroyed( QObject * destroyedViewer );

private:
  QString startViewer();

  QString playerCommandLine;
  ExternalViewer * exitingViewer;

  struct QObjectDeleteLater
  {
    void operator()( QObject * p )
    {
      if ( p )
        p->deleteLater();
    }
  };
  std::unique_ptr< ExternalViewer, QObjectDeleteLater > viewer;
};
