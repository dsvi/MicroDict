#include "dictionary_icon_name.hh"
#include <QMutexLocker>


QString Icons::DictionaryIconName::getIconName( const QString & key, const QString & nameText )
{
  if ( key.isEmpty() ) {
    return {};
  }
  QMutexLocker _( &_mutex );

  if ( _dictionaryIconNames.contains( key ) ) {
    return _dictionaryIconNames.value( key );
  }
  QString source = nameText.isEmpty() ? key : nameText;
  if ( source.isEmpty() ) {
    return {};
  }
  QString name = source.at( 0 ).toUpper();
  int charCount = ++_iconDictionaryNames[ name ];

  QString resultName = name + QString::number( charCount );
  _dictionaryIconNames.insert( key, resultName );
  return resultName;
}
