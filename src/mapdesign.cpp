#include "mapdesign.h"

#include <QApplication>
#include <QDebug>

#include "branch-container.h"
#include "branchitem.h"
#include "file.h"
#include "heading-container.h"
#include "misc.h"

extern bool usingDarkTheme;

template <typename T> ConfigList <T> & ConfigList<T>::operator<<(const T &other) {
    qlist << other;
    return *this;
}

template <typename T> T & ConfigList<T>::operator[](int i) {
    if (i >= 0 && i < qlist.count())
        return qlist.at(i);
}

template <typename T> T ConfigList<T>::tryAt(int i) {
    if (i >= qlist.count())
        return qlist.last();
    else
        return qlist.at(i);
}

template <typename T> void ConfigList<T>::setAt(int i, const T &val) {
    if (i <= 0) {
        // Set val for first level and clear all others
        qlist.clear();
        qlist << val;
    } else if (i >= qlist.count()) {
        // Fill all values between last and new one with last one
        for (int j = qlist.count(); j < i; j++)
            qlist << qlist.last();
        qlist << val;
    } else
        qlist.replace(i, val);

}

template <typename T> int ConfigList<T>::count() {
    return qlist.count();
}

template <typename T> void ConfigList<T>::clear() {
    qlist.clear();
}

template <typename T> QString ConfigList<T>::save(
        const QString &attrName,
        QString (&f)(int)) {
    XMLObj xml;
    QString s;
    for (int i = 0; i < qlist.size(); ++i) 
        s += xml.singleElement("md",
                xml.attribute( "key", attrName) + 
                xml.attribute( "d", QString("%1").arg(i)) +
                xml.attribute( "val", f(qlist.at(i)))
                );
    return s;
}

template <typename T> QString ConfigList<T>::saveBool(
        const QString &attrName) {
    XMLObj xml;
    QString s;
    for (int i = 0; i < qlist.size(); ++i) 
        s += xml.singleElement("md",
                xml.attribute( "key", attrName) + 
                xml.attribute( "d", QString("%1").arg(i)) +
                xml.attribute( "val", toS(qlist.at(i)))
                );
    return s;
}

template <typename T> QString ConfigList<T>::saveColor(
        const QString &attrName) {
    XMLObj xml;
    QString s;
    for (int i = 0; i < qlist.size(); ++i) 
        s += xml.singleElement("md",
                xml.attribute( "key", attrName) + 
                xml.attribute( "d", QString("%1").arg(i)) +
                xml.attribute( "val", qlist.at(i).name(QColor::HexArgb))
                );
    return s;
}

template <typename T> QString ConfigList<T>::saveInt(
        const QString &attrName) {
    XMLObj xml;
    QString s;
    for (int i = 0; i < qlist.size(); ++i) 
        s += xml.singleElement("md",
                xml.attribute( "key", attrName) + 
                xml.attribute( "d", QString("%1").arg(i)) +
                xml.attribute( "val", QString("%1").arg(qlist.at(i)))
                );
    return s;
}

/*
FIXME-3 currently not used template <typename T> void ConfigList<T>::setDefault(const T &d) {
    defaultValue = d;
}
*/

/////////////////////////////////////////////////////////////////
// MapDesign
/////////////////////////////////////////////////////////////////

MapDesign::MapDesign()  // FIXME-1 add options to update styles when relinking (Triggers, Actors)
                        // Triggers: Never, DepthChanged, Always
                        // Actors: Inner/Outer-Frames,Fonts,HeadingColor,Rotation Heading/Subtree, ...
{
    // qDebug() << "Constr. MapDesign";
    init();
}

