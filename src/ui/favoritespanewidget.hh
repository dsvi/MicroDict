/* This file is (c) 2017 Abs62
 * Licensed under GPLv3 or later, see the LICENSE file */

#pragma once

#include <QWidget>
#include <QSize>
#include <QLabel>
#include <QHBoxLayout>
#include <QMenu>
#include <QDomNode>
#include <QList>
#include <QMimeData>
#include <QTreeView>

#include <config.hh>
#include "delegate.hh"

class FavoritesModel;

class TreeItem;
class FavoritesPaneWidget: public QWidget
{
  Q_OBJECT

public:
  FavoritesPaneWidget( QWidget * parent = 0 ):
    QWidget( parent ),
    itemSelectionChanged( false ),
    listItemDelegate( 0 ),
    m_favoritesModel( 0 ),
    timerId( 0 )
  {
  }

  virtual ~FavoritesPaneWidget();

  virtual QSize sizeHint() const
  {
    return QSize( 204, 204 );
  }

  void setUp( Config::Class * cfg, std::initializer_list< QAction * > actionsFromMainWindow );

  void addWordToActiveFav( const QString & word );
  bool removeWordFromActiveFav( const QString & word );

  bool trySetCurrentActiveFav( const QStringList & fullpath );
  void getDataInXml( QByteArray & dataStr );
  void getDataInPlainText( QString & dataStr );
  bool setDataFromXml( const QString & dataStr );
  bool setDataFromTxt( const QString & dataStr );
  void clearAllItems();

  void setFocusOnTree()
  {
    m_favoritesTree->setFocus();
  }

  void setSaveInterval( unsigned interval );

  bool isWordPresentInActiveFolder( const QString & headword );

  void saveData();

signals:
  void favoritesItemRequested( const QString & word, const QString & favoritesFolder );
  void activeFavChange();

protected:
  virtual void timerEvent( QTimerEvent * ev );

private slots:
  void emitFavoritesItemRequested( const QModelIndex & );
  void onSelectionChanged( const QItemSelection & selected, const QItemSelection & deselected );
  void onItemClicked( const QModelIndex & idx );
  void showCustomMenu( const QPoint & pos );
  void deleteSelectedItems();
  void folderActivation();
  void copySelectedItems();
  void addFolder();
public slots:
  void addRemoveWordInActiveFav( const QString & word );

private:
  virtual bool eventFilter( QObject *, QEvent * );
  Config::Class * m_cfg       = nullptr;
  QTreeView * m_favoritesTree = nullptr;

  QMenu * m_favoritesMenu             = nullptr;
  QAction * m_activeFolderForFav      = nullptr;
  QAction * m_deleteSelectedAction    = nullptr;
  QAction * m_separator               = nullptr;
  QAction * m_copySelectedToClipboard = nullptr;
  QAction * m_addFolder               = nullptr;
  QAction * m_clearAll                = nullptr;

  QWidget favoritesPaneTitleBar;
  QHBoxLayout favoritesPaneTitleBarLayout;
  QLabel favoritesLabel;

  bool itemSelectionChanged;

  WordListItemDelegate * listItemDelegate;
  FavoritesModel * m_favoritesModel;

  int timerId;
};


class TreeItem
{
public:
  enum Type {
    Word,
    Folder,
    Root
  };

  explicit TreeItem( const QVariant & data, TreeItem * parent = 0, Type type_ = Word );
  ~TreeItem();

  void appendChild( TreeItem * child );

  void insertChild( int row, TreeItem * item );

  void deleteChild( int row );

  TreeItem * child( int row ) const;
  QList< TreeItem * > & children()
  {
    return childItems;
  };
  int childCount() const;
  QVariant data() const;
  void setData( const QVariant & newData );
  int row() const;
  TreeItem * parent();

  Type type() const
  {
    return m_type;
  }

  Qt::ItemFlags flags() const;

  void setExpanded( bool expanded )
  {
    m_expanded = expanded;
  }

  bool isExpanded() const
  {
    return m_expanded;
  }

  QStringList fullPath() const;

  TreeItem * duplicateItem( TreeItem * newParent ) const;

  bool haveAncestor( TreeItem * item );

