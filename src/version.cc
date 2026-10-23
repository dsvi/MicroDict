#include "version.hh"

namespace Version {

QString version()
{
  return QStringLiteral( PROGRAM_VERSION );
}

QString everything()
{
  return QStringLiteral( R"(Version: %1
Qt %2 %3
%4 %5 %6 %7
Flags: %8)" )
    .arg( Version::version() )
    .arg( QLatin1StringView( qVersion() ), Version::compiler )
    .arg( QSysInfo::productType(), QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::buildAbi() )
    .arg( flags );
}

QString getReleaseUrl()
{
  return QStringLiteral( "https://github.com/dsvi/MicroDict/releases" );
}

}
