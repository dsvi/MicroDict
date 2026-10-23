/* This file is (c) 2018 Igor Kushnir <igorkuo@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <memory>
#include <QString>
#include <QObject>

class AudioPlayerInterface: public QObject
{
  Q_OBJECT

public:
  virtual QString play( const char * data, int size ) = 0;
  virtual void stop() = 0;

signals:
  void error( QString message );
};

using AudioPlayerPtr = std::unique_ptr< AudioPlayerInterface >;
