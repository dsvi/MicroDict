/* Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QFont>
#include <QString>

namespace LiteFonts {

enum Family {
  Standard,
  Serif,
  SansSerif,
  Monospace,
};

QFont defaultFamily( Family which );

QString styleSheet();

}
