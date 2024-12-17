#include <QMessageBox>

#include "export-impress.h"

#include "branchitem.h"
#include "mainwindow.h"
#include "vymmodel.h"
#include "zip-agent.h"

extern QString vymName;
extern Main *mainWindow;

ExportImpress::ExportImpress()
{
    exportName = "Impress";
    filter = "LibreOffice Impress (*.odp);;All (* *.*)";
    caption = vymName + " -" +
              QObject::tr("Export as LibreOffice Impress presentation");
    useSections = false;
}

ExportImpress::~ExportImpress() {}

QString ExportImpress::buildList(TreeItem *current)
{
    QString r;

    uint i = 0;
    BranchItem *bi = current->getFirstBranch();
    if (bi) {
        // Start list
        r += "<text:list text:style-name=\"vym-list\">\n";
        while (bi) {
            if (!bi->hasHiddenParent()) {
                r += "<text:list-item><text:p >";
                r += quoteMeta(bi->headingPlain());
                // If necessary, write note
                if (!bi->isNoteEmpty()) {
                    QString n = bi->getNoteASCII();
                    n.replace("&", "&amp;");
                    n.replace("<", "&lt;");
                    n.replace(">", "&gt;");
                    r += "<text:line-break/>" + n;
                }
                r += "</text:p>";
                r += buildList(bi); // recursivly add deeper branches
                r += "</text:list-item>\n";
            }
            i++;
            bi = current->getBranchNum(i);
        }
        r += "</text:list>\n";
    }
    return r;
}

void ExportImpress::exportPresentation()
{
    QString allPages;

    BranchItem *firstMCO =
        (BranchItem *)(model->getRootItem()->getFirstBranch());
    if (!firstMCO) {
        QMessageBox::critical(0, QObject::tr("Critical Export Error"),
                              QObject::tr("No objects in map!"));
        return;
    }

    // Insert new content
    // FIXME add extra title in mapinfo for vym 1.13.x
    content.replace("<!-- INSERT TITLE -->",
                    quoteMeta(firstMCO->headingPlain()));
    content.replace("<!-- INSERT AUTHOR -->", quoteMeta(model->getAuthor()));

    QString onePage;
    QString list;

    BranchItem *sectionBI;
    int i = 0;
    BranchItem *pagesBI;
    int j = 0;

    int mapcenters = model->getRootItem()->branchCount();

    // useSections already has been set in setConfigFile
    if (mapcenters > 1)
        sectionBI = firstMCO;
    else
        sectionBI = firstMCO->getFirstBranch();

    // Walk sections
    while (sectionBI && !sectionBI->hasHiddenParent()) {
        if (useSections) {
            // Add page with section title
            onePage = sectionTemplate;
            onePage.replace("<!-- INSERT PAGE HEADING -->",
                            quoteMeta(sectionBI->headingPlain()));
            allPages += onePage;
            pagesBI = sectionBI->getFirstBranch();
        }
        else {
            // i=-2; // only use inner loop to
            // turn mainbranches into pages
            // sectionBI=firstMCO;
            pagesBI = sectionBI;
        }

        j = 0;
        while (pagesBI && !pagesBI->hasHiddenParent()) {
            // Add page with list of items
            onePage = pageTemplate;
            onePage.replace("<!-- INSERT PAGE HEADING -->",
                            quoteMeta(pagesBI->headingPlain()));
            list = buildList(pagesBI);
            onePage.replace("<!-- INSERT LIST -->", list);
            allPages += onePage;
            if (pagesBI != sectionBI) {
                j++;
                pagesBI = ((BranchItem *)pagesBI->parent())->getBranchNum(j);
            }
            else
                pagesBI = nullptr; // We are already iterating over the sectionBIs
        }
        i++;
        if (mapcenters > 1)
            sectionBI = model->getRootItem()->getBranchNum(i);
        else
            sectionBI = firstMCO->getBranchNum(i);
    }

    content.replace("<!-- INSERT PAGES -->", allPages);

    // Write modified content
    QFile f(contentFile);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Could not write %1").arg(contentFile));
        mainWindow->statusMessage(QString(QObject::tr("Export failed.")));
        return;
    }

    QTextStream t(&f);
    t << content;
    f.close();

    // zip tmpdir to destination
    ZipAgent zipAgent(tmpDir, filePath);
    zipAgent.setBackgroundProcess(false);
    zipAgent.startZip();
    if(zipAgent.exitStatus() != QProcess::NormalExit ||
            zipAgent.exitCode() > 0) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Could not compress file %1").arg(filePath));
    }


    displayedDestination = filePath;

    result = ExportBase::Success;

    QStringList args;
    args << filePath;
    args << configFile;
    completeExport(args);
}

bool ExportImpress::setConfigFile(const QString &cf)
{
    configFile = cf;
    int i = cf.lastIndexOf("/");
    if (i >= 0)
        configDir = cf.left(i);
    SimpleSettings set;

    if (!set.readSettings(configFile)) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Couldn't read settings from \"%1\"").arg(configFile));
        return false;
    }

    // set paths
    templateDir = configDir + "/" + set.value("Template");

    setupTmpDir();

    QDir d(templateDir);
    if (!d.exists()) {
        QMessageBox::critical(0, QObject::tr("Critical Export Error"),
                              QObject::tr("Check \"%1\" in\n%2")
                                  .arg("Template=" + set.value("Template"))
                                  .arg(configFile));
        return false;
    }

    contentTemplateFile = templateDir + "content-template.xml";
    pageTemplateFile = templateDir + "page-template.xml";
    sectionTemplateFile = templateDir + "section-template.xml";
    contentFile = tmpDir.path() + "/content.xml";

    if (set.value("useSections").contains("yes"))
        useSections = true;

    // Copy template to tmpdir
    copyDir(templateDir, tmpDir);

    // Read content-template
    if (!loadStringFromDisk(contentTemplateFile, content)) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Could not read %1").arg(contentTemplateFile));
        return false;
    }

    // Read page-template
    if (!loadStringFromDisk(pageTemplateFile, pageTemplate)) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Could not read %1").arg(pageTemplateFile));
        return false;
    }

    // Read section-template
    if (useSections &&
        !loadStringFromDisk(sectionTemplateFile, sectionTemplate)) {
        QMessageBox::critical(
            0, QObject::tr("Critical Export Error"),
            QObject::tr("Could not read %1").arg(sectionTemplateFile));
        return false;
    }
    return true;
}
