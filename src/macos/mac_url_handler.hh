#pragma once
#ifdef __APPLE__

  #include <QObject>
  #include <QUrl>

class MacUrlHandler: public QObject
{
  Q_OBJECT

public:
  MacUrlHandler( QObject * parent ):
    QObject( parent )
  {
  }
public slots:
  void processURL( const QUrl & url );
signals:
  void wordReceived( const QString & word );
};

#endif
