#ifndef BRANCH_CONTAINER_H
#define BRANCH_CONTAINER_H

#include <QBrush>

#include "container.h"
#include "mapdesign.h"
#include "selectable-container.h"

class BranchItem;
class FlagRowContainer;
class HeadingContainer;
class LinkContainer;

class LinkObj;

class BranchContainer : public BranchContainerBase, public SelectableContainer {
  public:
    BranchContainer(
            QGraphicsScene *scene,
            BranchItem *bi = nullptr);
    virtual ~BranchContainer();
    virtual void init();

    BranchContainer* parentBranchContainer();

    void setBranchItem(BranchItem *);
    BranchItem *getBranchItem() const;

    virtual QString getName();

    void setOriginalOrientation();
    Orientation getOriginalOrientation();
    QPointF getOriginalParentPos();

    void setOriginalScenePos(); //! Saves current scene position for later restoring
    void updateVisibility();    // consider scroll hidden state for branchesCont and imagesCont

    void setScrolled(bool b);
    void setScrollOpacity(qreal a);
    qreal getScrollOpacity();
  private:
    qreal scrollOpacity;

  private:
    void updateBranchesContainerParent();

  public:
    void addToBranchesContainer(BranchContainer *bc);

  private:
    void updateImagesContainer();       //! Remove unused containers and add needed ones
    void createOuterContainer();        //! Used if only images have FloatingBounded layout
    void deleteOuterContainer();
    void updateTransformations();       //! Update rotation and scaling

  public:
    void updateChildrenStructure();     //! Depending on layouts of children, rearrange structure

  private:
    void updateImagesContainerParent(); //! Set parent depending on outerContainer

  public:
    void createImagesContainer();
    void addToImagesContainer(Container *c);

    HeadingContainer* getHeadingContainer();
    LinkContainer* getLinkContainer();
    LinkObj* getLink();
    void linkTo(BranchContainer *);

    /*! Get suggestion where new child could be positioned (scene coord) */
    QPointF getPositionHintNewChild(Container*);

    /*! Set hints where to place links between branches */
    void setUpLinkPosHint(const LinkObj::PosHint &);
    void setDownLinkPosHint(const LinkObj::PosHint &);

    /*! Get scene positions for links depending on frameType and orientation*/
    QPointF downLinkPos();
    QPointF downLinkPos(const Orientation &orientationChild);
    QPointF upLinkPos(const Orientation &orientationChild);

  private:
    LinkObj::PosHint upLinkPosHintInt;
    LinkObj::PosHint downLinkPosHintInt;

  public:
    /*! Update "upwards" links in LinkContainer */
    void updateUpLink();

    void setLayout(const Layout &l);

    bool imagesContainerAutoLayout;
    void setImagesContainerLayout(const Layout &l);
    Container::Layout imagesContainerLayout();
    bool hasFloatingImagesLayout(); //! Checks, if children images are or should be floating

    bool branchesContainerAutoLayout;
    void setBranchesContainerLayout(const Layout &l);
    Container::Layout branchesContainerLayout();
    bool hasFloatingBranchesLayout(); //! Checks, if children branches are or should be floating
    void setBranchesContainerHorizontalAlignment(const HorizontalAlignment &);
    void setBranchesContainerVerticalAlignment(const VerticalAlignment &);
    void setBranchesContainerBrush(const QBrush &b);

    void setBranchesContainerAndOrnamentsVertical(bool);

    QRectF headingRect();    //! Return rectangle of HeadingContainer in absolute coordinates
    QRectF ornamentsRect();  //! Return rectangle of ornamentsContainer in absolute coordinates

    void setColumnWidthAutoDesign(const bool &);
    bool columnWidthAutoDesign();

    void setColumnWidth(const int &);
    int columnWidth();

    void setRotationsAutoDesign(const bool &, const bool &update = true);
    bool rotationsAutoDesign();
    void setRotationHeading(const int &);
    int rotationHeading();
    int rotationHeadingInScene();

