#include <QStringList>
#include <iostream>

#include "attributeitem.h"
#include "branchitem.h"
#include "misc.h"
#include "treeitem.h"
#include "vymmodel.h"
#include "xlink.h"
#include "xlinkitem.h"

extern ulong itemLastID;
extern FlagRowMaster *standardFlagsMaster;
extern FlagRowMaster *systemFlagsMaster;
extern FlagRowMaster *userFlagsMaster;

extern QTextStream vout;

TreeItem::TreeItem(TreeItem *parent)
{
    // qDebug() << "Constructor TreeItem this=" << this << "  parent=" << parent;
    init();
    parentItem = parent;

    rootItem = this;
    if (parentItem)
        rootItem = parentItem->rootItem;
}

TreeItem::~TreeItem()
{
    qDebug() << "Destr TreeItem begin: this=" << this << headingPlain();
    TreeItem *ti;
    while (!childItems.isEmpty()) {
        ti = childItems.takeFirst();
        qDebug() << "  In destr TI going to delete ti=" << ti << ti->headingPlain();
        delete ti;
    }
}

void TreeItem::init()
{
    model = nullptr;

    // Assign ID
    itemLastID++;
    itemID = itemLastID;
    uuid = QUuid::createUuid();

    branchOffset = 0;
    branchCounter = 0;

    imageOffset = 0;
    imageCounter = 0;

    attributeCounter = 0;
    attributeOffset = 0;

    xlinkCounter = 0;
    xlinkOffset = 0;

    target = false;

    headingInt.clear();
    headingInt.setText(" ");
    note.setText("");

    hidden = false;
    hideTemporaryInt = false;

    itemData.clear();
    itemData << "";

    urlInt = QString();
    urlTypeInt = NoUrl;

    standardFlags.setMasterRow(standardFlagsMaster);
    userFlags.setMasterRow(userFlagsMaster);
    systemFlags.setMasterRow(systemFlagsMaster);
}

void TreeItem::setModel(VymModel *m) { model = m; }

VymModel* TreeItem::getModel() { return model; }

MapDesign* TreeItem::mapDesign() { return model->mapDesign(); }

int TreeItem::getRowNumAppend(TreeItem *item)
{
    switch (item->type) {
        case Attribute:
            return attributeOffset + attributeCounter;
        case XLinkItemType:
            return xlinkOffset + xlinkCounter;
        case Image:
            return imageOffset + imageCounter;
        case MapCenter:
            return branchOffset + branchCounter;
        case Branch:
            return branchOffset + branchCounter;
        default:
            return -1;
    }
}

void TreeItem::appendChild(TreeItem *item)
{
    item->parentItem = this;
    item->rootItem = rootItem;
    item->setModel(model);

    if (item->type == Attribute) {
        // attribute are on top of list
        childItems.insert(attributeCounter, item);
        attributeCounter++;
        xlinkOffset++;
        imageOffset++;
        branchOffset++;
    }

    if (item->type == XLinkItemType) {
        childItems.insert(xlinkCounter + xlinkOffset, item);
        xlinkCounter++;
        imageOffset++;
        branchOffset++;
    }

    if (item->type == Image) {
        childItems.insert(imageCounter + imageOffset, item);
        imageCounter++;
        branchOffset++;
    }

    if (item->hasTypeBranch()) {
        // branches are on bottom of list
        childItems.append(item);
        branchCounter++;

        // Set correct type
        if (this == rootItem)
            item->setType(MapCenter);
        else
            item->setType(Branch);
    }
}

void TreeItem::removeChild(int row)
{
    if (row < 0 || row > childItems.size() - 1)
        qWarning("TreeItem::removeChild tried to remove non existing item?!");
    else {
        if (childItems.at(row)->type == Attribute) {
            attributeCounter--;
            xlinkOffset--;
            imageOffset--;
            branchOffset--;
        }
        if (childItems.at(row)->type == XLinkItemType) {
            xlinkCounter--;
            imageOffset--;
            branchOffset--;
        }
        if (childItems.at(row)->type == Image) {
            imageCounter--;
            branchOffset--;
        }
        if (childItems.at(row)->hasTypeBranch())
            branchCounter--;

        childItems.removeAt(row);
    }
}

