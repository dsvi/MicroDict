/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QColor>
#include <QDBusVariant>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QVariantMap>
#include <Qt>

class QFileSystemWatcher;

bool isDarkPalette( const QPalette & p );

QString articleThemeCss( const QPalette & p, bool dark );

class SystemAppearance: public QObject
{
  Q_OBJECT

public:
  static SystemAppearance & instance();

  bool isDark() const;

  Qt::ColorScheme scheme() const;

  bool desktopPalette( QPalette & out ) const;

  bool publishesColors() const;

  QColor semanticForeground( const QString & key ) const;

  QString describe() const;

signals:
  void changed();

private:
  SystemAppearance();

  void refresh();

  void readAppearance();

  QString role( const QString & group, const QString & key ) const;

  void listenToPortal();
  void watchKdeGlobalsDir();

private slots:
  void onQtColorSchemeChanged( Qt::ColorScheme scheme );
  void onPortalSettingChanged( const QString & space, const QString & key, const QDBusVariant & value );
  void onKdeGlobalsDirChanged( const QString & path );

private:
  QVariant portalScheme;
  QVariantMap fileGroups;

  QColor portalAccent;

  Qt::ColorScheme current = Qt::ColorScheme::Unknown;
  QString decidedBy;
  QPalette lastPalette;
  bool haveDesktopColors = false;

  QFileSystemWatcher * watcher = nullptr;
};