void MapDesign::init()
{
    //
    // Design elements not depending on depth, incl. legacy map attributes
    //

    // Background
    usesBackgroundImage = false;

    innerFramePenWidths << 2;
    outerFramePenWidths << 2;

    // Colors
    if (usingDarkTheme) {
        QPalette palette = qApp->palette();
        backgroundColorInt = QColor(palette.color(QPalette::Base));

        innerFramePenColors << QColor(Qt::white);
        innerFrameBrushColors << QColor(85, 85, 127);

        innerFramePenColors << QColor(Qt::blue);
        innerFrameBrushColors << QColor(25, 25, 127);

        outerFramePenColors << QColor(Qt::green);
        outerFramePenColors << QColor(Qt::red);
        outerFramePenColors << QColor(Qt::green);
        outerFramePenColors << QColor(Qt::red);
        outerFrameBrushColors << QColor(85, 85, 127);
        outerFrameBrushColors << QColor(25, 25, 117);
        outerFrameBrushColors << QColor(85, 85, 127);
        outerFrameBrushColors << QColor(25, 25, 117);
    } else {
        backgroundColorInt = QColor(Qt::white);
        innerFramePenColors << QColor(Qt::black);
        innerFrameBrushColors << QColor(Qt::white);
        outerFramePenColors << QColor(Qt::green);
        outerFrameBrushColors << QColor(85, 85, 127);
    }

    // Font
    fontInt.setPointSizeF(16);

    // Dimensions
    headingColumnWidths << 42;

    // Selection
    selectionPenInt = QPen(QColor(255,255,0,255), 3);
    selectionBrushInt = QBrush(QColor(255,255,0,120));

    // Should links of branches use a default color or the color of heading?
    linkColorHintInt = LinkObj::DefaultColor;
    defaultLinkCol = Qt::blue;

    // XLinks
    defXLinkPenInt.setWidth(1);
    defXLinkPenInt.setColor(QColor(50, 50, 255));
    defXLinkPenInt.setStyle(Qt::DashLine);
    defXLinkStyleBeginInt = "HeadFull";
    defXLinkStyleEndInt = "HeadFull";

    //
    // Design elements depending on depth
    //

    // Layout of children branches 
    branchesContainerLayouts << Container::FloatingBounded;
    branchesContainerLayouts << Container::Vertical;

    // Layout of children images 
    imagesContainerLayouts << Container::GridColumns;
    imagesPositionsInt << ImagesPositionHint::VerticalAboveBranches;

    // Layout children branches above/below heading or side by side?
    branchesContainerAndOrnamentsVerticalInt << false;
    branchesContainerVerticalAlignmentsInt << Container::VertAlignedCentered;

    // Heading colors
    headingColorHints << MapDesign::SpecificColor;         // Specific for MapCenter
    headingColorHints << MapDesign::InheritedColor;        // Use color of parent
    headingColorUpdateWhenRelinking << false;

    headingColors << QColor(Qt::white);
    headingColors << QColor(Qt::green);

    // Frames
    innerFrameTypes << FrameContainer::RoundedRectangle;
    innerFrameTypes << FrameContainer::Rectangle;
    innerFrameTypes << FrameContainer::NoFrame;
    innerFramePenWidths << 2;
    innerFrameUpdateWhenRelinking << true;   // MapCenters inner frame
    innerFrameUpdateWhenRelinking << true;   // Mainbranches inner frame
    innerFrameUpdateWhenRelinking << false;

    outerFrameTypes << FrameContainer::NoFrame;
    /*
    outerFrameTypes << FrameContainer::Rectangle;
    outerFrameTypes << FrameContainer::RoundedRectangle;
    outerFrameTypes << FrameContainer::Rectangle;
    outerFrameTypes << FrameContainer::Rectangle;
    outerFrameTypes << FrameContainer::NoFrame;
    */
    outerFrameUpdateWhenRelinking << true;   // MapCenters outer frame
    outerFrameUpdateWhenRelinking << false;

    outerFrameTypes << FrameContainer::NoFrame;
    /*
    outerFramePenColors;
    outerFrameBrushColors;
    outerFramePenWidths;
    */

    // Transformations
    rotationHeadingInt << 0;
    //rotationHeadingInt << 20;
    //rotationHeadingInt << 20;

    rotationSubtreeInt << 0;

    scaleHeadingInt << 1.3;
    scaleHeadingInt << 1.0;
    scaleHeadingInt << 0.8;

    scaleSubtreeInt << 1.0;

    // Links
    linkStyles << LinkObj::NoLink;
    linkStyles << LinkObj::PolyParabel;
    linkStyles << LinkObj::Parabel;
}

void MapDesign::setName(const QString &s)
{
    name = s;
}

QString MapDesign::getName()
{
    return name;
}

QString MapDesign::updateModeString(const UpdateMode &mode)
{
    switch (mode) {
        case Undefined:
            return "Update mode Undefined!";
        case LoadingMap:
            return "LoadingMap";
        case CreatedByUser:
            return "CreatedByUser";
        case RelinkedByUser:
            return "RelinkedByUser";
        case LayoutChanged:
            return "LayoutChanged";
        case LinkStyleChanged:
            return "LinkStyleChanged";
        case StyleChanged:
            return "StyleChanged";
        case AutoDesign:
            return "AutoDesign";
        default:
            return QString("Unknown update mode: %1").arg(mode);
    }
}

