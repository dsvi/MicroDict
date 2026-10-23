/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <string>
#include <format>

// clang-format off

#define DEF_EX( exName, exDescription, exParent )         \
  constexpr static char ExStr_## exName[] = exDescription; \
  using exName = defineEx< exParent, ExStr_## exName >;

#define DEF_EX_STR( exName, exDescription, exParent )     \
  constexpr static char ExStr_## exName[] = exDescription; \
  using exName = defineExStr< exParent, ExStr_## exName >;

// clang-format on

template< typename ParentEx, const char * description >
class defineEx: public ParentEx
{
public:
  virtual const char * what() const noexcept
  {
    return description;
  }
};

template< typename ParentEx, const char * description >
class defineExStr: public ParentEx
{
public:
  explicit defineExStr( const std::string & message_ ):
    message( std::format( "{} {}", description, message_ ) )
  {
  }
  virtual const char * what() const noexcept
  {
    return message.c_str();
  }

private:
  std::string message;
};
