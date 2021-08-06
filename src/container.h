#ifndef CONTAINER_H
#define CONTAINER_H

#include <QGraphicsRectItem>

class MapObj;
class TreeItem;

class Container : public QGraphicsRectItem {
  public:
    enum ContentType {Undefined, MapObject, Containers};
    enum LayoutType {Horizontal, Vertical};
    enum HorizontalAlignment {Top, Middle, Bottom};
    enum VerticalAlignment  {Left, Center, Right};

    Container (QGraphicsItem *parent = NULL, TreeItem *ti = NULL);
    virtual ~Container();
    virtual void init();
    virtual void copy(Container *);

    void setContentType(const ContentType &ctype);
    ContentType getContentType();
    void setContent(MapObj* mapObj);

    void setLayoutType(const LayoutType &ltype);

    virtual void setTreeItem(TreeItem *);
    virtual TreeItem *getTreeItem() const;

    int subContainerCount();

    void addContainer(Container *c);

    void reposition();

    void setName(const QString &n);
    QString getName();

  private:
    ContentType contentType;
    MapObj *contentObj; //! Content object, e.g. HeadingObj or FlagRowObj
    TreeItem *treeItem; //! Crossreference to "parent" TreeItem 

    QString name;

    QList <Container*> childrenList;

    LayoutType layout;
};

#endif