bool MapDesign::setElement(const QString &key, const QString &val, const QString &d)    // FIXME-3 should also return undo/redo commands to VymModel, if called from there?
{
    //qDebug() << "MD::setElement k=" << key << " v=" << val << " d=" << d;
    
    int depth;
    bool ok;

    depth = d.toInt(&ok);
    if (!ok) {
        qWarning() << "MD::setElement k=" << key << " v=" << val << " Failed to parse d=" << d;
        return false;
    }

    if (key == "branchesLayout") {
        branchesContainerLayouts.setAt(depth, Container::layoutFromString(val));
        return true;
    } else if (key == "childrenAndHeadingVertical") {
        branchesContainerAndOrnamentsVerticalInt.setAt(depth, QVariant(val).toBool());
        return true;
    } else if (key == "childrenVertAlignment") {
        branchesContainerVerticalAlignmentsInt.setAt(depth, Container::verticalAlignmentFromString(val));
        return true;
    } else if (key == "imagesLayout") {
        imagesContainerLayouts.setAt(depth, Container::layoutFromString(val));
        return true;
    } else if (key == "innerFrameType") {
        innerFrameTypes.setAt(depth, FrameContainer::frameTypeFromString(val));
        return true;
    } else if (key == "innerFrameBrushColor") {
        innerFrameBrushColors.setAt(depth, QColor(val));
        return true;
    } else if (key == "innerFramePenColor") {
        innerFramePenColors.setAt(depth, QColor(val));
        return true;
    } else if (key == "innerFramePenWidth") {
        innerFramePenWidths.setAt(depth, val.toInt());
        return true;
    } else if (key == "innerFrameUpdateWhenRelinking") {
        innerFrameUpdateWhenRelinking.setAt(depth, QVariant(val).toBool());
        return true;
    } else if (key == "outerFrameType") {
        outerFrameTypes.setAt(depth, FrameContainer::frameTypeFromString(val));
        return true;
    } else if (key == "outerFrameBrushColor") {
        outerFrameBrushColors.setAt(depth, QColor(val));
        return true;
    } else if (key == "outerFramePenColor") {
        outerFramePenColors.setAt(depth, QColor(val));
        return true;
    } else if (key == "outerFramePenWidth") {
        outerFramePenWidths.setAt(depth, val.toInt());
        return true;
    } else if (key == "outerFrameUpdateWhenRelinking") {
        outerFrameUpdateWhenRelinking.setAt(depth, QVariant(val).toBool());
        return true;
    } else if (key == "linkStyle") {
        auto style = LinkObj::styleFromString(val);
        linkStyles.setAt(depth, style);
        return true;
    }

    return false;
}

Container::Layout MapDesign::branchesContainerLayout(int depth)
{
    return branchesContainerLayouts.tryAt(depth);
}

Container::VerticalAlignment MapDesign::branchesContainerVerticalAlignment(int depth)
{
    return branchesContainerVerticalAlignmentsInt.tryAt(depth);
}

Container::Layout MapDesign::imagesContainerLayout(int depth)
{
    return imagesContainerLayouts.tryAt(depth);
}

bool MapDesign::branchesContainerAndOrnamentsVertical(int depth)
{
    return branchesContainerAndOrnamentsVerticalInt.tryAt(depth);
}

MapDesign::ImagesPositionHint MapDesign::imagesPosition(int depth)
{
    return imagesPositionsInt.tryAt(depth);
}

LinkObj::ColorHint MapDesign::linkColorHint()
{
    return linkColorHintInt;
}

void MapDesign::setLinkColorHint(const LinkObj::ColorHint &lch)
{
    linkColorHintInt = lch;
}

QColor MapDesign::defaultLinkColor()
{
    return defaultLinkCol;
}

void MapDesign::setDefaultLinkColor(const QColor &col)
{
    defaultLinkCol = col;
}

LinkObj::Style MapDesign::linkStyle(int depth)
{
    return linkStyles.tryAt(depth);
}

void MapDesign::setLinkStyle(LinkObj::Style style, int depth)
{
    linkStyles.setAt(depth, style);
}

qreal MapDesign::linkWidth()
{
    return 20;
}

void MapDesign::setDefXLinkPen(const QPen &p)
{
    defXLinkPenInt = p;
}

