#include "dictionarybar.hh"
#include "instances.hh"
#include "globalbroadcaster.hh"
#include <QAction>
#include <QApplication>
#include <QIconEngine>
#include <QMenu>
#include <QInputDialog>
#include <QPainter>
#include "metadata.hh"
#include "common/utils.hh"
#include <QContextMenuEvent>
#include <QStyle>


using std::vector;

namespace {

class DimmedIconEngine: public QIconEngine
{
public:
  explicit DimmedIconEngine( const QIcon & source_ ): source( source_ ) {}

  QIconEngine * clone() const override
  {
    return new DimmedIconEngine( source );
  }

  void paint( QPainter * painter, const QRect & rect, QIcon::Mode mode, QIcon::State state ) override
  {
    painter->drawPixmap( rect, pixmap( rect.size(), mode, state ) );
  }

  QPixmap pixmap( const QSize & size, QIcon::Mode, QIcon::State ) override
  {
    if ( size.isEmpty() ) {
      return {};
    }

    QPixmap srcPixmap = source.pixmap( size, QIcon::Normal, QIcon::Off );
    if ( srcPixmap.isNull() ) {
      return srcPixmap;
    }

    QImage srcImage = srcPixmap.toImage().convertToFormat( QImage::Format_ARGB32 );
    QImage grayImage( srcImage.size(), QImage::Format_ARGB32 );
    for ( int y = 0; y < srcImage.height(); ++y ) {
      const QRgb * srcLine = reinterpret_cast< const QRgb * >( srcImage.constScanLine( y ) );
      QRgb * dstLine       = reinterpret_cast< QRgb * >( grayImage.scanLine( y ) );
      for ( int x = 0; x < srcImage.width(); ++x ) {
        const QRgb rgba  = srcLine[ x ];
        const int gValue = qGray( rgba );
        dstLine[ x ]     = qRgba( gValue, gValue, gValue, qAlpha( rgba ) );
      }
    }

    grayImage = grayImage.convertToFormat( QImage::Format_ARGB32_Premultiplied );

    const qreal dpr = qMax< qreal >( srcPixmap.devicePixelRatio(), 1.0 );
    QImage canvas( size * dpr, QImage::Format_ARGB32_Premultiplied );
    canvas.setDevicePixelRatio( dpr );
    canvas.fill( Qt::transparent );

    QPainter p( &canvas );
    p.setRenderHint( QPainter::Antialiasing, true );
    p.setRenderHint( QPainter::SmoothPixmapTransform, true );

    const QSizeF srcLogical = QSizeF( srcImage.size() ) / dpr;
    const QRectF target( ( size.width() - srcLogical.width() ) / 2.0,
                         ( size.height() - srcLogical.height() ) / 2.0,
                         srcLogical.width(),
                         srcLogical.height() );
    p.drawImage( target, grayImage );

    const qreal lineWidth = qMax< qreal >( 2, size.height() / 10.0 );
    p.setPen( QPen( QColor( 235, 90, 70, 230 ), lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
    const qreal padding = lineWidth + 2;
    p.drawLine( QPointF( padding, size.height() - padding ), QPointF( size.width() - padding, padding ) );
    p.end();

    return QPixmap::fromImage( canvas );
  }

private:
  QIcon source;
};

QIcon makeDimmedIcon( const QIcon & source )
{
  if ( source.isNull() ) {
    return source;
  }
  return QIcon( new DimmedIconEngine( source ) );
}
}

DictionaryBar::DictionaryBar( QWidget * parent, const unsigned short & maxDictionaryRefsInContextMenu_ ):
  QToolBar( tr( "&Dictionary Bar" ), parent ),
  mutedDictionaries( nullptr ),
  maxDictionaryRefsInContextMenu( maxDictionaryRefsInContextMenu_ )
{
  normalIconSize = { this->iconSize().height(), this->iconSize().height() };

  setObjectName( "dictionaryBar" );

  maxDictionaryRefsAction =
    new QAction( QIcon( ":/icons/addtab.svg" ), tr( "Extended menu with all dictionaries..." ), this );

  connect( GlobalBroadcaster::instance(),
           &GlobalBroadcaster::mutedDictionariesChanged,
           this,
           &DictionaryBar::mutedDictionariesChanged );

  connect( this, &QToolBar::actionTriggered, this, &DictionaryBar::actionWasTriggered );
}

static QString elideDictName( const QString & name )
{

  const int maxSize = 33;

  if ( name.size() <= maxSize ) {
    return name;
  }

  const int pieceSize = maxSize / 2 - 1;

  return name.left( pieceSize ) + QChar( 0x2026 ) + name.right( pieceSize );
}

void DictionaryBar::setDictionaries( const vector< sptr< Dictionary::Class > > & dictionaries )
{
  setUpdatesEnabled( false );

  allDictionaries.clear();
  allDictionaries = dictionaries;

  clear();
  dictActions.clear();
  fullIcons.clear();
  dimmedIcons.clear();

  for ( const auto & dictionary : dictionaries ) {
    QIcon icon = dictionary->getIcon();

    QString dictName = QString::fromUtf8( dictionary->getName().c_str() );

    QAction * action = addAction( icon, elideDictName( dictName ) );

    action->setToolTip( dictName );

    QString id = QString::fromStdString( dictionary->getId() );

    action->setData( id );

    action->setCheckable( true );

    action->setChecked( mutedDictionaries ? !mutedDictionaries->contains( id ) : true );

    fullIcons.insert( action, icon );
    dimmedIcons.insert( action, makeDimmedIcon( icon ) );

    connect( action, &QAction::toggled, this, [ this, action ]( bool checked ) {
      applyActionIcon( action, checked );
    } );

    applyActionIcon( action, action->isChecked() );

    dictActions.append( action );
  }


  setUpdatesEnabled( true );
}

void DictionaryBar::updateToGroup( const Instances::Group * grp,
                                   QSet< QString > * allGroupMutedDictionaries,
                                   Config::Class & cfg )
{
  Q_ASSERT( grp != nullptr );
  if ( !this->toggleViewAction()->isChecked() ) {
    return;
  }

  setMutedDictionaries( nullptr );
  if ( grp->id == GroupId::AllGroupId ) {
    setMutedDictionaries( allGroupMutedDictionaries );
  }
  else {
    Config::Group * _grp = cfg.getGroup( grp->id );
    if ( _grp ) {
      setMutedDictionaries( &_grp->mutedDictionaries );
    }
  }
  setDictionaries( grp->dictionaries );
}

void DictionaryBar::setDictionaryIconSize( IconSize size )
{
  switch ( size ) {
    case IconSize::Small: {
      auto smallSize = QApplication::style()->pixelMetric( QStyle::PM_SmallIconSize );
      setIconSize( { smallSize, smallSize } );
      break;
    }

    case IconSize::Normal: {
      setIconSize( normalIconSize );
      break;
    }

    case IconSize::Large: {
      auto largeSize = QApplication::style()->pixelMetric( QStyle::PM_LargeIconSize );
      setIconSize( { largeSize + 10, largeSize + 10 } );
      break;
    }
  }
}

void DictionaryBar::contextMenuEvent( QContextMenuEvent * event )
{
  showContextMenu( event );
}


void DictionaryBar::showContextMenu( QContextMenuEvent * event, bool extended )
{
  QMenu menu( this );

  const QAction * restoreSelectionAction = nullptr;
  if ( tempSelectionCapturedMuted.has_value() ) {
    restoreSelectionAction = menu.addAction( tr( "Restore selection" ) );
  }

  const QAction * editAction            = nullptr;
  const QAction * infoAction            = nullptr;
  const QAction * headwordsAction       = nullptr;
  Dictionary::Class * pDict             = nullptr;
  const QAction * changeNameAction      = nullptr;
  const QAction * openDictFolderAction  = nullptr;
  const QAction * scheduleReindexAction = nullptr;

  editAction = menu.addAction( QIcon( ":/icons/bookcase.svg" ), tr( "Edit this group" ) );

  QString dictFilename;

  QAction * dictAction = actionAt( event->x(), event->y() );
  if ( dictAction ) {
    const QString id          = dictAction->data().toString();
    for ( const auto & dictionary : allDictionaries ) {
      if ( id.compare( dictionary->getId().c_str() ) == 0 ) {
        pDict = dictionary.get();
        break;
      }
    }

    if ( pDict ) {
      infoAction = menu.addAction( tr( "Dictionary info" ) );

      if ( pDict->isLocalDictionary() ) {
        changeNameAction = menu.addAction( tr( "Change display name" ) );

        Config::Class * cfg  = GlobalBroadcaster::instance()->getConfig();
        const QString dictId = pDict->getId().c_str();
        if ( cfg && cfg->dictionariesToReindex.contains( dictId ) ) {
          scheduleReindexAction = menu.addAction( tr( "Cancel reindex" ) );
        }
        else {
          scheduleReindexAction = menu.addAction( tr( "Schedule for reindex" ) );
        }

        if ( pDict->getWordCount() > 0 ) {
          headwordsAction = menu.addAction( tr( "Dictionary headwords" ) );
        }
        openDictFolderAction = menu.addAction( tr( "Open dictionary folder" ) );
      }
    }
  }

  if ( !dictActions.empty() ) {
    menu.addSeparator();
  }

  unsigned refsAdded = 0;

  for ( const auto & dictAction : std::as_const( dictActions ) ) {

    if ( refsAdded++ >= maxDictionaryRefsInContextMenu && !extended ) {
      menu.addSeparator();
      menu.addAction( maxDictionaryRefsAction );
      break;
    }

    QAction * action = menu.addAction( dictAction->icon(), dictAction->toolTip() );

    action->setCheckable( true );
    action->setChecked( dictAction->isChecked() );
    action->setData( QVariant::fromValue( (void *)dictAction ) );
    action->setIconVisibleInMenu( true );
  }

  connect( this, &DictionaryBar::closePopupMenu, &menu, &QWidget::close );

  const QAction * result = menu.exec( event->globalPos() );

  if ( result && result == infoAction ) {
    const QString id = dictAction->data().toString();
    emit showDictionaryInfo( id );
    return;
  }

  if ( result && result == changeNameAction ) {
    if ( !pDict || pDict->getContainingFolder().isEmpty() ) {
      return;
    }
    QInputDialog dialog( this );
    dialog.setWindowTitle( tr( "Change display name" ) );
    dialog.setLabelText( tr( "New display name:" ) );
    dialog.setTextValue( QString::fromUtf8( pDict->getName().c_str() ) );
    if ( auto * lineEdit = dialog.findChild< QLineEdit * >() ) {
      lineEdit->setClearButtonEnabled( true );
    }

    if ( dialog.exec() == QDialog::Accepted ) {
      QString newName = dialog.textValue();
      Metadata::saveDisplayName( Utils::Path::combine( pDict->getContainingFolder(), "metadata.toml" ).toStdString(),
                                 newName.toStdString() );
      pDict->setName( newName.toStdString() );
      QString effectiveName = QString::fromUtf8( pDict->getName().c_str() );
      dictAction->setText( elideDictName( effectiveName ) );
      dictAction->setToolTip( effectiveName );
    }
  }

  if ( result && result == scheduleReindexAction ) {
    const QString dictId = dictAction->data().toString();
    Config::Class * cfg  = GlobalBroadcaster::instance()->getConfig();

    if ( cfg && cfg->dictionariesToReindex.contains( dictId ) ) {
      cfg->dictionariesToReindex.remove( dictId );

      emit showStatusBarMessage( tr( "Cancel schedule reindex" ), 3000 );
    }
    else if ( cfg ) {
      cfg->dictionariesToReindex.insert( dictId );

      emit showStatusBarMessage(
        tr(
          "The dictionary has been scheduled for reindexing. The index will be rebuilt on the next application restart." ),
        3000 );
    }

    if ( cfg ) {
      cfg->dirty = true;
      Config::save( *cfg );
    }

    return;
  }

  if ( result && result == headwordsAction ) {
    const std::string id = dictAction->data().toString().toStdString();
    for ( const auto & dict : allDictionaries ) {
      if ( id == dict->getId() ) {
        emit showDictionaryHeadwords( dict.get() );
        break;
      }
    }
    return;
  }

  if ( result && result == openDictFolderAction ) {
    const QString id = dictAction->data().toString();
    emit openDictionaryFolder( id );
    return;
  }

  if ( result && result == maxDictionaryRefsAction ) {
    showContextMenu( event, true );
  }

  if ( result && result == restoreSelectionAction ) {
    *mutedDictionaries = tempSelectionCapturedMuted.value();
    tempSelectionCapturedMuted.reset();
    GlobalBroadcaster::instance()->mutedDictionariesChanged();
  }

  if ( result == editAction ) {
    emit editGroupRequested();
  }
  else if ( result && result->data().value< void * >() ) {
    ( (QAction *)( result->data().value< void * >() ) )->trigger();
  }

  event->accept();
}

void DictionaryBar::mutedDictionariesChanged()
{

  if ( !mutedDictionaries ) {
    return;
  }

  setUpdatesEnabled( false );

  for ( const auto & dictAction : std::as_const( dictActions ) ) {
    const bool isUnmuted = !mutedDictionaries->contains( dictAction->data().toString() );

    if ( isUnmuted != dictAction->isChecked() ) {
      dictAction->setChecked( isUnmuted );
    }
  }

  setUpdatesEnabled( true );
}

void DictionaryBar::actionWasTriggered( QAction * action )
{
  if ( !mutedDictionaries ) {
    return;
  }

  const QString id = action->data().toString();

  if ( id.isEmpty() ) {
    return;
  }

  if ( QApplication::keyboardModifiers().testFlag( Qt::ControlModifier ) ) {
    if ( ( dictActions.size() - mutedDictionaries->size() ) == 1 && !action->isChecked() ) {
      mutedDictionaries->clear();
    }
    else {
      selectSingleDict( id );
    }
  }
  else if ( QApplication::keyboardModifiers().testFlag( Qt::ShiftModifier ) ) {
    tempSelectionCapturedMuted.emplace( *mutedDictionaries );
    selectSingleDict( id );
  }
  else {
    if ( action->isChecked() ) {
      mutedDictionaries->remove( id );
    }
    else {
      mutedDictionaries->insert( id );
    }
  }
  GlobalBroadcaster::instance()->mutedDictionariesChanged();
}

void DictionaryBar::selectSingleDict( const QString & id )
{
  for ( auto & dictAction : std::as_const( dictActions ) ) {
    const QString dictId = dictAction->data().toString();
    if ( dictId == id ) {
      mutedDictionaries->remove( dictId );
    }
    else {
      mutedDictionaries->insert( dictId );
    }
  }
}

void DictionaryBar::dictsPaneClicked( const QString & id )
{
  if ( !isVisible() ) {
    return;
  }

  for ( const auto & dictAction : std::as_const( dictActions ) ) {
    const QString dictId = dictAction->data().toString();
    if ( dictId == id ) {
      dictAction->activate( QAction::Trigger );
      break;
    }
  }
}

void DictionaryBar::applyActionIcon( QAction * action, bool checked )
{
  if ( action == nullptr ) {
    return;
  }

  const auto it = fullIcons.constFind( action );
  if ( it == fullIcons.constEnd() ) {
    return;
  }

  action->setIcon( checked ? it.value() : dimmedIcons.value( action ) );
}
