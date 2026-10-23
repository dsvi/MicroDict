/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QProcess>
#include "dictionary.hh"
#include "config.hh"

namespace Programs {

using std::vector;
using std::string;

vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Programs & );

class RunInstance: public QObject
{
  Q_OBJECT
  QProcess process;

public:

  RunInstance();

  bool start( const Config::Program &, const QString & word, const QString & searchBarText, QString & error );

signals:
  void finished( QByteArray output, QString error );

signals:
  void processFinished();
private slots:

  void handleProcessFinished();
};

class ProgramDataRequest: public Dictionary::DataRequest
{
  Q_OBJECT
  Config::Program prg;
  RunInstance instance;

public:

  ProgramDataRequest( const QString & word, const Config::Program & );

  virtual void cancel();

private slots:

  void instanceFinished( QByteArray output, QString error );
};

class ProgramWordSearchRequest: public Dictionary::WordSearchRequest
{
  Q_OBJECT
  Config::Program prg;
  RunInstance instance;

public:

  ProgramWordSearchRequest( const QString & word, const Config::Program & );

  virtual void cancel();

private slots:

  void instanceFinished( QByteArray output, QString error );
};

}
