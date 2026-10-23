/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

class KeyboardState
{
public:

  enum Modifier {
    Alt   = 1,
    Ctrl  = 2,
    Shift = 4,
    Win   = 8,
  };

  bool static checkModifiersPressed( int mask );
};
