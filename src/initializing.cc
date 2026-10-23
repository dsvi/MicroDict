/* This file is (c) 2008-2012 Konstantin Isakov
 * Licensed under GPLv3 or later, see the LICENSE file */

#include "initializing.hh"
#include <QCloseEvent>
#include <QFont>

Initializing::Initializing( QWidget * parent, bool showSplashWindow ):
  QDialog( parent )
{
  ui.setupUi( this );

  setWindowFlags( Qt::Dialog | Qt::FramelessWindowHint );
  setWindowIcon( QIcon( ":/icons/programicon.png" ) );

  QFont captionFont = ui.operation->font();
  captionFont.setCapitalization( QFont::AllUppercase );
  captionFont.setLetterSpacing( QFont::PercentageSpacing, 108 );
  ui.operation->setFont( captionFont );

  if ( showSplashWindow ) {
    ui.operation->setText( tr( "Please wait..." ) );
    ui.dictionary->setText( "" );
    show();
  }
}

void Initializing::indexing( const QString & dictionaryName )
{
  ui.operation->setText( tr( "Indexing..." ) );
  ui.dictionary->setText( dictionaryName );
}

void Initializing::loading( const QString & dictionaryName )
{
  ui.operation->setText( tr( "Loading..." ) );
  ui.dictionary->setText( dictionaryName );
}

void Initializing::closeEvent( QCloseEvent * ev )
{
  ev->ignore();
}

void Initializing::reject() {}
