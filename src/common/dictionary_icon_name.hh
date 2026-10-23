#pragma once

#include <QMap>
#include <QString>
#include <QMutex>

namespace Icons {
class DictionaryIconName
{
  QMap< QString, int > _iconDictionaryNames;
  QMap< QString, QString > _dictionaryIconNames;

  QMutex _mutex;

public:
  QString getIconName( const QString & key, const QString & nameText );
};
}
