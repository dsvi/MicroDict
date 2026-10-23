#pragma once

#include <QToolBar>
class QMainWindow;
#include <QSize>
#include <QList>
#include <QHash>
#include <QIcon>
#include <QString>
#include <QTimer>
#include "dict/dictionary.hh"
#include "config.hh"

namespace Instances {
struct Group;
}

class DictionaryBar: public QToolBar
{
  Q_OBJECT

public:

  DictionaryBar( QWidget * parent, const unsigned short & maxDictionaryRefsInContextMenu_ );

  void setDictionaries( const std::vector< sptr< Dictionary::Class > > & );
  void setMutedDictionaries( QSet< QString > * mutedDictionaries_ )
  {
    mutedDictionaries = mutedDictionaries_;
  }
  const QSet< QString > * getMutedDictionaries() const
  {
    return mutedDictionaries;
  }

  enum class IconSize {
    Small,
    Normal,
    Large,
  };

  void setDictionaryIconSize( IconSize size );
  void updateToGroup( const Instances::Group * grp, QSet< QString > * allGroupMutedDictionaries, Config::Class & cfg );

signals:

  void editGroupRequested();

  void showDictionaryInfo( const QString & id );

  void showDictionaryHeadwords( Dictionary::Class * dict );

  void openDictionaryFolder( const QString & id );

  void closePopupMenu();

  void showStatusBarMessage( const QString & message, int timeout = 3000 );

private:

  QSet< QString > * mutedDictionaries;

  std::optional< QSet< QString > > tempSelectionCapturedMuted;

  void selectSingleDict( const QString & id );

  void applyActionIcon( QAction * action, bool checked );

  const unsigned short & maxDictionaryRefsInContextMenu;
  std::vector< sptr< Dictionary::Class > > allDictionaries;
  QList< QAction * > dictActions;
  QHash< QAction *, QIcon > fullIcons;
  QHash< QAction *, QIcon > dimmedIcons;
  QAction * maxDictionaryRefsAction;

  QSize normalIconSize;

protected:

  void contextMenuEvent( QContextMenuEvent * event );

private slots:

  void mutedDictionariesChanged();

  void actionWasTriggered( QAction * );

  void showContextMenu( QContextMenuEvent * event, bool extended = false );

public slots:

  void dictsPaneClicked( const QString & );
};
