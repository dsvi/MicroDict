/* This file is (c) 2015 Zhe Wang <0x1997@gmail.com>
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "chinese.hh"
#include <algorithm>
#include <stdexcept>
#include <QCoreApplication>
#include <opencc/opencc.h>
#include "folding.hh"
#include "transliteration.hh"
#include "text.hh"

namespace ChineseTranslit {

class CharacterConversionDictionary: public Transliteration::BaseTransliterationDictionary
{
  std::vector< std::vector< opencc_t > > chains;

public:

  CharacterConversionDictionary( const std::string & id,
                                 const std::string & name,
                                 QIcon icon,
                                 const std::vector< std::vector< QString > > & openccConfigChains );
  ~CharacterConversionDictionary();

  std::vector< QString > getAlternateWritings( const QString & ) noexcept override;
};

namespace {

opencc_t openConverter( const QString & openccConfig )
{
  const opencc_t invalidConverter = reinterpret_cast< opencc_t >( -1 );

  try {
    opencc_t converter = opencc_open( openccConfig.toLocal8Bit().constData() );
    if ( converter != invalidConverter ) {
      return converter;
    }
    qWarning( "CharacterConversionDictionary: failed to initialize OpenCC from config %s: %s",
              openccConfig.toLocal8Bit().constData(),
              opencc_error() );
  }
  catch ( std::exception & e ) {
    qWarning( "CharacterConversionDictionary: failed to initialize OpenCC from config %s: %s",
              openccConfig.toLocal8Bit().constData(),
              e.what() );
  }
  catch ( ... ) {
    qWarning( "CharacterConversionDictionary: failed to initialize OpenCC from config %s",
              openccConfig.toLocal8Bit().constData() );
  }

  return nullptr;
}

std::vector< opencc_t > openChain( const std::vector< QString > & chainConfigs )
{
  std::vector< opencc_t > chain;

  for ( size_t i = 0; i < chainConfigs.size(); ++i ) {
    const QString & openccConfig = chainConfigs[ i ];
    opencc_t converter           = openConverter( openccConfig );

    if ( converter != nullptr ) {
      chain.push_back( converter );
      continue;
    }

    if ( i + 1 != chainConfigs.size() ) {
      qWarning( "CharacterConversionDictionary: skipping unavailable config %s",
                openccConfig.toLocal8Bit().constData() );
      continue;
    }

    qWarning( "CharacterConversionDictionary: dropping conversion chain, config %s is unavailable",
              openccConfig.toLocal8Bit().constData() );
    for ( opencc_t opened : chain ) {
      opencc_close( opened );
    }
    chain.clear();
    break;
  }

  return chain;
}

}

CharacterConversionDictionary::CharacterConversionDictionary(
  const std::string & id,
  const std::string & name_,
  QIcon icon_,
  const std::vector< std::vector< QString > > & openccConfigChains ):
  Transliteration::BaseTransliterationDictionary( id, name_, icon_, false )
{
  for ( const std::vector< QString > & chainConfigs : openccConfigChains ) {
    std::vector< opencc_t > chain = openChain( chainConfigs );
    if ( !chain.empty() ) {
      chains.push_back( std::move( chain ) );
    }
  }
}

CharacterConversionDictionary::~CharacterConversionDictionary()
{
  for ( const std::vector< opencc_t > & chain : chains ) {
    for ( opencc_t converter : chain ) {
      opencc_close( converter );
    }
  }
}

std::vector< QString > CharacterConversionDictionary::getAlternateWritings( const QString & str ) noexcept
{
  std::vector< QString > results;

  if ( chains.empty() ) {
    return results;
  }

  QString folded = Folding::applySimpleCaseOnly( str );

  for ( const std::vector< opencc_t > & chain : chains ) {
    std::string output = Text::toUtf8( folded );

    try {
      for ( opencc_t converter : chain ) {
        char * tmp = opencc_convert_utf8( converter, output.c_str(), output.length() );
        if ( tmp == nullptr ) {
          qWarning( "OpenCC: conversion failed %s", opencc_error() );
          continue;
        }
        output.assign( tmp );
        opencc_convert_utf8_free( tmp );
      }
    }
    catch ( std::exception & ex ) {
      qWarning( "OpenCC: conversion failed %s", ex.what() );
    }
    catch ( ... ) {
      qWarning( "OpenCC: conversion failed with an unknown error" );
    }

    QString result = Text::toQString( output );

    if ( !result.isEmpty() && result != folded
         && std::find( results.begin(), results.end(), result ) == results.end() ) {
      results.push_back( result );
    }
  }

  return results;
}

std::vector< sptr< Dictionary::Class > > makeDictionaries( const Config::Chinese & cfg )

{
  std::vector< sptr< Dictionary::Class > > result;

  QString configDir = Config::getOpenCCDir();
  if ( !configDir.isEmpty() ) {
    configDir += "/";
  }

  if ( cfg.enable ) {
    if ( cfg.enableSCToTWConversion ) {
      result.push_back( std::make_shared< CharacterConversionDictionary >(
        "bf1c33a59cbacea8f39b5b5475787cfd",
        QCoreApplication::translate( "ChineseConversion",
                                     "Simplified to traditional Chinese (Taiwan variant) conversion" )
          .toUtf8()
          .data(),
        QIcon( ":/icons/tc.svg" ),
        std::vector< std::vector< QString > >{ { configDir + "s2tw.json" },
                                               { configDir + "jp2t.json", configDir + "s2tw.json" } } ) );
    }

    if ( cfg.enableSCToHKConversion ) {
      result.push_back( std::make_shared< CharacterConversionDictionary >(
        "9e0681fb9e1c0b6c90e6fb46111d96b5",
        QCoreApplication::translate( "ChineseConversion",
                                     "Simplified to traditional Chinese (Hong Kong variant) conversion" )
          .toUtf8()
          .data(),
        QIcon( ":/icons/hk.svg" ),
        std::vector< std::vector< QString > >{ { configDir + "s2hk.json" },
                                               { configDir + "jp2t.json", configDir + "s2hk.json" } } ) );
    }

    if ( cfg.enableTCToSCConversion ) {
      result.push_back( std::make_shared< CharacterConversionDictionary >(
        "0db536ce0bdc52ea30d11a82c5db4a27",
        QCoreApplication::translate( "ChineseConversion", "Traditional to simplified Chinese conversion" )
          .toUtf8()
          .data(),
        QIcon( ":/icons/sc.svg" ),
        std::vector< std::vector< QString > >{ { configDir + "t2s.json" },
                                               { configDir + "jp2t.json", configDir + "t2s.json" } } ) );
    }

    if ( cfg.enableJapaneseConversion ) {
      result.push_back( std::make_shared< CharacterConversionDictionary >(
        "7d8e9f1a2b3c4d5e6f0a1b2c3d4e5f60",
        QCoreApplication::translate( "ChineseConversion", "Chinese to Japanese Shinjitai conversion" ).toUtf8().data(),
        QIcon( ":/icons/jpc.svg" ),
        std::vector< std::vector< QString > >{ { configDir + "s2t.json", configDir + "t2jp.json" } } ) );
    }
  }

  return result;
}

}
