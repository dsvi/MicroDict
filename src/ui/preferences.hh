#pragma once

#include "config.hh"
#include "ui_preferences.h"

class Preferences: public QDialog
{
  Q_OBJECT

  Config::Class & cfg;
  QAction helpAction;

public:

  Preferences( QWidget * parent, Config::Class & cfg_ );
  void buildDisabledTypes( QString & disabledTypes, bool is_checked, QString name );
  ~Preferences() override = default;

  Config::Preferences getPreferences();

private:

  Ui::Preferences ui;

private slots:
  void on_enableClipboardHotkey_toggled( bool checked );

  void on_useExternalPlayer_toggled( bool enabled );


  void on_collapseBigArticles_toggled( bool checked );
  void on_limitInputPhraseLength_toggled( bool checked );
};
