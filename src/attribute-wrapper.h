#ifndef ATTRIBUTE_WRAPPER_H
#define ATTRIBUTE_WRAPPER_H

class AttributeItem;
class VymModel;

#include <QObject>

class AttributeWrapper : public QObject {
    Q_OBJECT
  public:
    AttributeWrapper(AttributeItem*);
    ~AttributeWrapper();
    VymModel* model();
    AttributeItem* attributeItem();

  public slots:
    bool hasRichTextHeading();
    QString headingText();  
    bool selectParent();
    void setHeadingRichText(const QString &);
    void setHeadingText(const QString &);

  private:
    AttributeItem *attributeItemInt;
};

#endif
