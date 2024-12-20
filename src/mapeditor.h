#ifndef MAPEDITOR_H
#define MAPEDITOR_H

#include <QGraphicsView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPropertyAnimation>

#include "tmp-parent-container.h"
#include "settings.h"
#include "vymmodel.h"

class XLink;
class XLinkItem;
class Winter;

/*! \brief Main widget in vym to display and edit a map */

class MapEditor : public QGraphicsView {
    Q_OBJECT

  public:
    enum EditorState {
        Neutral,
        EditingHeading,
        CreatingXLink,
        EditingXLink,
        MovingObject,
        MovingObjectTmpLinked,
        MovingObjectWithoutLinking,
        PanningView,
        PickingColor
    };

    MapEditor(VymModel *vm);
    ~MapEditor();
    VymModel *getModel();
    QGraphicsScene *getScene();

    // Animation of scrollbars
    Q_PROPERTY(QPointF scrollBarPos READ getScrollBarPos WRITE setScrollBarPos)

  protected:
    QPointF scrollBarPos;
    QPointF scrollBarPosTarget;
    QPropertyAnimation scrollBarPosAnimation;
    QTimer *panningTimer;
    QPointF vPan;      //! Direction of panning during moving of object

  private slots:
    void panView();

  public:
    void ensureAreaVisibleAnimated(
            const QRectF &area, 
            bool scaled = false, 
            bool rotated = false,
            qreal new_angle = 0) ;
    void ensureSelectionVisibleAnimated(bool scaled = false, bool rotated = false);
    void setScrollBarPosTarget(QRectF rect); //!  ensureVisible of rect
    QPointF getScrollBarPosTarget();
    void setScrollBarPos(const QPointF &p);
    QPointF getScrollBarPos();
    void animateScrollBars();

    // Animation of containers
  private:
    QTimer *animationTimer;
    bool animationUse;
    uint animationTicks;
    uint animationInterval;
    int timerId;                 // animation timer
    QList<Container*> animatedContainers;

  private slots:
    void animate(); //!< Called by timer to animate stuff
  public:
    void startAnimation(Container *c, const QPointF &v);
    void startAnimation(Container *c, const QPointF &start, const QPointF &dest);
    void stopContainerAnimation(Container *c);
    void stopContainerAnimations();
    void stopViewAnimations();

    // Animation of zoom
    Q_PROPERTY(qreal zoomFactorInt READ zoomFactor WRITE setZoomFactor)

  protected:
    qreal zoomDelta;
    qreal zoomFactorInt;
    qreal zoomFactorTargetInt;
    QPropertyAnimation zoomAnimation;

  public:
    void zoomIn();
    void zoomOut();
    void setZoomFactorTarget(const qreal &zf);
    qreal zoomFactorTarget();
    void setZoomFactor(const qreal &zf);
    qreal zoomFactor();

    // Animation of rotation
    Q_PROPERTY(qreal rotationInt READ rotation WRITE setRotation)

  protected:
    qreal rotationInt;
    qreal rotationTargetInt;
    QPropertyAnimation rotationAnimation;

    bool useTransformationOrigin;
    QPointF transformationOrigin;
    QPointF vp_center;   // Calculated before transformation to center on later

  public:
    void setRotationTarget(const qreal &a);
    qreal rotationTarget();
    void setRotation(const qreal &a);
    qreal rotation();

    // Animation of viewCenter
    Q_PROPERTY(QPointF viewCenter READ getViewCenter WRITE setViewCenter)

  protected:
    QPointF viewCenter;
    QPointF viewCenterTarget;

  public:
    void setViewCenterTarget(
        const QPointF &p, const qreal &zft, const qreal &at,
        const int duration = 2000,
        const QEasingCurve &easingCurve = QEasingCurve::OutQuint);
    void
    setViewCenterTarget(); //! Convenience function, center on selected item
    QPointF getViewCenterTarget();
    void setViewCenter(const QPointF &p);
    QPointF getViewCenter();
    QPropertyAnimation viewCenterAnimation;

    void updateMatrix(); //! Sets transformation matrix with current rotation
                         //! and zoom values
    void minimizeView();

    // xmas egg
  protected:
    Winter *winter;

  public:
    void print();                     //!< Print the map
    QRectF getTotalBBox();            //!< Bounding box of all items in map
    QImage getImage(QPointF &offset); //!< Get a pixmap of the map
    void setAntiAlias(bool);          //!< Set or unset antialiasing
    void setSmoothPixmap(bool);       //!< Set or unset smoothing of pixmaps
  public slots:
    void autoLayout();    //!< Auto layout of map by using collision detection
    void testFunction1(); //! just testing new stuff
    void testFunction2(); //! just testing new stuff