  bool haveSameItem( TreeItem * item, bool allowSelf );

  QStringList getTextFromAllChilds() const;
  void clearChildren();

private:
  QList< TreeItem * > childItems;
  QVariant itemData;
  TreeItem * parentItem;
  Type m_type;
  bool m_expanded;
};

class FavoritesModel: public QAbstractItemModel
{
  Q_OBJECT

public:
  explicit FavoritesModel( QString favoritesFilename, QObject * parent = 0 );
  ~FavoritesModel();
  void clearAllItems();

  QVariant data( const QModelIndex & index, int role ) const;
  Qt::ItemFlags flags( const QModelIndex & index ) const;
  QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const;
  QModelIndex index( int row, int column, const QModelIndex & parent = QModelIndex() ) const;
  QModelIndex parent( const QModelIndex & index ) const;
  int rowCount( const QModelIndex & parent = QModelIndex() ) const;
  int columnCount( const QModelIndex & parent = QModelIndex() ) const;
  bool removeRows( int row, int count, const QModelIndex & parent );
  bool setData( const QModelIndex & index, const QVariant & value, int role );

  Qt::DropActions supportedDropActions() const;
  QStringList mimeTypes() const;
  QMimeData * mimeData( const QModelIndexList & indexes ) const;
  bool dropMimeData( const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & par );

  void checkNodeForExpand( const TreeItem * item, const QModelIndex & parent );
  void checkAllNodesForExpand();

  QStringList getTextForIndexes( const QModelIndexList & idxList ) const;

  void removeItemsForIndexes( const QModelIndexList & idxList );

  QModelIndex addNewFolder( const QModelIndex & idx );

  bool addNewWordFullPath( const QString & headword );

  bool removeWordFullPath( const QString & headword );

  bool isWordPresentFullPath( const QString & headword );

  QString pathToItem( const QModelIndex & idx );

  TreeItem::Type itemType( const QModelIndex & idx )
  {
    return getItem( idx )->type();
  }

  void getDataInXml( QByteArray & dataStr );
  void getDataInPlainText( QString & dataStr );
  bool setDataFromXml( const QString & dataStr );
  bool setDataFromTxt( const QString & dataStr );

  void saveData();

  TreeItem * getItem( const QModelIndex & index ) const;
  TreeItem * getItemByFullPath( const QStringList & fullpath ) const;
  QModelIndex getModelIndexByFullPath( const QStringList & fullpath ) const;

  QStringList activeFolderFullPath;

public slots:
  void itemCollapsed( const QModelIndex & index );
  void itemExpanded( const QModelIndex & index );

signals:
  void expandItem( const QModelIndex & index );
  void itemDropped();

protected:
  void readData();
  void addFolder( TreeItem * parent, QDomNode & node );
  void storeFolder( TreeItem * folder, QDomNode & node );
  TreeItem * findFolderByName( TreeItem * parent, const QString & name, TreeItem::Type type );

  QModelIndex findItemInFolder( const QString & itemName, TreeItem::Type itemType, const QModelIndex & parentIdx );

  QModelIndex forceFolder( const QString & name, const QModelIndex & parentIdx );

  bool addHeadword( const QString & word, const QModelIndex & parentIdx );

  int level( const QModelIndex & idx );

private:
  QString m_favoritesFilename;
  TreeItem * rootItem;
  QDomDocument dom;
  bool dirty;
};

#define FAVORITES_MIME_TYPE "application/x-microdict-tree-items"

class FavoritesMimeData: public QMimeData
{
  Q_OBJECT

public:
  FavoritesMimeData():
    QMimeData()
  {
  }

  virtual QStringList formats() const
  {
    return QStringList( QString::fromLatin1( FAVORITES_MIME_TYPE ) );
  }

  virtual bool hasFormat( const QString & mimetype ) const
  {
    return mimetype.compare( QString::fromLatin1( FAVORITES_MIME_TYPE ) ) == 0;
  }

  void setIndexesList( const QModelIndexList & list )
  {
    indexes.clear();
    indexes = list;
  }

  const QModelIndexList & getIndexesList() const
  {
    return indexes;
  }

private:
  QModelIndexList indexes;
};
