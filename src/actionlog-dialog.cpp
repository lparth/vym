#include "actionlog-dialog.h"

#include <QFileDialog>

#include "file.h"
#include "mainwindow.h"

extern Main *mainWindow;
extern QString vymName;

extern QString iconTheme;

extern bool useActionLog;
extern QString actionLogPath;

ActionLogDialog::ActionLogDialog()
{
    ui.setupUi(this);

    QDialog::setWindowTitle( vymName 
            + tr("Logfile settings", "Dialog to set if and where logfile is used"));

    updateControls();

    //ui.selectImageButton->setIcon(QIcon::fromTheme("document-new"));
    ui.setPathButton->setIcon(QPixmap(QString(":/document-open-%1.svg").arg(iconTheme)));
    connect(ui.setPathButton, SIGNAL(pressed()), this, SLOT(setLogPath()));

    connect(ui.useBackgroundImageCheckbox, SIGNAL(clicked()), this, SLOT(toggleBackgroundImage()));
}

int ActionLogDialog::exec()
{
    int r = QDialog::exec();
    /*
    if (ui.useBackgroundImageCheckbox->isChecked())
        model->setBackgroundImageName(ui.imageNameLineEdit->text());
    */
    return r;
}

void ActionLogDialog::toggleBackgroundImage()
{
    /*
    if (!ui.useBackgroundImageCheckbox->isChecked()) {
        model->unsetBackgroundImage();
        updateBackgroundImageControls();
    } else
        selectBackgroundImage();
    */
}

void ActionLogDialog::setLogPath()
{
    QStringList filters;
    filters << tr("Logfiles") + " (*.log)";
    QFileDialog fd;
    fd.setFileMode(QFileDialog::AnyFile);
    fd.setNameFilters(filters);
    fd.setWindowTitle(vymName + " - " + tr("Set path to logfile"));
    fd.setDirectory(dirname(actionLogPath));
    fd.setAcceptMode(QFileDialog::AcceptOpen);

    if (fd.exec() == QDialog::Accepted && !fd.selectedFiles().isEmpty()) {
        actionLogPath = fd.selectedFiles().first();
        updateControls();
    }
}

void ActionLogDialog::updateBackgroundColorButton()
{
    /*
    QPixmap pix(16, 16);
    pix.fill(model->mapDesign()->backgroundColor());
    */
}

void ActionLogDialog::updateControls()
{
    ui.logFilePathLineEdit->setText(actionLogPath);
    /*
    if (model->hasBackgroundImage()) {
        ui.imageNameLineEdit->setText(model->backgroundImageName());
        ui.useBackgroundImageCheckbox->setChecked(true);
    } else {
        ui.imageNameLineEdit->setText("");
        ui.useBackgroundImageCheckbox->setChecked(false);
    }
    */
}
