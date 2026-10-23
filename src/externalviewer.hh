/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QObject>
#include <QTemporaryFile>
#include <QProcess>
#include "ex.hh"

class ExternalViewer: public QObject
{
  Q_OBJECT

  QTemporaryFile tempFile;
  QProcess viewer;
  QString viewerCmdLine;
  QString tempFileName;

public:

  DEF_EX( Ex, "External viewer exception", std::exception )
  DEF_EX( exCantCreateTempFile, "Couldn't create temporary file.", Ex )
  DEF_EX_STR( exCantRunViewer, "Couldn't run external viewer:", Ex )

  ExternalViewer(
    const char * data, int size, const QString & extension, const QString & viewerCmdLine, QObject * parent = 0 );

  void start();

  bool stop();
  void stopSynchronously();
};

void stopAndDestroySynchronously( ExternalViewer * viewer );