TreeItem *TreeItem::child(int row) { return childItems.value(row); }

int TreeItem::childCount() const { return childItems.count(); }

int TreeItem::childNumber() const
{
    if (parentItem)
        return parentItem->childItems.indexOf(const_cast<TreeItem *>(this));

    return 0;
}

int TreeItem::columnCount() const { return 1; }

int TreeItem::branchCount() const { return branchCounter; }

int TreeItem::imageCount() const { return imageCounter; }

int TreeItem::xlinkCount() const { return xlinkCounter; }

int TreeItem::attributeCount() const { return attributeCounter; }

int TreeItem::row() const
{
    if (parentItem)
        return parentItem->childItems.indexOf(const_cast<TreeItem *>(this));

    qDebug() << "TI::row() pI=nullptr this=" << this << "  ***************";
    return 0;
}

int TreeItem::depth()
{
    // Rootitem  d = -1
    // MapCenter d =  0
    int d = -2;
    TreeItem *ti = this;
    while (ti != nullptr) {
        ti = ti->parent();
        d++;
    }
    return d;
}

TreeItem *TreeItem::parent()
{
    // qDebug() << "TI::parent of " << headingStd() << "  is " << parentItem;
    return parentItem;
}

bool TreeItem::isChildOf(TreeItem *ti)
{
    if (this == rootItem)
        return false;
    if (parentItem == ti)
        return true;
    if (parentItem == rootItem)
        return false;
    return parentItem->isChildOf(ti);
}

int TreeItem::childNum() { return parentItem->childItems.indexOf(this); }

int TreeItem::num()
{
    if (!parentItem)
        return -1;
    return parentItem->num(this);
}

int TreeItem::num(TreeItem *item)
{
    if (!item)
        return -1;
    if (!childItems.contains(item))
        return -1;
    switch (item->getType()) {
        case MapCenter: return childItems.indexOf(item) - branchOffset;
        case Branch: return childItems.indexOf(item) - branchOffset;
        case Image: return childItems.indexOf(item) - imageOffset;
        case Attribute: return childItems.indexOf(item) - attributeOffset;
        case XLinkItemType: return childItems.indexOf(item) - xlinkOffset;
        default: return -1;
    }
}
void TreeItem::setType(const Type t)
{
    type = t;
}

TreeItem::Type TreeItem::getType()
{
    if (type == Branch && depth() == 0)
        return MapCenter; // should not be necesssary
    return type;
}

bool TreeItem::hasTypeAttribute() const
{
    if (type == Attribute)
        return true;
    else
        return false;
}

bool TreeItem::hasTypeBranch() const
{
    if (type == Branch || type == MapCenter)
        return true;
    else
        return false;
}

bool TreeItem::hasTypeImage() const
{
    if (type == Image)
        return true;
    else
        return false;
}

bool TreeItem::hasTypeBranchOrImage() const
{
    if (type == Image || type == Branch || type == MapCenter)
        return true;
    else
        return false;
}

bool TreeItem::hasTypeXLink() const
{
    if (type == XLinkItemType)
        return true;
    else
        return false;
}

QString TreeItem::getTypeName()
{
    switch (type) {
        case Undefined:
            return QString("Undefined");
        case MapCenter:
            return QString("MapCenter");
        case Branch:
            return QString("Branch");
        case Image:
            return QString("Image");
        case Attribute:
            return QString("Attribute");
        case XLinkItemType:
            return QString("XLink");
        default:
            return QString("TreeItem::getTypeName no typename defined?!");
    }
}

QVariant TreeItem::data(int column) const { return itemData.value(column); }