QPen MapDesign::defXLinkPen()
{
    return defXLinkPenInt;
}

void MapDesign::setDefXLinkStyleBegin(const QString &s)
{
    defXLinkStyleBeginInt = s;
}

QString MapDesign::defXLinkStyleBegin()
{
    return defXLinkStyleBeginInt;
}

void MapDesign::setDefXLinkStyleEnd(const QString &s)
{
    defXLinkStyleEndInt = s;
}

QString MapDesign::defXLinkStyleEnd()
{
    return defXLinkStyleEndInt;
}


void MapDesign::setBackgroundColor(const QColor &col)
{
    backgroundColorInt = col;
}

QColor MapDesign::backgroundColor()
{
    return backgroundColorInt;
}

bool MapDesign::setBackgroundImage(const QString &fileName)
{
    backgroundImage.load(fileName);
    if (backgroundImage.isNull()) {
        usesBackgroundImage = false;
        return false;
    }

    usesBackgroundImage = true;
    backgroundImageNameInt = basename(fileName);
    backgroundImageBrushInt.setTextureImage(backgroundImage);

    return true;
}

void MapDesign::setBackgroundImageName(const QString &n)
{
    backgroundImageNameInt = n;
}

void MapDesign::unsetBackgroundImage()
{
    usesBackgroundImage = false;
    backgroundImageNameInt.clear();
}

bool MapDesign::hasBackgroundImage()
{
    return usesBackgroundImage;
}

QString MapDesign::backgroundImageName()
{
    return backgroundImageNameInt;
}

QBrush MapDesign::backgroundImageBrush()
{
    return backgroundImageBrushInt;
}


QFont MapDesign::font()
{
    return fontInt;
}

void MapDesign::setFont(const QFont &f)
{
    fontInt = f;
}

int MapDesign::headingColumnWidth(const int &depth)
{
    return headingColumnWidths.tryAt(depth);
}

QColor MapDesign::headingColor(
        const MapDesign::UpdateMode &updateMode,
        BranchItem *branchItem,
        bool &updateRequired)
{
    updateRequired = false;
    QColor col;

    if (!branchItem) return col;

    int depth = branchItem->depth();
    if (updateMode == CreatedByUser || 
            (updateMode == RelinkedByUser && headingColorUpdateWhenRelinking.tryAt(depth)))
    {
        HeadingColorHint colHint = headingColorHints.tryAt(depth);

        switch (colHint) {
            case InheritedColor: {
                BranchItem *pbi = branchItem->parentBranch();
                if (pbi) {
                    col = pbi->headingColor();
                    //qDebug() << " - Inherited color: " << col.name();
                    break;
                }
                // If there is no parent branch, mapCenter should 
                // have a specific color, thus continue
            }
            case SpecificColor: {
                col = headingColors.tryAt(depth);
                //qDebug() << " - SpecificColor:" << col.name();
                break;
            }
            case UnchangedColor:
                //qDebug() << " - UnchangedColor";
                break;
            default:
                qWarning() << "MapDesign::branchHeadingColor no branchHeadingColorHint defined";
        }
        if (col != branchItem->headingColor()) 
            updateRequired = true;
    }
    return col;
}

FrameContainer::FrameType MapDesign::frameType(bool useInnerFrame, const int &depth)
{
    if (useInnerFrame)
        return innerFrameTypes.tryAt(depth);
    else
        return outerFrameTypes.tryAt(depth);
}

QColor MapDesign::frameBrushColor( bool useInnerFrame, const int &depth)
{
    if (useInnerFrame)
        return innerFrameBrushColors.tryAt(depth);
    else
        return outerFrameBrushColors.tryAt(depth);
}

QColor MapDesign::framePenColor( bool useInnerFrame, const int &depth)
{
    if (useInnerFrame)
        return innerFramePenColors.tryAt(depth);
    else
        return outerFramePenColors.tryAt(depth);
}

int MapDesign::framePenWidth( bool useInnerFrame, const int &depth)
{
    if (useInnerFrame)
        return innerFramePenWidths.tryAt(depth);
    else
        return outerFramePenWidths.tryAt(depth);
}

bool MapDesign::updateFrameWhenRelinking(bool useInnerFrame, const int &depth)
{
    if (useInnerFrame)
        return innerFrameUpdateWhenRelinking.tryAt(depth);
    else
        return outerFrameUpdateWhenRelinking.tryAt(depth);
}

