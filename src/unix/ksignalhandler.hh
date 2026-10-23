/*
    SPDX-FileCopyrightText: 2021 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: GPL-3.0-or-later

    Copied from KDE's KCoreAddons with minor modifications
*/
#pragma once
#include <QtGlobal>
#ifdef Q_OS_UNIX
  #include <QObject>
  #include <signal.h>
  #include <memory>

class KSignalHandlerPrivate;

class KSignalHandler: public QObject
{
  Q_OBJECT

public:
  ~KSignalHandler() override;

  void watchSignal( int signal );

  static KSignalHandler * self();

Q_SIGNALS:
  void signalReceived( int signal );

private:
  KSignalHandler();

  std::unique_ptr< KSignalHandlerPrivate > d;
};

#endif