void TreeItem::setHeading(const VymText &vt)
{
    headingInt = vt;
    itemData[0] = headingPlain().replace("\n"," "); // used in TreeEditor
}

void TreeItem::setHeadingPlainText(const QString &s)
{
    VymText vt;

    vt.setPlainText(s);

    if (!headingInt.isRichText())
        // Keep current color
        vt.setColor(headingInt.getColor());
    setHeading(vt);
}

Heading TreeItem::heading() const { return headingInt; }

QString TreeItem::headingText(bool indented)
{
    if (!indented)
        return headingInt.getText();
    else {
        QString ds;
        for (int i = 0; i < depth(); i++)
            ds += "  ";
        return ds + headingPlain();
    }
}

std::string TreeItem::headingStd() const
{
    return headingPlain().toStdString();
}

QString TreeItem::headingPlain() const
{
    // strip beginning and tailing WS
    return headingInt.getTextASCII().trimmed();
}

QString TreeItem::headingPlainWithParents(uint numberOfParents = 0)
{
    QString s = headingPlain();
    if (numberOfParents > 0) {
        TreeItem *ti = this;
        int l = numberOfParents;
        while (l > 0 && ti->depth() > 0) {
            ti = ti->parent();
            if (ti)
                s = ti->headingPlain() + " -> " + s;
            else
                l = 0;
            l--;
        }
    }
    return s;
}

void TreeItem::setHeadingColor(QColor color) { headingInt.setColor(color); }

QColor TreeItem::headingColor() { return headingInt.getColor(); }

void TreeItem::setUrl(const QString &u)
{
    urlInt = u;
    if (!urlInt.isEmpty())
        systemFlags.activate(QString("system-url"));
    else
        systemFlags.deactivate(QString("system-url"));
}

QString TreeItem::url() { return urlInt; }

bool TreeItem::hasUrl() { return !urlInt.isEmpty();}

void TreeItem::setUrlType(UrlType ut)
{
    urlTypeInt = ut;
}

TreeItem::UrlType TreeItem::urlType()
{
    return urlTypeInt;
}

void TreeItem::setVymLink(const QString &vl)
{
    if (!vl.isEmpty()) {
        // We need the relative (from loading)
        // or absolute path (from User event)
        // and build the absolute path.

        QDir d(vl);
        if (d.isAbsolute())
            vymLinkInt = vl;
        else {
            // If we have relative, use path of
            // current map to build absolute path
            // based on path of current map and relative
            // path to linked map
            QString p = dirname(model->getDestPath());
            vymLinkInt = convertToAbs(p, vl);
        }
        systemFlags.activate(QString("system-vymLink"));
    }
    else {
        vymLinkInt.clear();
        systemFlags.deactivate(QString("system-vymLink"));
    }
}

QString TreeItem::vymLink() { return vymLinkInt; }

bool TreeItem::hasVymLink() { return !vymLinkInt.isEmpty();}

void TreeItem::toggleTarget()
{
    systemFlags.toggle(QString("system-target"));
    target = systemFlags.isActive(QString("system-target"));
    model->emitDataChanged(this); // FIXME-4 better call from VM?
}

bool TreeItem::isTarget() { return target; }

bool TreeItem::isNoteEmpty() { return note.isEmpty(); }

bool TreeItem::clearNote()
{
    note.clear();
    return systemFlags.deactivate(QString("system-note"));
}

bool TreeItem::setNote(const VymText &vt)
{
    note = vt;

    if (note.isEmpty()) {
        if (systemFlags.isActive(QString("system-note")))
            return systemFlags.deactivate(QString("system-note"));
    }
    else {
        if (!systemFlags.isActive(QString("system-note")))
            return systemFlags.activate(QString("system-note"));
    }
    return false; // No need to update flag and reposition later
}

bool TreeItem::setNote(const VymNote &vn) { return setNote((VymText)vn); }

bool TreeItem::hasEmptyNote() { return note.isEmpty(); }

VymNote TreeItem::getNote() { return note; }