QPen MapDesign::selectionPen()
{
    return selectionPenInt;
}

void MapDesign::setSelectionPen(const QPen &p)
{
    selectionPenInt = p;
}

QBrush MapDesign::selectionBrush()
{
    return selectionBrushInt;
}

void MapDesign::setSelectionBrush(const QBrush &b)
{
    selectionBrushInt = b;
}

int MapDesign::rotationHeading(const int &depth)
{
    return rotationHeadingInt.tryAt(depth);
}

int MapDesign::rotationSubtree(const int &depth)
{
    return rotationSubtreeInt.tryAt(depth);
}

qreal MapDesign::scaleHeading(const int &depth)
{
    return scaleHeadingInt.tryAt(depth);
}

qreal MapDesign::scaleSubtree(const int &depth)
{
    return scaleSubtreeInt.tryAt(depth);
}

QString MapDesign::saveToDir(const QString &tmpdir, const QString &prefix)
{
    XMLObj xml;
    QString s;

    xml.incIndent();
    s += xml.beginElement("mapdesign");

    xml.incIndent();
    s += xml.singleElement("md",
            xml.attribute("backgroundColor", backgroundColorInt.name()));

    // Save background image
    if (usesBackgroundImage && !backgroundImage.isNull()) {
        QString fn = "images/image-0-background";
        if (!backgroundImage.save(tmpdir + fn, "PNG", 100))
            qWarning() << "md::saveToDir failed to save background image to " << fn;
        else
            s += xml.singleElement("md",
                xml.attribute("backgroundImage", "file:" + fn) +
                xml.attribute("backgroundImageName", backgroundImageNameInt));
    }

    s += xml.singleElement("md",
            xml.attribute("font", fontInt.toString()));

    s += xml.singleElement("md",
            xml.attribute("selectionPenColor", selectionPenInt.color().name(QColor::HexArgb)));
    s += xml.singleElement("md",
            xml.attribute("selectionPenWidth", QString().setNum(selectionPenInt.width())));
    s += xml.singleElement("md",
            xml.attribute("selectionBrushColor", selectionBrushInt.color().name(QColor::HexArgb)));

    if (linkColorHintInt == LinkObj::HeadingColor)
        s += xml.singleElement("md",
                xml.attribute("linkColorHint", "HeadingColor"));

    s += xml.singleElement("md",
            xml.attribute("linkColor", defaultLinkColor().name()));

    s += xml.singleElement("md",
            xml.attribute("defXLinkColor", defXLinkPenInt.color().name()));
    s += xml.singleElement("md",
            xml.attribute("defXLinkWidth",
                 QString().setNum(defXLinkPenInt.width(), 10)));
    s += xml.singleElement("md",
            xml.attribute("defXLinkPenStyle",
                 penStyleToString(defXLinkPenInt.style())));
    s += xml.singleElement("md",
            xml.attribute("defXLinkStyleBegin", defXLinkStyleBeginInt));
    s += xml.singleElement("md",
            xml.attribute("defXLinkStyleEnd", defXLinkStyleEndInt));

    s += linkStyles.save("linkStyle", LinkObj::styleString);
    s += imagesContainerLayouts.save("imagesLayout", Container::layoutString);

    s += branchesContainerLayouts.save("branchesLayout", Container::layoutString);
    s += branchesContainerAndOrnamentsVerticalInt.saveBool("childrenAndHeadingVertical");
    s += branchesContainerVerticalAlignmentsInt.save("childrenVertAlignment", Container::verticalAlignmentString);

    s += innerFrameTypes.save("innerFrameType", FrameContainer::frameTypeString);
    s += innerFrameBrushColors.saveColor("innerFrameBrushColor");
    s += innerFramePenColors.saveColor("innerFramePenColor");
    s += innerFramePenWidths.saveInt("innerFramePenWidth");
    s += innerFrameUpdateWhenRelinking.saveBool("innerFrameUpdateWhenRelinking");

    s += outerFrameTypes.save("outerFrameType", FrameContainer::frameTypeString);
    s += outerFrameBrushColors.saveColor("outerFrameBrushColor");
    s += outerFramePenColors.saveColor("outerFramePenColor");
    s += outerFramePenWidths.saveInt("outerFramePenWidth");
    s += outerFrameUpdateWhenRelinking.saveBool("outerFrameUpdateWhenRelinking");

    xml.decIndent();
    s += xml.endElement("mapdesign");
    xml.decIndent();

    return s;
}
