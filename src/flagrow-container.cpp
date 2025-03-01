#include <QDebug>

#include "flag.h"
#include "flag-container.h"
#include "flagrow-container.h"
#include "flagrow-master.h"

/////////////////////////////////////////////////////////////////
// FlagRowContainer
/////////////////////////////////////////////////////////////////
FlagRowContainer::FlagRowContainer()
{
    // qDebug() << "Const FlagRowContainer ()";
    // setPen(QPen(Qt::red));
    containerType = FlagRowCont;
    layoutInt = Horizontal;
    horizontalDirection = LeftToRight;
    horizontalAlignmentInt = HorAlignedCentered;
}

FlagRowContainer::~FlagRowContainer()
{
    // qDebug() << "Destr FlagRowContainer";
}

void FlagRowContainer::updateActiveFlagContainers(const QList<QUuid> activeFlagUids,
                                      FlagRowMaster *masterRowMain,
                                      FlagRowMaster *masterRowOptional)
{
    // Add missing active flags
    for (int i = 0; i <= activeFlagUids.size() - 1; i++) {
        if (!isFlagActive(activeFlagUids.at(i))) {
            Flag *f = masterRowMain->findFlagByUid(activeFlagUids.at(i));
            if (f) {
                activateFlag(f);
            }
            if (masterRowOptional) {
                f = masterRowOptional->findFlagByUid(activeFlagUids.at(i));
                if (f) {
                    activateFlag(f);
                }
            }
        }
    }

    // Remove flags no longer active in TreeItem
    foreach (QGraphicsItem *child, childItems()) {
        FlagContainer* fc = (FlagContainer*) child;
        if (!activeFlagUids.contains(fc->getUuid())) {
            delete fc;
        }
    }
}

bool FlagRowContainer::isFlagActive(const QUuid &uid)
{
    FlagContainer *fc = findFlagContainerByUid(uid);
    if (fc)
        return true;
    else
        return false;
}

void FlagRowContainer::activateFlag(Flag *flag)
{
    if (flag) {
        FlagContainer *fc = new FlagContainer;
        addContainer(fc);

        // qDebug() << "FRC::activateFlag  visible="<< visible << "  Qtvis=" << isVisible();

        // Loading an image  will *copy* it
        // and thus read the flag from cash
        fc->copy(flag->getImageContainer());
        fc->setUuid(flag->getUuid());
        fc->setZValue(QGraphicsItem::zValue());
    }
}

FlagContainer *FlagRowContainer::findFlagContainerByUid(const QUuid &uid)
{
    foreach (QGraphicsItem *child, childItems()) {
        FlagContainer* fc = (FlagContainer*) child;
        if (fc->getUuid() == uid)
            return fc;
    }
    return nullptr;
}

QUuid FlagRowContainer::findFlagByPos(const QPointF &p)
{
    if (!boundingRect().contains(mapFromScene(p)))
        return QUuid();

    foreach (QGraphicsItem *child, childItems()) {
        FlagContainer* fc = (FlagContainer*) child;
        if (fc->boundingRect().contains(fc->mapFromScene(p)))
        return fc->getUuid();
    }
    return QUuid();
}

void FlagRowContainer::reposition()
{
    //qDebug() << "FRC::reposition()" << getName() << parentContainer()->parentContainer()->parentContainer()->getName();
    Container::reposition();
}