QString TreeItem::getNoteASCII(const QString &indent, const int &width)
{
    return note.getTextASCII(indent, width);
}

QString TreeItem::getNoteASCII() { return note.getTextASCII(); }

void TreeItem::activateStandardFlagByName(const QString &name)
{
    standardFlags.activate(name);
    //    model->emitDataChanged(this);
}

void TreeItem::deactivateStandardFlagByName(const QString &name)
{
    standardFlags.deactivate(name);
    //    model->emitDataChanged(this);
}

void TreeItem::deactivateAllStandardFlags()
{
    standardFlags.deactivateAll();
    userFlags.deactivateAll();
    //    model->emitDataChanged(this);
}

Flag *TreeItem::findFlagByUid(const QUuid &uid)
{
    Flag *f = standardFlagsMaster->findFlagByUid(uid);
    if (!f)
        f = userFlagsMaster->findFlagByUid(uid);
    return f;
}

Flag *TreeItem::toggleFlagByUid(const QUuid &uid, bool useGroups)
{
    Flag *f = standardFlagsMaster->findFlagByUid(uid);
    if (f)
        standardFlags.toggle(uid, useGroups);
    else {
        f = userFlagsMaster->findFlagByUid(uid);
        if (f)
            userFlags.toggle(uid, useGroups);
        else
            qWarning() << "TI::toggleFlag failed for flag " << uid;
    }

    return f;
}

void TreeItem::toggleSystemFlag(const QString &name, FlagRow *master)
{
    systemFlags.toggle(name, master);
    model->emitDataChanged(this);
}

bool TreeItem::hasActiveFlag(const QString &name)
{
    if (standardFlags.hasFlag(name))
        return standardFlags.isActive(name);
    else
        return userFlags.isActive(name);
}

bool TreeItem::hasActiveSystemFlag(const QString &name)
{
    return systemFlags.isActive(name);
}

void TreeItem::activateSystemFlagByName(const QString &name)
{
    systemFlags.activate(name);
}

void TreeItem::deactivateSystemFlagByName(const QString &name)
{
    systemFlags.deactivate(name);
}

QList<QUuid> TreeItem::activeFlagUids()
{
    return standardFlags.activeFlagUids() + userFlags.activeFlagUids();
}

QList<QUuid> TreeItem::activeSystemFlagUids()
{
    return systemFlags.activeFlagUids();
}

ulong TreeItem::getID() { return itemID; }

void TreeItem::setUuid(const QString &id) { uuid = QUuid(id); }

QUuid TreeItem::getUuid() { return uuid; }

TreeItem *TreeItem::getChildNum(const int &n)
{
    if (n >= 0 && n < childItems.count())
        return childItems.at(n);
    else
        return nullptr;
}

BranchItem *TreeItem::getFirstBranch()
{
    if (branchCounter > 0)
        return getBranchNum(0);
    else
        return nullptr;
}

BranchItem *TreeItem::getLastBranch()
{
    if (branchCounter > 0)
        return getBranchNum(branchCounter - 1);
    else
        return nullptr;
}

ImageItem *TreeItem::getFirstImage()
{
    if (imageCounter > 0)
        return getImageNum(imageCounter - 1);
    else
        return nullptr;
}

ImageItem *TreeItem::getLastImage()
{
    if (imageCounter > 0)
        return getImageNum(imageCounter - 1);
    else
        return nullptr;
}

TreeItem *TreeItem::getFirstItem()
{
    if (hasTypeBranch())
        return getFirstBranch();

    if (hasTypeImage())
        return getFirstImage();

    return nullptr;
}

TreeItem *TreeItem::getLastItem()
{
    if (hasTypeBranch())
        return getLastBranch();

    if (hasTypeImage())
        return getLastImage();

    return nullptr;
}

BranchItem *TreeItem::getNextBranch(BranchItem *currentBranch)
{
    if (!currentBranch)
        return nullptr;
    int n = num(currentBranch) + 1;
    if (n < branchCounter)
        return getBranchNum(branchOffset + n);
    else
        return nullptr;
}

