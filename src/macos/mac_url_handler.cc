#ifdef __APPLE__
  #include "mac_url_handler.hh"
  #include "utils.hh"
  #include <QDebug>

void MacUrlHandler::processURL( const QUrl & url )
{
  qDebug() << "External URL received: " << url;

  if ( url.scheme() == "microdict" ) {
    QString word = Utils::Url::extractWordFromUrl( url );

    if ( !word.isEmpty() ) {
      QString message = QStringLiteral( "action:translate" );

      QString encodedWord = QUrl::toPercentEncoding( word );
      message += "|word:" + encodedWord;
      emit wordReceived( message );
    }
  }
}
#endif
