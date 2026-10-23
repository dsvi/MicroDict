#pragma once
#include "audioplayerinterface.hh"
#include <QStringList>
#include <QStringList>

class InternalPlayerBackend
{
public:
  static bool anyAvailable();
  AudioPlayerInterface * getActualPlayer();
  static QStringList availableBackends();

  const QString & getName() const
  {
    return name;
  }

  void setName( const QString & name_ )
  {
    name = name_;
  }

  bool operator==( const InternalPlayerBackend & other ) const
  {
    return name == other.name;
  }

  bool operator!=( const InternalPlayerBackend & other ) const
  {
    return !operator==( other );
  }

private:
  QString name;
};