  public:
    TreeItem *findMapItem(
            QPointF p, 
            const QList <TreeItem*> &excludedItems = QList<TreeItem*>(),
            bool findNearCenter = false);  //! find item in map at position
                                           //! p. Ignore item exclude
    BranchItem *findMapBranchItem(
            QPointF p,
            const QList <TreeItem*> &excludedItems = QList<TreeItem*>(),
            bool findNearCenter = false); //! only return BranchItem
    void toggleWinter();

    enum RadarDirection {
        UpDirection,
        DownDirection,
        LeftDirection,
        RightDirection
    };

    bool isContainerCloserInDirection(Container *c1, Container *c2, const qreal &d_min, const QPoint &v, RadarDirection radarDir);
    TreeItem* getItemInDirection(TreeItem *ti, RadarDirection);
    TreeItem* getItemFromGeometry(TreeItem *ti, RadarDirection);
    TreeItem* getItemFromOrgChart(TreeItem *ti, RadarDirection);
    TreeItem* getItemFromClassicMap(TreeItem *ti, RadarDirection);

    TreeItem* getItemDirectAbove(TreeItem *ti);
    TreeItem* getItemDirectBelow( TreeItem *ti);

  private:
    // Toggle objects by moving the cursor up/down with shift modifier
    // (needs to consider the current direction of movement)
    enum ToggleDirection {toggleUndefined, toggleUp, toggleDown};
    ToggleDirection lastToggleDirection;

  public slots:
    void cursorUp();
    void cursorUpToggleSelection();
    void cursorDown();
    void cursorDownToggleSelection();
    void cursorLeft();
    void cursorRight();
    void cursorFirst();
    void cursorLast();
    void editHeading(BranchItem *selbi = nullptr);
    void editHeadingFinished();

  private:
    QLineEdit *lineEdit;

  private:
    void contextMenuEvent(QContextMenuEvent *e);
    void keyPressEvent(QKeyEvent *);
    void keyReleaseEvent(QKeyEvent *);
    void startPanningView(QMouseEvent *);
    void mousePressEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void moveObject(QMouseEvent *, const QPointF &p_event);    // Called from mouseMoveEvent
    void mouseReleaseEvent(QMouseEvent *);
    void mouseDoubleClickEvent(QMouseEvent *);
    void wheelEvent(QWheelEvent *);
    void focusOutEvent(QFocusEvent *);
    void resizeEvent(QResizeEvent *);

    void dragEnterEvent(QDragEnterEvent *);
    void dragMoveEvent(QDragMoveEvent *);
    void dragLeaveEvent(QDragLeaveEvent *);
    void dropEvent(QDropEvent *);

    void updateCursor();
  private:
    QGraphicsScene *mapScene;
    VymModel *model; //!< Vym Map, includding several mapCenters

    bool adjustCanvasRequested; // collect requests until end of user event
    BranchObj *editingBO;       // entering Text into BO

    QCursor HandOpenCursor;  // cursor while moving canvas view
    QCursor PickColorCursor; // cursor while picking color
    QCursor CopyCursor;      // cursor while picking color
    QCursor XLinkCursor;     // cursor while picking color

    // Various states of the MapEditor
  public:
    MapEditor::EditorState state();

  private:
    EditorState editorState;

    void setState(EditorState);
    bool objectMoved; // true if object was not clicked, but moved with mouse

    // Temporary used for linkx
    XLink *tmpXLink;

    // Temporary used for panning view
    QPoint panning_initialPointerPos;           // initial pos in pointer coordinates
    QPoint panning_initialScrollBarValues;      // inital values of scrollbars

    // Moving containers
    QList <TreeItem*> movingItems;              // selected items which are currently moved
    QPointF movingObj_initialScenePos;          // coord when button was pressed
    QPointF movingObj_initialContainerOffset;   // offset from above coordinates to object
    TmpParentContainer *tmpParentContainer;

    QPointF contextMenuPos; // position where context event was triggered

    bool printFrame;  // Print frame around map
    bool printFooter; // Print footer below map

    QPoint exportOffset; // set before export, used in save

    //////////// Selection related
  public:
    enum SelectionMode {
        AutoSelection,
        ClassicSelection,
        OrgChartSelection,
        GeometricSelection
    };

  SelectionMode currentSelectionMode(TreeItem *);

  private:
    SelectionMode selectionMode;

  signals:
    void selectionChanged(const QItemSelection &, const QItemSelection &);

  public slots:
    void updateData(const QModelIndex &);                 // update data
    void togglePresentationMode();
};
#endif
