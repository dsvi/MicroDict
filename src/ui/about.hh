/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include "ui_about.h"
#include "dict/dictionary.hh"
#include <QDialog>

class About: public QDialog
{
  Q_OBJECT

public:
  explicit About( QWidget * parent );

private:

  Ui::About ui;
};
