#include <QDebug>
#include <QRegularExpression>

#include "heading-container.h"

#define qdbg() qDebug().nospace().noquote()

HeadingContainer::HeadingContainer()

{
    //qDebug() << "* Const HeadingContainer begin this = ";
    init();
}

HeadingContainer::~HeadingContainer()
{
    //qDebug() << "* Destr HeadingContainer" << getName();

    clearHeading();
}

void HeadingContainer::init()
{
    containerType = Container::Heading;

    headingInt.setText(" ");
    headingColorInt = QColor(Qt::black);

    layoutInt = Vertical;
    horizontalAlignmentInt = HorAlignedLeft;

    columnWidthInt = 40;  // Will be set from MapDesign in VymModel later
}

QGraphicsTextItem *HeadingContainer::newLine(QString s)
{
    QGraphicsTextItem *t = new QGraphicsTextItem(s, this);
    t->setFont(headingFontInt);
    t->setDefaultTextColor(headingColorInt);

    headingLines.append(t);

    return t;
}

void HeadingContainer::setHeading(const VymText &vt)
{
    headingInt = vt;

    QGraphicsTextItem *t;

    QString s = vt.getText();

    // remove old textlines and prepare generating new ones
    clearHeading();

    if (vt.isRichText()) {
        t = new QGraphicsTextItem(this);
        t->setHtml(s);
        t->setDefaultTextColor(headingColorInt);
        headingLines.append(t);

       // Translate line to move center to origin
        t->setPos(-t->boundingRect().center());
        setRect(t->boundingRect());
    } else {
        // prevent empty headingLines, so at least a small selection stays
        // visible for this heading
        if (s.length() == 0)
            s = "  ";

        int i = 0;  // index for actual search for ws
        int j = 0;  // index of last ws
        int k = 0;  // index of "<br>" or similar linebreak
        int br = 0; // width of found break, e.g. for <br> it is 4

        // set the text and wrap lines
        while (s.length() > 0) {
            i = s.indexOf(" ", i);
            if (i < 0 && j == 0) { // no ws found at all in s
                // append whole s
                t = newLine(s);
                s = "";
            }
            else {
                if (i < 0 && j > 0) { // no ws found in actual search
                    if (s.length() <= columnWidthInt) {
                        t = newLine(s);
                        s = "";
                    }
                    else {
                        t = newLine(s.left(j));
                        s = s.mid(j + 1, s.length());
                        j = 0;
                    }
                }
                else {
                    if (i >= 0 &&
                        i <= static_cast<int>(
                                 columnWidthInt)) { // there is a ws in columnWidth
                        if (br > 0) {
                            // here is a linebreak
                            t = newLine(s.left(i));
                            s = s.mid(i + br, s.length());
                            i = 0;
                            j = 0;
                            br = 0;
                        }
                        else {
                            j = i;
                            i++;
                        }
                    }
                    else {
                        if (i > static_cast<int>(columnWidthInt)) {
                            if (j > 0) { // a ws out of columnWidth, but we have
                                         // also one in
                                t = newLine(s.left(j));
                                s = s.mid(j + 1, s.length());
                                i = 0;
                                j = 0;
                            }
                            else { // a ws out of text, but none in
                                t = newLine(s.left(i));
                                s = s.mid(i + 1, s.length());
                                i = 0;
                            }
                        }
                    }
                }
            }
        }
    } // ASCII heading with multiple lines

    // Align headingLines vertically and find center
    qreal h = 0;
    qreal w = 0;
    qreal w_max = 0;
    foreach (QGraphicsTextItem *ti, headingLines) {
        ti->setPos(0, h);
        h += ti->boundingRect().height();
        w = ti->boundingRect().width();
        w_max = (w_max < w) ? w : w_max;
    }

    // Translate all lines to move center to origin
    QPointF v(-w_max / 2, - h / 2);

    foreach (QGraphicsTextItem *ti, headingLines)
        ti->setPos(ti->pos() + v);

    setRect(v.x(), v.y(), w_max, h);

    setName(QString("HC (%1)").arg(s));
}

void HeadingContainer::clearHeading()
{
    while (!headingLines.isEmpty())
        delete headingLines.takeFirst();
    setRect(QRectF());
}

void HeadingContainer::setFont(const QFont &f)
{
    headingFontInt = f;
}

QFont HeadingContainer::font() {return headingFontInt;}

void HeadingContainer::setColor(const QColor &c)
{
    headingInt.setColor(c); 
    if (headingColorInt != c) {
        headingColorInt = c;
        for (int i = 0; i < headingLines.size(); ++i)
            // TextItem
            headingLines.at(i)->setDefaultTextColor(headingColorInt);
    }
}

void HeadingContainer::setColumnWidth(const int &i)
{
    columnWidthInt = i;
    setHeading(headingInt);
}

int HeadingContainer::columnWidth()
{
    return columnWidthInt;
}

QString HeadingContainer::getName() {
    return Container::getName() + QString(" '%1'").arg(headingInt.getTextASCII());
}

void HeadingContainer::setScrollOpacity(qreal o) // FIXME-3 needed?
{
    qDebug() << "HC::setScrollOpacity o=" << o;
    foreach (QGraphicsTextItem *ti, headingLines)
        ti->setOpacity(o);
}

qreal HeadingContainer::getScrollOpacity() // FIXME-3 needed?
{
    return 0;
}

void HeadingContainer::reposition()
{
    // qdbg() << ind() << "HC::reposition " + info();
    return;
}

