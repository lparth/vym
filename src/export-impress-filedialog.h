#ifndef EXPORTIMPRESSFILEDIALOG
#define EXPORTIMPRESSFILEDIALOG

#include <QFileDialog>
#include <QStringList>

#include "settings.h"

/*! \brief Dialog to select output file and format for Open Office documents

This is an overloaded QFileDialog, which allows to select templates by setting a
type.
*/

class ExportImpressFileDialog : public QFileDialog {
    Q_OBJECT
  public:
    ExportImpressFileDialog();

    ExportImpressFileDialog(QWidget *parent, const QString &caption = QString());
    bool foundConfig();
    QString selectedConfig();
    void show();

  private slots:
    void newConfigPath(const QString &f);

  private:
    void init();
    void addFilter(const QString &);
    void scanExportConfigs(QDir);
    QStringList configPaths;
    QStringList filters;
    QString lastFilter;
};
#endif
