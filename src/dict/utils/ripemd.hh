// Copyright (C) 2007 Michael Niedermayer <michaelni@gmx.at>
// Copyright (C) 2013 James Almer <jamrial@gmail.com>
// Copyright (C) 2015 Zhe Wang <0x1998@gmail.com>
//
// Based on the RIPEMD-128 implementation from libavutil
//
// This program is a free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// You can get a copy of GNU General Public License along this program
// But you can always get it from http://www.gnu.org/licenses/gpl.txt
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once

#include <QtGlobal>

class RIPEMD128
{
public:
  RIPEMD128();

  void update( const uchar * data, size_t len );

  void digest( uchar * digest );

private:
  quint64 count;
  uchar buffer[ 64 ];
  quint32 state[ 10 ];

  void transform( const uchar buffer[ 64 ] );
};
