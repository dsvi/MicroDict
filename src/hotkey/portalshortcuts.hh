/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QDBusObjectPath>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class PortalShortcuts: public QObject
{
  Q_OBJECT

public:
  explicit PortalShortcuts( QObject * parent = nullptr );
  ~PortalShortcuts() override;

  bool initialize();

  void add( const QString & id, const QKeySequence & sequence, const QString & description );

  void clear();

  static void releaseDesktopShortcuts( const QStringList & ids );

  QString assignedTrigger( const QString & id ) const;

  static QString toPortalTrigger( const QKeySequence & sequence );

signals:
  void activated( const QString & id );

private slots:
  void handleActivated( const QDBusObjectPath & session, const QString & id,
                        quint64 timestamp, const QVariantMap & options );

  void handleResponse( uint response, const QVariantMap & results );

private:
  struct Request {
    QString id;
    QKeySequence sequence;
    QString description;
  };

  enum class Stage {
    None,
    CreatingSession,
    Binding
  };

  void rebind();
  void createSession();
  void bindShortcuts();
  void closeSession();

  void watchRequest( const QString & token );
  void unwatchRequest();

  static QString requestPathFor( const QString & token );

  bool ready = false;
  bool commitScheduled = false;
  bool sessionPending = false;
  int tokenCounter = 0;

  Stage stage = Stage::None;
  QString pendingRequestPath;

  QString sessionHandle;
  QList< Request > requests;
  QHash< QString, QString > assigned;
};
