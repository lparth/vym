#include "image-wrapper.h"

#include "imageitem.h"

#include "vymmodel.h"

#include <QJSEngine>
extern QJSEngine *scriptEngine;

ImageWrapper::ImageWrapper(ImageItem *ii)
{
    //qDebug() << "Constr ImageWrapper (ii)";
    imageItem = ii;
}

ImageWrapper::~ImageWrapper()
{
    //qDebug() << "Destr ImageWrapper";
}

QString ImageWrapper::headingText()
{
    return setResult(imageItem->headingPlain());
}

void ImageWrapper::setHeadingRichText(const QString &text)
{
    VymText vt;
    vt.setRichText(text);
    imageItem->getModel()->setHeading(vt, imageItem);
}

void ImageWrapper::setHeadingText(const QString &text)
{
    imageItem->getModel()->setHeadingPlainText(text, imageItem);
}