    void setRotationSubtree(const int &);
    int rotationSubtree();

    void setScaleAutoDesign(const bool &, const bool &update = true);
    bool scaleAutoDesign();
    void setScaleHeading(const qreal &);
    qreal scaleHeading();
    void setScaleSubtree(const qreal &);
    qreal scaleSubtree();

    void setColor(const QColor &);

  protected:
    bool columnWidthAutoDesignInt;
    bool rotationsAutoDesignInt;
    qreal rotationHeadingInt;
    qreal rotationSubtreeInt;
    bool scaleAutoDesignInt;
    qreal scaleHeadingInt;
    qreal scaleSubtreeInt;

  public:
    QUuid findFlagByPos(const QPointF &p);
    bool isInClickBox(const QPointF &p);
    QRectF getBBoxURLFlag();

    virtual void select();  // Overloads SelectableContainer::select

  private:
    bool autoDesignInnerFrame;
    bool autoDesignOuterFrame;

  public:
    // FrameContainer interfaces
    bool frameAutoDesign(const bool &useInnerFrame);
    void setFrameAutoDesign(const bool &userInnerFrame, const bool &);
    FrameContainer::FrameType frameType(const bool &useInnerFrame);
    QString frameTypeString(const bool &useInnerFrame);
    void setFrameType(const bool &useInnerFrame, const FrameContainer::FrameType &);
    void setFrameType(const bool &useInnerFrame, const QString &);

    int framePadding(const bool &useInnerFrame = true);
    void setFramePadding(const bool &useInnerFrame, const int &);
    int framePenWidth(const bool &useInnerFrame);
    void setFramePenWidth(const bool &useInnerFrame, const int &);
    QColor framePenColor(const bool &useInnerFrame);
    void setFramePenColor(const bool &useInnerFrame, const QColor &);
    QColor frameBrushColor(const bool &useInnerFrame);
    void setFrameBrushColor(const bool &useInnerFrame, const QColor&);

    QString saveFrame();

  public:
    /*! Update flags and heading */
    void updateVisuals();

    void reposition();

  protected:
    static qreal linkWidth;
    BranchItem *branchItem; //! Crossreference to "parent" BranchItem 

    // Uplink to parent
    LinkObj *upLink;

    // Save layout, alignment and brush of children containers 
    // even before containers are created on demand
    Layout imagesContainerLayoutInt;
    Layout branchesContainerLayoutInt;
    HorizontalAlignment branchesContainerHorizontalAlignmentInt;
    VerticalAlignment branchesContainerVerticalAlignmentInt;
    QBrush branchesContainerBrushInt;

    bool branchesContainerAndOrnamentsVerticalInt;    // Position branchesContainer e.g. for orgcharts
                                                // Affects layout if innerContainer in BC
    FrameContainer *innerFrame;         // Frame container around ornamentsContainer
    FrameContainer *outerFrame;         // Frame container around whole BranchContainer
    HeadingContainer *headingContainer; // Heading of this branch
    HeadingContainer *linkSpaceContainer; // space for downLinks
    LinkContainer *linkContainer;       // uplink to parent
    Container *listContainer;           // Container for bullet point lists, if used
    HeadingContainer *bulletPointContainer;  // if lists are used, contains bulletpoint
    Container *ornamentsContainer;      // Flags and heading
    Container *innerContainer;          // Ornaments (see above) and children branches
    Container *outerContainer;          // Used only with FloatingBounded images and vertical branches

    FlagRowContainer *standardFlagRowContainer;
    FlagRowContainer *systemFlagRowContainer;

  private:
    Orientation originalOrientation;            //! Save orientation before move for undo
    bool originalFloating;                      //! Save, if floating before linked temporary
    QPointF originalParentPos;                  // used in ME to determine orientation during move: scene coord of orig, parent

  public:   // FIXME-3 v_anim public only for experimenting  
    QPointF v_anim;                     // Animation vector. Added to current pos in each animation step
    QGraphicsLineItem v;
};

#endif