BranchItem *TreeItem::getBranchNum(const int &n)
{
    if (n >= 0 && n < branchCounter)
        return (BranchItem *)getChildNum(branchOffset + n);
    else
        return nullptr;
}

QList <BranchItem*> TreeItem::getBranches()
{
    QList <BranchItem*> branches;
    for (int i = 0; i < branchCounter; i++)
        branches << getBranchNum(i);
    return branches;
}

ImageItem* TreeItem::getImageNum(const int &n)
{
    if (n >= 0 && n < imageCounter)
        return (ImageItem *)getChildNum(imageOffset + n);
    else
        return nullptr;
}

AttributeItem* TreeItem::getAttributeNum(const int &n)
{
    if (n >= 0 && n < attributeCounter)
        return (AttributeItem *)getChildNum(attributeOffset + n);
    else
        return nullptr;
}

AttributeItem* TreeItem::getAttributeByKey(const QString &k)
{
    AttributeItem *ai;
    for (int i = 0; i < attributeCount(); i++) {
        ai = getAttributeNum(i);
        if (ai->key() == k) return ai;
    }
    return nullptr;
}

QVariant TreeItem::attributeValue(const QString &k)
{
    AttributeItem *ai;
    for (int i = 0; i < attributeCount(); i++) {
        ai = getAttributeNum(i);
        if (ai->key() == k) return ai->value();
    }
    return QVariant();
}

XLinkItem* TreeItem::getXLinkItemNum(const int &n)
{
    if (n >= 0 && n < xlinkCounter)
        return (XLinkItem *)getChildNum(xlinkOffset + n);
    else
        return nullptr;
}

XLinkObj *TreeItem::getXLinkObjNum(const int &n)
{
    if (xlinkCounter > 0) {
        XLinkItem *xli = getXLinkItemNum(n);
        if (xli) {
            XLink *l = xli->getXLink();
            if (l)
                return l->getXLinkObj();
        }
    }
    return nullptr;
}

void TreeItem::setHideMode(HideTmpMode mode) 
{
    // Note: Overloaded in BranchItem
    // Will updateVisibility() of BranchContainer there

    if (type == Image || type == Branch || type == MapCenter)
    {
        if (mode == HideExport &&
            (hideTemporaryInt ||
             hasHiddenParent())) // FIXME-4  try to avoid calling
                                       // hasScrolledParent repeatedly

            // Hide stuff according to hideTemporaryInt flag and parents
            hidden = true;
        else
            // Do not hide, but still take care of scrolled status
            hidden = false;

        // And take care of my children
        for (int i = 0; i < branchCount(); ++i)
            getBranchNum(i)->setHideMode(mode);
    }
}

bool TreeItem::hasHiddenParent()
{
    // Calls parents recursivly to
    // find out, if we or parents are temp. hidden

    if (hidden || hideTemporaryInt)
        return true;

    if (parentItem)
        return parentItem->hasHiddenParent();
    else
        return false;
}

void TreeItem::setHideTemporary(bool b)
{
    if (type == MapCenter || type == Branch || type == Image) {
        hideTemporaryInt = b;
        if (b)
            systemFlags.activate(QString("system-hideInExport"));
        else
            systemFlags.deactivate(QString("system-hideInExport"));
    }
}

bool TreeItem::hideTemporary() { return hideTemporaryInt; }

bool TreeItem::isHidden() { return hidden; }

QString TreeItem::getGeneralAttr()
{
    QString s;
    if (hideTemporaryInt)
        s += attribute("hideInExport", "true");
    if (!urlInt.isEmpty())
        s += attribute("url", urlInt);
    if (hasVymLink())
        s += attribute("vymLink", convertToRel(model->getDestPath(), vymLinkInt));

    if (target)
        s += attribute("localTarget", "true");
    return s;
}
