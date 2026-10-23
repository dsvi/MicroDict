#pragma once

#include <QString>

namespace Version {

constexpr QLatin1StringView flags = QLatin1StringView(
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  "OPENCC "
#endif
);

constexpr QLatin1StringView compiler = QLatin1StringView(
#if defined( Q_CC_CLANG )
  "Clang " __clang_version__
#elif defined( Q_CC_GNU )
  "GCC " __VERSION__
#else
  "Unknown complier"
#endif
);
QString version();

QString everything();

QString getReleaseUrl();

}
