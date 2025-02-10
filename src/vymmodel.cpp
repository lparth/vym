#include <QApplication>
#include <QSvgGenerator>

#if defined(VYM_DBUS)
#include <QtDBus/QDBusConnection>
#endif

#ifndef Q_OS_WINDOWS
#include <unistd.h>
#else
#define sleep Sleep
#endif

#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QPrinter>

#include "vymmodel.h"

#include "attributeitem.h"
#include "branchitem.h"
#include "branchpropeditor.h"
#include "confluence-agent.h"
#include "download-agent.h"
#include "editxlinkdialog.h"
#include "export-ao.h"
#include "export-ascii.h"
#include "export-confluence.h"
#include "export-csv.h"
#include "export-firefox.h"
#include "export-html.h"
#include "export-impress.h"
#include "export-impress-filedialog.h"
#include "export-latex.h"
#include "export-markdown.h"
#include "export-orgmode.h"
#include "export-taskjuggler.h"
#include "file.h"
#include "findresultmodel.h"
#include "heading-container.h"
#include "image-container.h"
#include "jira-agent.h"
#include "linkobj.h"
#include "lockedfiledialog.h"
#include "mainwindow.h"
#include "mapdesign.h"
#include "mapeditor.h"
#include "misc.h"
#include "noteeditor.h"
#include "options.h"
#include "scripteditor.h"
#include "slideitem.h"
#include "slidemodel.h"
#include "taskeditor.h"
#include "taskmodel.h"
#include "treeitem.h"
#include "vymmodelwrapper.h"
#include "vymview.h"
#include "warningdialog.h"
#include "xlink.h"
#include "xlinkitem.h"
#include "xlinkobj.h"
#include "xml-freeplane.h"
#include "xml-vym.h"
#include "xmlobj.h"
#include "zip-agent.h"

extern bool debug;
extern bool testmode;
extern bool restoreMode;
extern QStringList ignoredLockedFiles;

extern BranchPropertyEditor *branchPropertyEditor;
extern Main *mainWindow;

extern QDir tmpVymDir;

extern bool useActionLog;
extern QString actionLogPath;

extern NoteEditor *noteEditor;
extern TaskEditor *taskEditor;
extern ScriptEditor *scriptEditor;
extern FlagRowMaster *standardFlagsMaster;
extern FlagRowMaster *userFlagsMaster;

extern Options options;

extern QString clipboardDir;
extern QString clipboardFile;

extern ImageIO imageIO;

extern TaskModel *taskModel;

extern QString vymName;
extern QString vymVersion;
extern QDir vymBaseDir;

extern QDir lastImageDir;
extern QDir lastExportDir;

extern Settings settings;
extern QTextStream vout;

extern bool usingDarkTheme;
extern QColor vymBlue;

uint VymModel::idLast = 0; // make instance

VymModel::VymModel()
{
    // qDebug()<< "Const VymModel" << this;
    init();
    rootItem->setModel(this);
    wrapper = new VymModelWrapper(this);
}

VymModel::~VymModel()
{
    //qDebug() << "Destr VymModel begin this=" << this << "  " << mapName << "zipAgent=" << zipAgent;

    mapEditor = nullptr;
    repositionBlocked = true;
    autosaveTimer->stop();
    filePath.clear();
    fileChangedTimer->stop();

    if (zipAgent) {
        mainWindow->statusMessage(tr("Waiting until map is completely saved and compressed..."));
        zipAgent->waitForFinished();
        
        // zipAgent might be set to nullptr already in VymModel::zipFinished
        if (zipAgent) {
            if (zipAgent->exitStatus() != QProcess::NormalExit) {
                QMessageBox::critical(0, QObject::tr("Critical Error"),
                                      QObject::tr("zip didn't exit normally"));
            }
            else {
                if (zipAgent->exitCode() > 0) {
                    QMessageBox::critical(
                        0, QObject::tr("Critical Error"),
                        QString("zip exit code:  %1").arg(zipAgent->exitCode()));
                }
            }
            zipAgent->deleteLater();
            zipAgent = nullptr;
        }
    }

    vymLock.releaseLock();

    // Delete rootItem already now, while VymModel is still around
    // ImageItems can ask VymModel for a path in their destructor then.
    // Delete whole tree, XLinkItems will be queude for deletion in xlinksTrash
    delete rootItem;

    emptyXLinksTrash();

    delete (wrapper);
    delete mapDesignInt;


    //qDebug() << "Destr VymModel end this=" << this;

    logInfo("VymModel destroyed", __func__);
}

void VymModel::clear()
{
    while (rootItem->childCount() > 0) {
        // qDebug()<<"VM::clear  ri="<<rootItem<<"
        // ri->count()="<<rootItem->childCount();
        deleteItem(rootItem->getChildNum(0));
    }
}

void VymModel::init()
{
    // No MapEditor yet
    mapEditor = nullptr;

    // No ZipAgent yet and not saving
    zipAgent = nullptr;
    isSavingInt = false;

    // Use default author
    author = settings
            .value("/user/name",
                    tr("unknown user", "default name for map author in settings")).toString();
    // MapDesign
    mapDesignInt = new MapDesign;

    // States and IDs
    idLast++;
    modelIdInt = idLast;
    mapChanged = false;
    mapDefault = true;
    mapUnsaved = false;
    buildingUndoScript = false;

    // Selection history
    selModel = nullptr;
    selectionBlocked = false;
    resetSelectionHistory();

    resetHistory();

    // Create tmp dirs
    makeTmpDirectories();

    // Files
    readonly = false;
    zipped = true;
    filePath = "";
    fileName = tr("unnamed");
    mapName = fileName;
    repositionBlocked = false;
    saveStateBlocked = false;

    autosaveTimer = new QTimer(this);
    connect(autosaveTimer, SIGNAL(timeout()), this, SLOT(autosave()));

    fileChangedTimer = new QTimer(this);
    connect(fileChangedTimer, SIGNAL(timeout()), this, SLOT(fileChanged()));
    fileChangedTimer->start(3000);

    taskAlarmTimer = new QTimer(this);
    connect(taskAlarmTimer, SIGNAL(timeout()), this, SLOT(updateTasksAlarm()));
    taskAlarmTimer->start(3000);

    hasContextPos = false;

    hideMode = TreeItem::HideNone;

    // Animation in MapEditor
    zoomFactor = 1;
    mapRotationInt = 0;
    animDuration = 2000;
    animCurve = QEasingCurve::OutQuint;

    // Initialize presentation slides
    slideModel = new SlideModel(this);
    blockSlideSelection = false;

    // Network
    netstate = Offline;

#if defined(VYM_DBUS)
    // Announce myself on DBUS
    new AdaptorModel(this); // Created and not deleted as documented in Qt
    if (!QDBusConnection::sessionBus().registerObject(
            QString("/vymmodel_%1").arg(modelIdInt), this))
        qWarning("VymModel: Couldn't register DBUS object!");
#endif
}

void VymModel::makeTmpDirectories()
{
    // Create unique temporary directories
    tmpMapDirPath = tmpVymDir.path() + QString("/model-%1").arg(modelIdInt);
    histPath = tmpMapDirPath + "/history";
    QDir d;
    if (!d.mkdir(tmpMapDirPath))
        qWarning() << "Couldn't create tmpMapDir=" << tmpMapDirPath;

    QString s = tmpMapDirPath + "/zipDir";
    if (!d.mkpath(s))
        qWarning() << "Couldn't create zipDirInt=" << s;

    zipDirInt.setPath(s);
}

QString VymModel::tmpDirPath() { return tmpMapDirPath; }

QString VymModel::zipDirPath() { return zipDirInt.path(); }

MapEditor *VymModel::getMapEditor() { return mapEditor; }

VymModelWrapper *VymModel::getWrapper() { return wrapper; }

bool VymModel::isRepositionBlocked() { return repositionBlocked; }

void VymModel::updateActions()
{
    // Tell mainwindow to update states of actions
    mainWindow->updateActions();
}

bool VymModel::setData(const QModelIndex &, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;

    setHeadingPlainText(value.toString());

    return true;
}

void VymModel::resetUsedFlags()
{
    standardFlagsMaster->resetUsedCounter();
    userFlagsMaster->resetUsedCounter();
}

QString VymModel::saveToDir(const QString &tmpdir, const QString &prefix,
                            FlagRowMaster::WriteMode flagMode, const QPointF &offset,
                            TreeItem *saveSel)
{
    // tmpdir	    temporary directory to which data will be written
    // prefix	    mapname, which will be appended to images etc.
    //
    // writeflags   Only write flags for "real" save of map, not undo
    // offset	    offset of bbox of whole map in scene.
    //		    Needed for XML export

    XMLObj xml;

    // Save Header
    QString header =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><!DOCTYPE vymmap>\n";

    QString mapAttr = xml.attribute("version", vymVersion);
    // Current map version after load still might be original one, change it now.
    mapVersionInt = vymVersion;

    QString design;

    if (!saveSel) {
        mapAttr += xml.attribute("date", toS(QDate::currentDate())) + "\n";

        if (!author.isEmpty())
            mapAttr += xml.attribute("author", author) + "\n";
        if (!title.isEmpty())
            mapAttr += xml.attribute("title", title) + "\n";
        if (!comment.isEmpty())
            mapAttr += xml.attribute("comment", comment) + "\n";

        mapAttr += xml.attribute("branchCount", QString().number(branchCount()));
        mapAttr += xml.attribute("mapZoomFactor",
                     QString().setNum(mapEditor->zoomFactorTarget()));
        mapAttr += xml.attribute("mapRotation",
                     QString().setNum(mapEditor->rotationTarget()));

        design = mapDesignInt->saveToDir(tmpdir, prefix);
    }
    header += xml.beginElement("vymmap", mapAttr);


    xml.incIndent();

    // Find the used flags while traversing the tree
    resetUsedFlags();

    // Temporary list of links
    QList<XLink *> tmpXLinks;

    QString tree;
    // Build xml recursivly
    if (!saveSel) {
        // Save all mapcenters as complete map, if saveSel not set
        tree += saveTreeToDir(tmpdir, prefix, offset, tmpXLinks);

        // Save local settings
        tree += settings.getDataXML(destPath);

        // Save selection
        if (getSelectedItems().count() > 0 && !saveSel)
            tree += xml.valueElement("select", getSelectString());
    }
    else {
        switch (saveSel->getType()) {
            case TreeItem::Branch:
            case TreeItem::MapCenter:
                // Save Subtree
                tree += ((BranchItem *)saveSel)
                            ->saveToDir(tmpdir, prefix, offset, tmpXLinks, exportBoundingBoxes);
                break;
            case TreeItem::Image:
                tree += ((ImageItem *)saveSel)->saveToDir(tmpdir);
                break;
            case TreeItem::XLinkItemType:
                tree += ((XLinkItem *)saveSel)->getXLink()->saveToDir();
                break;
            default:
                // other types shouldn't be safed directly...
                break;
        }
    }

    QString flags;

    // Write images and definitions of used user flags
    standardFlagsMaster->saveDataToDir(tmpdir + "/flags/standard/", FlagRowMaster::UsedFlags);
    if (flagMode != FlagRowMaster::NoFlags) {
        // First find out, which flags are used
        // Definitions
        flags += userFlagsMaster->saveDef(flagMode);

        userFlagsMaster->saveDataToDir(tmpdir + "/flags/user/", flagMode);
    }

    QString footer;
    // Save XLinks
    for (int i = 0; i < tmpXLinks.count(); ++i)
        footer += tmpXLinks.at(i)->saveToDir();

    // Save slides
    footer += slideModel->saveToDir();

    xml.decIndent();
    footer += xml.endElement("vymmap");

    return header + design + flags + tree + footer;
}

QString VymModel::saveTreeToDir(const QString &tmpdir, const QString &prefix,
                                const QPointF &offset, QList<XLink *> &tmpXLinks)
{
    QString s;
    for (int i = 0; i < rootItem->branchCount(); i++)
        s += rootItem->getBranchNum(i)->saveToDir(
                tmpdir,
                prefix,
                offset,
                tmpXLinks,
                exportBoundingBoxes);
    return s;
}

void VymModel::setFilePath(QString fpath, QString destname)
{
    if (fpath.isEmpty() || fpath == "") {
        filePath = "";
        fileName = "";
        destPath = "";
    }
    else {
        filePath = fpath;    // becomes absolute path
        fileName = fpath;    // gets stripped of path
        destPath = destname; // needed for vymlinks and during load to reset
                             // fileChangedTime

        // If fpath is not an absolute path, complete it
        filePath = QDir(fpath).absolutePath();
        fileDir = filePath.left(filePath.lastIndexOf("/"));

        // Set short name, too. Search from behind:
        fileName = basename(fileName);

        // Forget the .vym (or .xml) for name of map
        mapName =
            fileName.left(fileName.lastIndexOf(".", -1, Qt::CaseSensitive));
    }
}

void VymModel::setFilePath(QString fpath) { setFilePath(fpath, fpath); }

QString VymModel::getFileDir() { return fileDir; }

QString VymModel::getFilePath() { return filePath; }

QString VymModel::getFileName() { return fileName; }

QString VymModel::getMapName() { return mapName; }

QString VymModel::getDestPath() { return destPath; }

File::ErrorCode VymModel::loadMap(QString fname, const File::LoadMode &lmode,
                                  const File::FileType &ftype,
                                  const int &contentFilter,
                                  BranchItem *insertBranch,
                                  int insertPos)
{
    File::ErrorCode err = File::Success;

    // Get updated zoomFactor, before applying one read from file in the end
    if (mapEditor) {
        zoomFactor = mapEditor->zoomFactorTarget();
        mapRotationInt = mapEditor->rotationTarget();
    }

    BaseReader *reader;
    fileType = ftype;
    switch (fileType) {
        case File::VymMap:
            reader = new VymReader(this);
            // For imports we might want to ignore slides
            reader->setContentFilter(contentFilter);// FIXME-4 Maybe ignore slides hardcoded, when parsing and remove contentFilter?
            break;
        case File::FreemindMap:
            reader = new FreeplaneReader(this);
            break;
        default:
            QMessageBox::critical(0, tr("Critical Parse Error"),
                                  "Unknown FileType in VymModel::load()");
            return File::Aborted;
    }

    if (lmode == File::NewMap) {
        // Reset timestamp to check for later updates of file
        fileChangedTime = QFileInfo(destPath).lastModified();

        selModel->clearSelection();
    }

    bool zipped_org = zipped;

    // Create temporary directory for unzip
    bool ok;
    QString tmpZipDir = makeTmpDir(ok, tmpDirPath(), "unzip");
    if (!ok) {
        QMessageBox::critical(
            0, tr("Critical Load Error"),
            tr("Couldn't create temporary directory before load\n"));
        return File::Aborted;
    }

    QString xmlfile;
    if (fname.right(4) == ".xml" || fname.right(3) == ".mm") {
        xmlfile = fname;
        zipped = false;

        if (lmode == File::NewMap || lmode == File::DefaultMap)
            zipped_org = false;
    } else {
        // Try to unzip file
        QFile file(fname);
        if (file.size() > 2000000)
            // Inform user that unzipping might take a while.
            // Detailed estimation about number of branches required for progress bar 
            // is within the zipfile and cannot be used yet.
            mainWindow->statusMessage(tr("Uncompressing %1").arg(fname));

        ZipAgent zipAgent(tmpZipDir, fname);
        zipAgent.setBackgroundProcess(false);
        zipAgent.startUnzip();
        if (zipAgent.exitStatus() != QProcess::NormalExit)
            err = File::Aborted;

        if (file.size() > 2000000)
            // Inform user that unzipping might take a while.
            // Detailed estimation about number of branches required for progress bar 
            // is within the zipfile and cannot be used yet.
            mainWindow->statusMessage(tr("Loading %1").arg(fname), 0);
    }

    if (zipped) {
        // Look for mapname.xml
        xmlfile = fname.left(fname.lastIndexOf(".", -1, Qt::CaseSensitive));
        xmlfile = xmlfile.section('/', -1);
        QFile mfile(tmpZipDir + "/" + xmlfile + ".xml");
        if (!mfile.exists()) {
            // mapname.xml does not exist, well,
            // maybe someone renamed the mapname.vym file...
            // Try to find any .xml in the toplevel
            // directory of the .vym file
            QStringList filters;
            filters << "*.xml";
            QStringList flist = QDir(tmpZipDir).entryList(filters);
            if (flist.count() == 1) {
                // Only one entry, take this one
                xmlfile = tmpZipDir + "/" + flist.first();
            } else {

                // FIXME-5 Multiple entries, load all (but only the first one
                // into this ME)
                // mainWindow->fileLoadFromTmp (flist);
                // returnCode = 1;	// Silently forget this attempt to load
                qWarning() << "VymModel::loadMap multimap found " << flist;
            }

            if (flist.isEmpty()) {
                QMessageBox::critical(
                    0, tr("Critical Load Error"),
                    tr("Couldn't find a map (*.xml) in .vym archive.\n"));
                err = File::Aborted;
            }
        } // file doesn't exist
        else
            xmlfile = mfile.fileName();
    }

    QFile file(xmlfile);

    // I am paranoid: file should exist anyway
    // according to check in mainwindow.
    if (!file.exists()) {
        QMessageBox::critical(
            0, tr("Critical Parse Error"),
            tr(QString("Couldn't open map %1").arg(file.fileName()).toUtf8()));
        err = File::Aborted;
    }
    else {
        bool saveStateBlockedOrg = saveStateBlocked;
        repositionBlocked = true;
        saveStateBlocked = true;
        mapEditor->setViewportUpdateMode(QGraphicsView::NoViewportUpdate);

        // We need to set the tmpDir in order  to load files with rel. path
        QString tmpdir;
        if (zipped)
            tmpdir = tmpZipDir;
        else
            tmpdir = fname.left(fname.lastIndexOf("/", -1));

        reader->setTmpDir(tmpdir);

        reader->setLoadMode(lmode);

        if (lmode == File::ImportReplace || lmode == File::ImportAdd)
            reader->setInsertBranch(insertBranch);

        if (lmode == File::ImportAdd)
            reader->setInsertPos(insertPos);

        bool parsedWell = false;

        // Open file
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            QMessageBox::critical(nullptr, "VymModel::loadMap",
                    QString("Cannot read file %1:\n%2.")
                    .arg(QDir::toNativeSeparators(fileName),
                        file.errorString()));
            err = File::Aborted;
        } else {
            // Here we actually parse the XML file
            parsedWell = reader->read(&file);

            file.close();
        }

        // Aftermath
        repositionBlocked = false;
        saveStateBlocked = saveStateBlockedOrg;
        mapEditor->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);

        if (err != File::Aborted) {
            if (parsedWell) {
                reposition();

                if (lmode == File::NewMap)  // no lockfile for default map!
                {
                    mapDefault = false;
                    mapChanged = false;
                    mapUnsaved = false;
                    autosaveTimer->stop();

                    resetHistory();
                    resetSelectionHistory();

                    // Set treeEditor and slideEditor visibility per map
                    vymView->readSettings();

                    if (!tryVymLock() && debug)
                        qWarning() << "VM::loadMap  no lockfile created!";
                }

                // Recalc priorities and sort
                taskModel->recalcPriorities();

                // Log
                logInfo(QString("Map loaded successfully from \"%1\"").arg(fname), __func__);
            } else {
                QMessageBox::critical(0, tr("Critical Parse Error"),
                                        reader->errorString());
                // returnCode=1;
                // Still return "success": the map maybe at least
                // partially read by the parser

                setReadOnly(true);
            }
        } // err != File::Aborted
    }

    // If required, fix positions when importing from old versions
    if (versionLowerOrEqual(mapVersionInt, "2.9.500")) {
        foreach (BranchItem *center, rootItem->getBranches()) {
            foreach (BranchItem *mainBranch, center->getBranches()) {
                BranchContainer *bc = mainBranch->getBranchContainer();
                QRectF rb = bc->ornamentsRect();
                QPointF offset;
                offset.setX(rb.width() / 2);
                offset.setY(rb.height() / 2);
                bc->setPos(bc->x() + offset.x(), bc->y() + offset.y());
                qDebug() << "VymModel::loadMap adjusting legacy position of " << mainBranch->headingPlain() << "  offset: " << toS(offset);
            }
        }
        reposition();
    }


    // Cleanup
    removeDir(QDir(tmpZipDir));
    delete reader;

    // Restore original zip state
    zipped = zipped_org;

    updateActions();

    if (lmode != File::NewMap)
        emitUpdateQueries();

    if (mapEditor) {
        mapEditor->setZoomFactorTarget(zoomFactor);
        mapEditor->setRotationTarget(mapRotationInt);
    }

    qApp->processEvents(); // Update view (scene()->update() is not enough)
    return err;
}

void VymModel::saveMap(const File::SaveMode &savemode)
{
    // Block closing the map while saving, esp. while zipping
    isSavingInt = true;

    QString mapFileName;
    QString saveFilePath;

    File::ErrorCode err = File::Success;

    if (zipped)
        // save as .xml
        mapFileName = mapName + ".xml";
    else
        // use name given by user, could be anything
        mapFileName = fileName;

    // Look, if we should zip the data:
    if (!zipped)
    {
        QMessageBox mb(QMessageBox::Warning, vymName,
           tr("The map %1\ndid not use the compressed "
              "vym file format.\nWriting it uncompressed will also "
              "write images \n"
              "and flags and thus may overwrite files into the "
              "given directory\n\nDo you want to write the map")
               .arg(filePath));
        QPushButton *compressedButton = mb.addButton(
            tr("compressed (vym default)"),
            QMessageBox::AcceptRole);
        QPushButton *uncompressedButton = mb.addButton(
            tr("uncompressed, potentially overwrite existing data"),
            QMessageBox::NoRole);
        mb.addButton(
            tr("Cancel"),
            QMessageBox::RejectRole);
        mb.exec();
        if (mb.clickedButton() == compressedButton)
            // save compressed (default file format)
            zipped = true;
        else if (mb.clickedButton() == uncompressedButton)
            zipped = false; // FIXME-5 Filename suffix still could be .xml instead of .vym
        else  {
            // do nothing
            isSavingInt = false;
            return; 
        }
    }

    // First backup existing file, we
    // don't want to add to old zip archives
    QFile f(destPath);
    if (f.exists()) {
        if (settings.value("/system/writeBackupFile").toBool()) {
            QString backupFileName(destPath + "~");
            QFile backupFile(backupFileName);
            if (backupFile.exists() && !backupFile.remove()) {
                QMessageBox::warning(
                    0, tr("Save Error"),
                    tr("%1\ncould not be removed before saving")
                        .arg(backupFileName));
            }
            else {
                if (!f.rename(backupFileName)) {
                    QMessageBox::warning(
                        0, tr("Save Error"),
                        tr("%1\ncould not be renamed before saving")
                            .arg(destPath));
                }
            }
        }
    }

    if (zipped) {
        // Create temporary directory for packing
        if (!zipDirInt.exists()) {
            QMessageBox::critical(
                0, tr("Critical Save Error"),
                tr("Couldn't access zipDir %1\n").arg(zipDirInt.path()));
            isSavingInt = false;
            return;
        }

        saveFilePath = filePath;
        setFilePath(zipDirInt.path() + "/" + mapName + ".xml", saveFilePath);
    } // zipped

    // Notification, that we start to save
    mainWindow->statusMessage(tr("Saving  %1...").arg(saveFilePath));

    // Create mapName and fileDir
    makeSubDirs(fileDir);

    // Real saving (not exporting) needs to save also temporary hidden parts
    TreeItem::HideTmpMode hideModeOrg = hideMode;
    setHideTmpMode(TreeItem::HideNone);

    QString mapStringData;
    if (savemode == File::CompleteMap || selModel->selection().isEmpty()) {
        // Save complete map    // FIXME-3 prefix still needed? all treeItems from all models have unique number in filename already...
        if (zipped)
            // Use defined name for map within zipfile to avoid problems
            // with zip library and umlauts (see #98)
            mapStringData =
                saveToDir(fileDir, "", FlagRowMaster::UsedFlags, QPointF(), nullptr);
        else
            mapStringData = saveToDir(fileDir, mapName + "-", FlagRowMaster::UsedFlags,
                                 QPointF(), nullptr);
        mapChanged = false;
        mapUnsaved = false;
        autosaveTimer->stop();
    }
    else {
        // Save part of map
        if (selectionType() == TreeItem::Image)
            saveImage();
        else
            mapStringData = saveToDir(fileDir, mapName + "-", FlagRowMaster::UsedFlags,
                                 QPointF(), getSelectedBranch());
        // FIXME-3 take care of multiselections when saving parts
    }

    // Restore original mode
    setHideTmpMode(hideModeOrg);

    QString saveFileName;
    if (zipped)
        // Use defined map name "map.xml", if zipped. Introduce in 2.6.6
        saveFileName = fileDir + "/map.xml";
    else
        // Use regular mapName, when saved as XML
        saveFileName = fileDir + "/" + mapFileName;

    if (!saveStringToDisk(saveFileName, mapStringData)) {
        qWarning("ME::saveStringToDisk failed!");
        err = File::Aborted;
    }

    if (err != File::Success)
        mainWindow->statusMessage(tr("Couldn't save ").arg(saveFilePath));
    else {
        if (useActionLog) {
            QString log = QString("Wrote unzipped map \"%1\" into zipDirInt = %2")
                .arg(mapFileName, zipDirInt.path());
            logInfo(log, __func__);
        }

        if (zipped) {
            // zip
            mainWindow->statusMessage(tr("Compressing %1").arg(destPath), 0);

            zipAgent = new ZipAgent(zipDirInt, destPath);
            connect(zipAgent, SIGNAL(zipFinished()), this, SLOT(zipFinished()));
            QString log = QString("Starting zipAgent to compress \"%1\" in zipDirInt = %2")
                .arg(mapFileName, zipDirInt.path());
            logInfo(log, __func__);
            zipAgent->startZip();
        } else
            mainWindow->statusMessage(tr("Saved %1").arg(saveFilePath));

        // Restore original filepath outside of tmp zip dir
        setFilePath(saveFilePath);
    }

    updateActions();

    if (!zipped)
        isSavingInt = false;
}

bool VymModel::isSaving()
{
    return isSavingInt;
}

void VymModel::zipFinished()
{
    // Cleanup
    QString log = QString("Finished zipping %1 to %2").arg(zipAgent->zipDir().path(), zipAgent->zipName());
    logInfo(log, __func__);

    zipAgent->deleteLater();
    zipAgent = nullptr;
    isSavingInt = false;

    mainWindow->statusMessage(tr("Saved %1").arg(filePath));

    fileChangedTime = QFileInfo(destPath).lastModified();

    updateActions();

}

ImageItem* VymModel::loadImage(BranchItem *parentBranch, const QStringList &imagePaths)
{
    if (!parentBranch)
        parentBranch = getSelectedBranch();

    if (parentBranch) {
        if (!imagePaths.isEmpty()) {
	    ImageItem *ii = nullptr;
            lastImageDir.setPath(
                imagePaths.first().left(imagePaths.first().lastIndexOf("/")));
            QString s;
            for (int j = 0; j < imagePaths.count(); j++) {
                s = imagePaths.at(j);
                ii = createImage(parentBranch);
                if (ii && ii->load(s)) {
                    
                    ImageContainer *ic = ii->getImageContainer();
                    QPointF pos_new = parentBranch->getBranchContainer()->getPositionHintNewChild(ic);
                    ic->setPos(pos_new);

                    QString bv = setBranchVar(parentBranch);
                    QString uc = setImageVar(ii) + "map.removeImage(i);";
                    QString rc = bv + "b.loadBranchInsert(\"REDO_PATH\", 0);";
                    saveState(
                            uc, rc,
                            QString("load image %1").arg(ii->getOriginalFilename()),
                            nullptr,
                            ii);
                }
                else {
                    qWarning() << QString("vymmodel: Failed to load '%1'").arg(s);
                    deleteItem(ii);
                    return nullptr;
                }
            }

	    reposition();
	    return ii;	// When pasting we need the last added image for scaling
        }
    }
    return nullptr;
}

ImageItem* VymModel::loadImage(BranchItem *parentBranch, const QString &imagePath)
{
    QStringList imagePaths;
    imagePaths << imagePath;
    return loadImage(parentBranch, imagePaths);
}

void VymModel::saveImage(ImageItem *ii, QString fn)
{
    if (!ii)
        ii = getSelectedImage();
    if (ii) {
        QString filter = QString(
            tr("Images") +
            " (*.png *.bmp *.xbm *.jpg *.png *.xpm *.gif *.pnm *.svg);;" +
            tr("All", "Filedialog") + " (*.*)");
        if (fn.isEmpty())
            fn = QFileDialog::getSaveFileName(
                nullptr, vymName + " - " + tr("Save image"), lastImageDir.path(),
                filter, nullptr, QFileDialog::DontConfirmOverwrite);

        if (!fn.isEmpty()) {
            lastImageDir.setPath(fn.left(fn.lastIndexOf("/")));
            if (QFile(fn).exists()) {
                QMessageBox mb(
                   QMessageBox::Warning,
                   vymName,
                   tr("The file %1 exists already.\n"
                      "Do you want to overwrite it?")
                       .arg(fn));
                mb.addButton(
                    tr("Overwrite"),
                    QMessageBox::AcceptRole);
                mb.addButton(
                    tr("Cancel"),
                    QMessageBox::RejectRole);
                mb.exec();
                if (mb.result() != QMessageBox::AcceptRole)
                    return;
            }
            if (!ii->saveImage(fn))
                QMessageBox::critical(0, tr("Critical Error"),
                                      tr("Couldn't save %1").arg(fn));
            else
                mainWindow->statusMessage(tr("Saved %1").arg(fn));
        }
    }
}

void VymModel::importDirInt(QDir d, BranchItem *dst)
{
    bool oldSaveState = saveStateBlocked;
    saveStateBlocked = true;
    BranchItem *bi = dst;
    if (bi) {
        int beginDepth = bi->depth();

        d.setFilter(QDir::AllEntries | QDir::Hidden);
        QFileInfoList list = d.entryInfoList();
        QFileInfo fi;

        QColor dirColor;
        QColor fileColor;
        if (usingDarkTheme) {
            dirColor = QColor(0, 170, 255); // #00aaff
            fileColor = QColor(255, 255, 255);
        } else {
            dirColor = QColor(0, 0, 255);
            fileColor = QColor(0, 0, 0);
        }

        // Traverse directories
        for (int i = 0; i < list.size(); ++i) {
            fi = list.at(i);
            if (fi.isDir() && fi.fileName() != "." && fi.fileName() != "..") {
                bi = addNewBranchInt(dst, -2);
                bi->setHeadingPlainText(fi.fileName());
                bi->setHeadingColor(dirColor);
                if (debug)
                    qDebug() << "Added subdir: " << fi.fileName();
                if (!d.cd(fi.fileName()))
                    QMessageBox::critical(
                        0, tr("Critical Import Error"),
                        tr("Cannot find the directory %1").arg(fi.fileName()));
                else {
                    // Recursively add subdirs
                    importDirInt(d, bi);
                    d.cdUp();
                }
                emitDataChanged(bi);
            }
        }

        for (int i = 0; i < list.size(); ++i) {
            fi = list.at(i);
            if (fi.isFile()) {
                bi = addNewBranchInt(dst, -2);
                bi->setHeadingPlainText(fi.fileName());
                bi->setHeadingColor(fileColor);
                if (fi.fileName().right(4) == ".vym")
                    bi->setVymLink(fi.filePath());
                emitDataChanged(bi);
            }
        }

        // Scroll at least some stuff
        if (dst->branchCount() > 1 && dst->depth() - beginDepth > 2)
            dst->toggleScroll();
    }
    saveStateBlocked = oldSaveState;
}

void VymModel::importDir(const QString &dirPath, BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        QString bv = setBranchVar(selbi);
        QString uc = bv + QString("map.loadMapReplace(\"UNDO_PATH\", b);");
        QString rc = bv + QString("b.importDir(\"%1\");").arg(dirPath);
        QString comment = QString("Import directory structure from \"%1\" to branch \"%2\"").arg(dirPath, selbi->headingText());
        saveState(uc, rc, comment, selbi);

        QDir d(dirPath);
        importDirInt(d, selbi);
    }
}

void VymModel::importDir()
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi) {
        QStringList filters;
        filters << "VYM map (*.vym)";
        QFileDialog fd;
        fd.setWindowTitle(vymName + " - " +
                          tr("Choose directory structure to import"));
        fd.setFileMode(QFileDialog::Directory);
        fd.setNameFilters(filters);
        fd.setWindowTitle(vymName + " - " +
                          tr("Choose directory structure to import"));
        fd.setAcceptMode(QFileDialog::AcceptOpen);

        if (fd.exec() == QDialog::Accepted && !fd.selectedFiles().isEmpty()) {
            importDir(fd.selectedFiles().first());
            reposition();
        }
    }
}

bool VymModel::addMapInsert(QString fpath, int insertPos, BranchItem *insertBranch)
{
    if (insertBranch) {
        // Only saveState if a branch is inserted
        // Other data like XLink is only used for undo/redo operations and currently
        // does not need a saveState
        QString bv = setBranchVar(insertBranch);
        QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
        QString rc = bv + QString("b.loadBranchInsert(\"%1\", %2);").arg(fpath).arg(insertPos);
        QString comment = QString("Add map %1 to \"%2\"").arg(fpath, insertBranch->headingText());
        saveState(uc, rc, comment, insertBranch);
    }

    if (File::Aborted != loadMap(fpath,
                File::ImportAdd,
                File::VymMap,
                0x0000,
                insertBranch,
                insertPos))
        return true;
    else
        return false;
}

bool VymModel::addMapReplace(QString fpath, BranchItem *bi)
{
    if (!bi) {
        qWarning() << "VymModel::addMapReplace No branch provided";
        return false;
    }

    QString bv = setBranchVar(bi);
    QString pbv = setBranchVar(bi->parentBranch(), "pb");
    QString uc = pbv + QString("map.loadBranchReplace(\"UNDO_PATH\", pb);");
    QString rc = bv + QString("map.loadBranchReplace(\"REDO_PATH\", b);");
    QString comment = QString("Replace \"%1\" with \"%2\"").arg(bi->headingText(), fpath);
    saveState(uc, rc, comment, bi->parentBranch(), bi);

    if (File::Aborted != loadMap(fpath,
               File::ImportReplace,
               File::VymMap,
               0x0000,
               bi))
        return true;
    else
        return false;
}

bool VymModel::removeVymLock()
{
    if (vymLock.removeLockForced()) {
        mainWindow->statusMessage(tr("Removed lockfile for %1").arg(mapName));
        setReadOnly(false);
        return true;
    } else
        return false;
}

bool VymModel::tryVymLock()
{
    // Defaults for author and host in vymLock
    QString defAuthor =
        settings
            .value("/user/name",
                   tr("unknown user", "Default for lockfiles of maps"))
            .toString();
    QString defHost = QHostInfo::localHostName();
    vymLock.setMapPath(filePath);
    vymLock.setAuthor(settings.value("/user/name", defAuthor).toString());
    if (getenv("HOST") != 0)
        vymLock.setHost(getenv("HOST"));
    else
        vymLock.setHost(defHost);

    // Now try to lock
    if (!vymLock.tryLock()) {
        if (debug)
            qDebug() << "VymModel::tryLock failed!";
        setReadOnly(true);
        if (vymLock.getState() == VymLock::LockedByOther) {
            if (restoreMode) {
                // While restoring maps, existing lockfiles will be ignored for
                // loading, but listed in a warning dialog
                ignoredLockedFiles << filePath;
                return removeVymLock();
            }
            else {
                LockedFileDialog dia;
                QString a = vymLock.getAuthor();
                QString h = vymLock.getHost();
                QString s =
                    QString(
                        tr("Map seems to be already opened in another vym "
                           "instance!\n\n "
                           "Map is locked by \"%1\" on \"%2\"\n\n"
                           "Please only delete the lockfile, if you are sure "
                           "nobody else is currently working on this map."))
                        .arg(a, h);
                dia.setText(s);
                dia.setWindowTitle(
                    tr("Warning: Map already opended", "VymModel"));
                if (dia.execDialog() == LockedFileDialog::DeleteLockfile) {
                    if (!removeVymLock()) {
                        // Could not remove existing lockfile, give up
                        QMessageBox::warning(
                            0, tr("Warning"),
                            tr("Couldn't remove lockfile for %1").arg(mapName));
                        return false;
                    }
                    if (!tryVymLock()) {
                        // Was able to remove existing lockfile, but not able to 
                        // create new one.
                        qWarning() << "VymModel::tryVymLock could not create new lockfile after removing old";
                        return false;
                    }
                }
            }
        }
        else if (vymLock.getState() == VymLock::NotWritable) {
            WarningDialog dia;
            QString s = QString(tr("Cannot create lockfile of map! "
                                   "It will be opened in readonly mode.\n\n"));
            dia.setText(s);
            dia.setWindowTitle(tr("Warning", "VymModel"));
            dia.showCancelButton(false);
            // dia.setShowAgainName("/mainwindow/mapIsLocked");
            dia.exec();
        }
        return false;
    }
    return true;
}

bool VymModel::renameMap(const QString &newPath)
// map is renamed before fileSaveAs() or from VymModelWrapper::saveSelection()
// Usually renamed back to original name again. Purpose here is to adapt the lockfile 
// new name of map.
// Internally the paths in ImageItems pointing to zipDirInt do not need to be adapted.
{
    QString oldPath = filePath;
    if (vymLock.getState() == VymLock::LockedByMyself || vymLock.getState() == VymLock::Undefined) {
        // vymModel owns the lockfile, try to create new lock
        VymLock newLock;
        newLock = vymLock;
        newLock.setMapPath(newPath);    // Resets state for newLock to "Undefined"
        if (!newLock.tryLock()) {
            qWarning() << QString("VymModel::renameMap  could not create lockfile for %1").arg(newPath);
            return false;
        }

        // Change lockfiles now
        if (!vymLock.releaseLock())
            qWarning() << "VymModel::renameMap failed to release lock for " << oldPath;
        vymLock = newLock;
        setFilePath(newPath);
        if (readonly)
            setReadOnly(false);
        return true;
    }
    qWarning() << "VymModel::renameMap failed to get lockfile. state=" << vymLock.getState();
    return false;
}

void VymModel::setReadOnly(bool b)
{
    readonly = b;
    mainWindow->updateTabName(this);
}

bool VymModel::isReadOnly() { return readonly; }

void VymModel::autosave()
{
    // Check if autosave is disabled due to testmode or current zip process
    if (testmode)
        return;

    // Check if autosave is disabled globally
    if (!mainWindow->useAutosave()) {
        // qWarning() << QString("VymModel::autosave disabled globally!  Current map: %1").arg(filePath);
        return;
    }

    if (zipAgent || isSavingInt) {
        //qDebug() << "VymModel::autosave blocked by zipAgent or ongoing save";
        return;
    }

    // Disable autosave, while we have gone back in history
    int redosAvail = undoSet.numValue(QString("/history/redosAvail"));
    if (redosAvail > 0)
        return;

    // Also disable autosave for new map without filename
    if (filePath.isEmpty()) {
        /*
        if (debug)
            qWarning() << "VymModel::autosave rejected due to missing filePath\n";
        */
        return;
    }

    if (mapUnsaved && mapChanged && !testmode) {
        if (QFileInfo(filePath).lastModified() <= fileChangedTime) {
            logInfo("Autosave starting", __func__);
            mainWindow->fileSave(this);	// FIXME-2 why redirecting via mainWindow?
        } else if (debug)
            qDebug() << "  VM::autosave  rejected, file on disk is newer than "
                        "last save.\n";
    }
}

void VymModel::fileChanged()
{
    // Check if file on disk has changed meanwhile
    if (!filePath.isEmpty()) {
        if (readonly && vymLock.getState() != VymLock::LockedByMyself) {
            // unset readonly if lockfile is gone
            // but only, if map was LockedByOther before
            if (vymLock.tryLock())
                setReadOnly(false);
        }
        else {
            // FIXME-5 We could check, if somebody else removed/replaced lockfile
            // (A unique vym ID would be needed)

            if (isSavingInt)
                return;

            QDateTime tmod = QFileInfo(filePath).lastModified();
            if (tmod > fileChangedTime) {
                // FIXME-5 VM switch to current mapeditor and finish
                // lineedits...
                QMessageBox mb(
                    QMessageBox::Question,
                    vymName,
                    tr("The file of the map  on disk has changed:\n\n"
                       "   %1\n\nDo you want to reload that map with the new "
                       "file?")
                        .arg(filePath));

                mb.addButton(
                    tr("Reload"),
                    QMessageBox::AcceptRole);
                mb.addButton(
                    tr("Ignore"),
                    QMessageBox::RejectRole);
                mb.exec();
                if (mb.result() == QMessageBox::AcceptRole) {
                    // Reload map
                    mainWindow->initProgressCounter(1);
                    loadMap(filePath);
                    mainWindow->removeProgressCounter();
                } else
                    // allow autosave to overwrite newer file!
                    fileChangedTime = tmod;
            }
        }
    }
}

void VymModel::blockReposition()
{
    repositionBlocked = true;
}

void VymModel::unblockReposition()
{
    repositionBlocked = false;
    reposition();
}

bool VymModel::isDefault() { return mapDefault; }

void VymModel::makeDefault()
{
    mapChanged = false;
    mapDefault = true;
}

bool VymModel::hasChanged() { return mapChanged; }

void VymModel::setChanged()
{
    if (!mapChanged)
        autosaveTimer->start(
            settings.value("/system/autosave/ms/", 30000).toInt());
    mapChanged = true;
    mapDefault = false;
    mapUnsaved = true;
    updateActions();
}

QString VymModel::getObjectName(TreeItem *ti)
{
    QString s;
    if (!ti)
        return QString("Error: nullptr has no name!");
    s = ti->headingPlain();
    if (s == "")
        s = "unnamed";

    return QString("%1 \"%2\"").arg(ti->getTypeName(), s);
}

void VymModel::redo()
{
    // Can we undo at all?
    if (redosAvail < 1)
        return;

    bool saveStateBlockedOrg = saveStateBlocked;
    saveStateBlocked = true;

    redosAvail--;

    if (undosAvail < stepsTotal)
        undosAvail++;
    curStep++;
    if (curStep > stepsTotal)
        curStep = 1;
    QString undoCommand =
        undoSet.value(QString("/history/step-%1/undoCommand").arg(curStep));
    QString redoCommand =
        undoSet.value(QString("/history/step-%1/redoCommand").arg(curStep));
    QString comment =
        undoSet.value(QString("/history/step-%1/comment").arg(curStep));

    /* TODO Maybe check for version, if we save the history
    if (!checkVersion(mapVersionInt))
    QMessageBox::warning(0,tr("Warning"),
        tr("Version %1 of saved undo/redo data\ndoes not match current vym
    version %2.").arg(mapVersionInt).arg(vymVersion));
    */

    if (debug) {
        qDebug() << "VymModel::redo() begin\n";
        qDebug() << "    undosAvail=" << undosAvail;
        qDebug() << "    redosAvail=" << redosAvail;
        qDebug() << "       curStep=" << curStep;
        qDebug() << "    ---------------------------";
        qDebug() << "    comment=" << comment;
        qDebug() << "    undoCom:";
        cout << qPrintable(undoCommand) << endl;
        qDebug() << "    redoCom=";
        cout << qPrintable(redoCommand) << endl;
        qDebug() << "    ---------------------------";
    }

    QString errMsg;
    QString redoScript =
        QString("map = vym.currentMap();%1").arg(redoCommand);
    errMsg = QVariant(execute(redoScript)).toString();
    saveStateBlocked = saveStateBlockedOrg;

    undoSet.setValue("/history/undosAvail", QString::number(undosAvail));
    undoSet.setValue("/history/redosAvail", QString::number(redosAvail));
    undoSet.setValue("/history/curStep", QString::number(curStep));
    undoSet.writeSettings(histPath);

    mainWindow->updateHistory(undoSet);

    updateActions();

    /* TODO remove testing
    qDebug() << "ME::redo() end\n";
    qDebug() << "    undosAvail=" << undosAvail;
    qDebug() << "    redosAvail=" << redosAvail;
    qDebug() << "       curStep=" << curStep;
    qDebug() << "    ---------------------------";
    */
}

bool VymModel::isRedoAvailable()
{
    if (undoSet.numValue("/history/redosAvail", 0) > 0)
        return true;
    else
        return false;
}

QString VymModel::lastRedoSelection()
{
    if (isUndoAvailable())
        return undoSet.value(
            QString("/history/step-%1/redoSelection").arg(curStep));
    else
        return QString();
}

QString VymModel::lastRedoCommand()
{
    if (isUndoAvailable())
        return undoSet.value(
            QString("/history/step-%1/redoCommand").arg(curStep));
    else
        return QString();
}

QVariant VymModel::repeatLastCommand()
{
    QString command = "m = vym.currentMap();";
    QString redoCommand = undoSet.value(
       QString("/history/step-%1/redoCommand").arg(curStep));
    if (isUndoAvailable() && !redoCommand.startsWith("model."))
        // Only repeat command, if not a set of commands
        command += "m." + redoCommand + ";";
    else
        return false;
    return execute(command);
}

void VymModel::undo()
{
    // Can we undo at all?
    if (undosAvail < 1)
        return;

    mainWindow->statusMessage(tr("Autosave disabled during undo."));

    bool saveStateBlockedOrg = saveStateBlocked;
    saveStateBlocked = true;

    QString undoCommand =
        undoSet.value(QString("/history/step-%1/undoCommand").arg(curStep));
    QString redoCommand =
        undoSet.value(QString("/history/step-%1/redoCommand").arg(curStep));
    QString comment =
        undoSet.value(QString("/history/step-%1/comment").arg(curStep));

    /* TODO Maybe check for version, if we save the history
    if (!checkVersion(mapVersionInt))
    QMessageBox::warning(0,tr("Warning"),
        tr("Version %1 of saved undo/redo data\ndoes not match current vym
    version %2.").arg(mapVersionInt).arg(vymVersion));
    */

    if (debug) {
        qDebug() << "VymModel::undo() begin\n";
        qDebug() << "    undosAvail=" << undosAvail;
        qDebug() << "    redosAvail=" << redosAvail;
        qDebug() << "       curStep=" << curStep;
        cout << "    ---------------------------" << endl;
        qDebug() << "    comment=" << comment;
        cout << "    undoCom:" << endl;
        cout << qPrintable(undoCommand) << endl;
        cout << "    redoCom:" << endl;
        cout << qPrintable(redoCommand) << endl;
        cout << "    ---------------------------" << endl;
    }

    // bool noErr;
    QString errMsg;
    QString undoScript;
    if (!undoCommand.contains("currentMap()"))
        // "Old" saveState without complete command // FIXME-2 saveState: remove finally old syntax
        undoScript = QString("map = vym.currentMap();%1").arg(undoCommand);
    else
        undoScript = undoCommand;

    // FIXME-0 errMsg = QVariant(execute(undoScript)).toString();
    errMsg = mainWindow->runScript(undoScript).toString();
    // FIXME-0 errMsg = QVariant(execute(undoScript)).toString();

    undosAvail--;
    curStep--;
    if (curStep < 1)
        curStep = stepsTotal;

    redosAvail++;

    saveStateBlocked = saveStateBlockedOrg;
    /* testing only
        qDebug() << "VymModel::undo() end\n";
        qDebug() << "    undosAvail="<<undosAvail;
        qDebug() << "    redosAvail="<<redosAvail;
        qDebug() << "       curStep="<<curStep;
        qDebug() << "    ---------------------------";
    */

    undoSet.setValue("/history/undosAvail", QString::number(undosAvail));
    undoSet.setValue("/history/redosAvail", QString::number(redosAvail));
    undoSet.setValue("/history/curStep", QString::number(curStep));
    undoSet.writeSettings(histPath);

    mainWindow->updateHistory(undoSet);

    updateActions();
}

bool VymModel::isUndoAvailable()
{
    if (undoSet.numValue("/history/undosAvail", 0) > 0)
        return true;
    return false;
}

void VymModel::gotoHistoryStep(int i)
{
    // Restore variables
    int undosAvail = undoSet.numValue(QString("/history/undosAvail"));
    int redosAvail = undoSet.numValue(QString("/history/redosAvail"));

    if (i < 0)
        i = undosAvail + redosAvail;

    // Clicking above current step makes us undo things
    if (i < undosAvail) {
        for (int j = 0; j < undosAvail - i; j++)
            undo();
        return;
    }
    // Clicking below current step makes us redo things
    if (i > undosAvail)
        for (int j = undosAvail; j < i; j++) {
            if (debug)
                qDebug() << "VymModel::gotoHistoryStep redo " << j << "/"
                         << undosAvail << " i=" << i;
            redo();
        }

    // And ignore clicking the current row ;-)
}

QString VymModel::getHistoryPath()
{
    QString histName(QString("history-%1").arg(dataStep));
    return (tmpMapDirPath + "/" + histName);
}

void VymModel::resetHistory()
{
    curStep = 0;
    redosAvail = 0;
    undosAvail = 0;
    dataStep = 0;

    stepsTotal = settings.value("/history/stepsTotal", 100).toInt();
    undoSet.setValue("/history/stepsTotal", QString::number(stepsTotal));
    mainWindow->updateHistory(undoSet);
}

QString VymModel::setAttributeVar(AttributeItem* ai, QString varName)
{
    // Default varName: "a"
    QString r;
    if (!ai)
        qWarning() << "VM::setAttributeVar ai == nullptr";
    else

        r = QString("%1 = map.findAttributeById(\"%2\");").arg(varName, ai->getUuid().toString());

    return r;
}

QString VymModel::setBranchVar(BranchItem* bi, QString varName)
{
    // Default varName: "b"
    QString r;
    if (!bi)
        qWarning() << "VM::setBranchVar bi == nullptr";
    else
        r = QString("%1 = map.findBranchById(\"%2\");").arg(varName, bi->getUuid().toString());

    return r;
}

QString VymModel::setImageVar(ImageItem* ii, QString varName)
{
    // Default varName: "i"
    QString r;
    if (!ii)
        qWarning() << "VM::setImageVar ii == nullptr";
    else
        r = QString("%1 = map.findImageById(\"%2\");").arg(varName, ii->getUuid().toString());

    return r;
}

QString VymModel::setXLinkVar(XLink* xl, QString varName)
{
    QString r;
    if (!xl)
        qWarning() << "VM::setXLinkVar xl == nullptr";
    else
        r = QString("%1 = map.findXLinkById(\"%2\");").arg(varName, xl->getUuid().toString());

    return r;
}

// FIXME-2 saveState: Check VymModelWrapper vs BranchWrapper  in scripts...
void VymModel::saveState(
         QString undoCommand,
         QString redoCommand,
         const QString &comment,
         TreeItem *saveUndoItem,
         TreeItem *saveRedoItem)
{
    // Main saveState

    // sendData(redoCom); // FIXME-5 testing network

    if (saveStateBlocked)
        return;

    /*
    if (debug) {
        qDebug() << "VM::saveState() for map " << mapName;
        qDebug() << "  comment: " << comment;
        qDebug() << "  Script:   " << buildingUndoScript;
        qDebug() << "  undoCom: " << undoCommand;
        qDebug() << "  redoCom: " << redoCommand;
    }
    */

    if (buildingUndoScript)
        logCommand("// Building script: " + redoCommand, comment, __func__);
    else
        logCommand(redoCommand, comment, __func__);

    // Increase undo steps, but check for repeated actions
    // like editing a vymNote - then do not increase but replace last command
    //
    bool repeatedCommand = false;

    /* FIXME-3 Repeated command not supported yet in saveState
    // Undo Scripts start with "model.select" - do not consider these for repeated actions
    if (!undoCommand.startsWith("{")) {
        if (curStep > 0 && redoSelection == lastRedoSelection()) {
            int i = redoCommand.indexOf("(");
            QString rcl = redoCommand.left(i-1);
            if (i > 0 && rcl == lastRedoCommand().left(i-1)) {

                // Current command is a repeated one. We only want to "squash" some of these
                QRegularExpression re("<vymnote");
                if (rcl.startsWith("model.parseVymText") && re.match(redoCommand).hasMatch()) {
                    if (debug)
                        qDebug() << "VM::saveState repeated command: " << redoCommand;

                    // Do not increase undoCommand counter
                    repeatedCommand = true;
                    undoCommand = undoSet.value(
                        QString("/history/step-%1/undoCommand").arg(curStep), undoCommand);
                } else
                    if (debug)
                        qDebug() << "VM::saveState not repeated command: " << redoCommand;
            }
        }
    }
    */
    QString histDir = getHistoryPath();

    // Create histDir if not available and required
    if (saveUndoItem || saveRedoItem) {
        dataStep++;
        QDir d(histDir);
        if (!d.exists())
            makeSubDirs(histDir);
    }

    // Save depending on how much needs to be saved
    //
    // FIXME-5 saveState: userFlags are not written, but still in memory. Could
    //         lead to problem, if one day removed from userFlags toolbar AND memory
    if (saveUndoItem) {
        QString dataXML = saveToDir(histDir, mapName + "-", FlagRowMaster::NoFlags, QPointF(),
                            saveUndoItem);

        QString xmlUndoPath = histDir + "/undo.xml";
        undoCommand.replace("UNDO_PATH", xmlUndoPath);
        saveStringToDisk(xmlUndoPath, dataXML);
    }
    if (saveRedoItem) {
        QString dataXML = saveToDir(histDir, mapName + "-", FlagRowMaster::NoFlags, QPointF(),
                            saveRedoItem);

        QString xmlRedoPath = histDir + "/redo.xml";
        redoCommand.replace("REDO_PATH", xmlRedoPath);
        saveStringToDisk(xmlRedoPath, dataXML);
    }

    if (debug) {
        qDebug() << "  undoCommand: " << undoCommand;
        qDebug() << "  redoCommand: " << redoCommand;
        qDebug() << "      Comment: " << comment;
    }

    if (buildingUndoScript)
    {
        // Build string with all commands
        undoScript = undoCommand + undoScript;
        redoScript = redoScript + redoCommand;

        if (debug) {
            qDebug() << "VM::saveState  building scripts:";
            qDebug() << "  undoScript = " << undoScript;
            qDebug() << "  redoScript = " << redoScript;
        }
        return;
    }

    if (!repeatedCommand) {
        if (undosAvail < stepsTotal)
            undosAvail++;

        curStep++;
        if (curStep > stepsTotal)
            curStep = 1;
    }

    // We would have to save all actions in a tree, to keep track of
    // possible redos after an action. Possible, but we are too lazy: forget
    // about redos
    redosAvail = 0;

    // Write the current state to disk
    undoSet.setValue("/history/undosAvail", QString::number(undosAvail));
    undoSet.setValue("/history/redosAvail", QString::number(redosAvail));
    undoSet.setValue("/history/curStep", QString::number(curStep));
    undoSet.setValue(QString("/history/step-%1/undoCommand").arg(curStep),
                     undoCommand);
    undoSet.setValue(QString("/history/step-%1/redoCommand").arg(curStep),
                     redoCommand);
    undoSet.setValue(QString("/history/step-%1/comment").arg(curStep), comment);
    undoSet.writeSettings(histPath);

    /*
    if (debug) {
        // qDebug() << "          into="<< histPath;
        qDebug() << "    stepsTotal=" << stepsTotal
                 << ", undosAvail=" << undosAvail
                 << ", redosAvail=" << redosAvail << ", curStep=" << curStep;
        cout << "    ---------------------------" << endl;
        qDebug() << "    comment=" << comment;
        qDebug() << "    undoSel=" << undoSelection;
        qDebug() << "    redoSel=" << redoSelection;
        if (saveSel)
            qDebug() << "    saveSel=" << qPrintable(getSelectString(saveSel));
        cout << "    undoCom:" <<  qPrintable(undoCommand) << "\n";
        cout << "    redoCom:" <<  qPrintable(redoCommand) << "\n";
        cout << "    ---------------------------\n";
    }
    */

    mainWindow->updateHistory(undoSet);

    setChanged();
}

void VymModel::saveStateBranch(
        BranchItem *bi,
        const QString &uc,
        const QString &rc,
        const QString &comment)
{
    QString prefix = setBranchVar(bi) + "b.";
    saveState(prefix + uc, prefix + rc, comment);
}

void VymModel::saveStateBeginScript(const QString &comment)
{
    buildingUndoScript = true;
    undoScriptComment = comment;
    undoScript.clear();
    redoScript.clear();
}

void VymModel::saveStateEndScript()
{
    if (debug)
        std::cout << "VM::saveStateEndScript  buildingScript=" << buildingUndoScript << " undoScript=" << undoScript.toStdString() << endl;

    if (buildingUndoScript) {
        buildingUndoScript = false;

        // Drop whole Script, if empty
        if (undoScript.isEmpty() && redoScript.isEmpty()) return;

        saveState(
                QString("{%1}").arg(undoScript),
                QString("{%1}").arg(redoScript),
                undoScriptComment, nullptr);
    }
}

QGraphicsScene *VymModel::getScene() { return mapEditor->getScene(); }

TreeItem *VymModel::findBySelectString(QString s)
{
    if (s.isEmpty())
        return nullptr;

    // Old maps don't have multiple mapcenters and don't save full path
    if (s.left(2) != "mc")
        s = "mc:0," + s;

    QStringList parts = s.split(",");
    QString typ;
    int n;
    TreeItem *ti = rootItem;

    while (!parts.isEmpty()) {
        typ = parts.first().left(2);
        n = parts.first().right(parts.first().length() - 3).toInt();
        parts.removeFirst();
        if (typ == "mc" || typ == "bo")
            ti = ti->getBranchNum(n);
        else if (typ == "fi")
            ti = ti->getImageNum(n);
        else if (typ == "ai")
            ti = ti->getAttributeNum(n);
        else if (typ == "xl")
            ti = ti->getXLinkItemNum(n);
        if (!ti)
            return nullptr;
    }
    return ti;
}

TreeItem *VymModel::findID(const uint &id)
{
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        if (id == cur->getID())
            return cur;
        int j = 0;
        while (j < cur->xlinkCount()) {
            XLinkItem *xli = cur->getXLinkItemNum(j);
            if (id == xli->getID())
                return xli;
            j++;
        }
        j = 0;
        while (j < cur->imageCount()) {
            ImageItem *ii = cur->getImageNum(j);
            if (id == ii->getID())
                return ii;
            j++;
        }
        j = 0;
        while (j < cur->attributeCount()) {
            AttributeItem *ai = cur->getAttributeNum(j);
            if (id == ai->getID())
                return ai;
            j++;
        }
        nextBranch(cur, prev);
    }
    return nullptr;
}

TreeItem *VymModel::findUuid(const QUuid &id)
{
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        if (id == cur->getUuid())
            return cur;
        int j = 0;
        while (j < cur->xlinkCount()) {
            XLinkItem *xli = cur->getXLinkItemNum(j);
            if (id == xli->getUuid())
                return xli;
            j++;
        }
        j = 0;
        while (j < cur->imageCount()) {
            ImageItem *ii = cur->getImageNum(j);
            if (id == ii->getUuid())
                return ii;
            j++;
        }
        nextBranch(cur, prev);
    }

    // Special case: XLinks (not XLinkItems!) are no TreeItem, if Uuid matches,
    // return one of the XLinkItems. Used in VymModelWrapper::findXLinkById
    foreach (auto xl, xlinks)
        if (xl->getUuid() == id)
            return xl->beginXLinkItem();

    // For restoring MapCenters we might want to add to rootItem
    if (rootItem->getUuid() == id)
        return rootItem;

    return nullptr;
}

BranchItem* VymModel::findBranchByAttribute(const QString &key, const QString &value)
{
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        AttributeItem *ai = cur->getAttributeByKey(key); 
        if (ai && ai->value().toString() == value)
            return cur;
        nextBranch(cur, prev);
    }

    return nullptr;
}

void VymModel::test()
{
    // Print item structure
    foreach (TreeItem *ti, getSelectedItems()) {
        if (ti->hasTypeBranch()) {
            BranchContainer *bc = ((BranchItem*)ti)->getBranchContainer();
            bc->printStructure();
        }
        if (ti->hasTypeImage())
            ((ImageItem*)ti)->parentBranch()->getBranchContainer()->printStructure();
    }
    return;

    // Do animation step. All BranchContainers
    QList <BranchContainer*> bc_list;

    qDebug() << "Calculating forces...";
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        //qDebug() << "Adding branch: " << cur->headingText();
        BranchContainer *bc = cur->getBranchContainer();
        bc->v_anim = QPointF(0,0);
        bc_list << bc;
        nextBranch(cur, prev);
    }

    foreach (BranchContainer *bc, bc_list) {
        HeadingContainer *hc = bc->getHeadingContainer();
        HeadingContainer *ohc;

        // Forces pushing apart
        /*
        */
        foreach (BranchContainer *obc, bc_list) {
            if (bc != obc) {
                
                ohc = obc->getHeadingContainer();

                QPointF vec = hc->mapFromItem(ohc, ohc->pos());
                qreal dx = vec.x();
                qreal dy = vec.y();
                double l = 2.0 * (dx * dx + dy * dy);

                if (l > 25) {
                    bc->v_anim += QPointF(- (dx *150) / l, - (dy * 150) / l);
                    qDebug() << "Push "<< hc->info() << " <- " << ohc->info() << " vec=" << toS(vec) << " l=" << l;
                }
            }
        }

        // Forces pulling together
        BranchItem *bi = bc->getBranchItem();
        double weight = (bi->branchCount() + 1) * 10;

        /*
        for (int i = 0; i < bi->branchCount(); i++) {
            BranchItem *obi = bi->getBranchNum(i);
            BranchContainer *obc = obi->getBranchContainer();
            ohc = obc->getHeadingContainer();

            // Parent pulled by child
            QPointF vec = hc->mapFromItem(ohc, ohc->pos());
            bc->v_anim += QPointF( vec.x() / weight, vec.y() / weight);
            qDebug() << "  Child Pull  from " << obi->headingText() << " to " << bi->headingText() << toS(vec);

            // Child pulled by parent
            vec = ohc->mapFromItem(hc, ohc->pos());
            obc->v_anim += QPointF( vec.x() / weight, vec.y() / weight);
            qDebug() << "  Parent Pull from " << bi->headingText() << " to " << obi->headingText() << toS(vec);
        }
        */

        // Move MapCenters towards center
        if (bi->depth() == 0) {
            QPointF vec = hc->mapToScene(QPointF(0,0));
            qreal dx = vec.x();
            qreal dy = vec.y();
            double l = sqrt( dx * dx + dy * dy);
            if (l > 5) {
                bc->v_anim += QPointF(- (dx ) / l, - (dy ) / l);
                qDebug() << "Moving to center: " << bc->info() << "l=" << l;
            }
        }

        // Ignore too small vector
        if (qAbs(bc->v_anim.x()) < 0.1 && qAbs(bc->v_anim.y()) < 0.1)
            bc->v_anim = QPointF(0, 0);
    }

    foreach (BranchContainer *bc, bc_list) {
        // Show vector
        bc->v.setLine(0, 0, bc->v_anim.x() * 10, bc->v_anim.y() * 10);

        // Now actually move items
        bc->setPos( bc->pos() + bc->v_anim);
    }

    reposition();
    return;


    //mapEditor->testFunction1();
    //return;

    // Read bookmarks
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(nullptr, "QXmlStream Bookmarks",
                QString("Cannot read file %1:\n%2.")
                .arg(QDir::toNativeSeparators(fileName),
                    file.errorString()));
        return;
    }

    VymReader reader(this);
    if (!reader.read(&file)) {
        QMessageBox::warning(nullptr, QString("QXmlStream Bookmarks"),
                QString("Parse error in file %1:\n\n%2")
                .arg(QDir::toNativeSeparators(fileName),
                    reader.errorString()));
    } else {
        mainWindow->statusMessage("File loaded");
        reposition();
    }

}

//////////////////////////////////////////////
// Interface
//////////////////////////////////////////////
    
void VymModel::setTitle(const QString &s)
{
    if (title != s) {
        saveState(QString("map.setTitle (\"%1\");").arg(title),
                  QString("map.setTitle (\"%1\");").arg(s),
                  QString("Set title of map to \"%1\"").arg(s));
        title = s;
    }
}

QString VymModel::getTitle() { return title; }

void VymModel::setAuthor(const QString &s)
{
    if (author != s) {
        saveState(QString("map.setAuthor (\"%1\");").arg(author),
                  QString("map.setAuthor (\"%1\");").arg(s),
                  QString("Set author of map to \"%1\"").arg(s));
        author = s;
    }
}

QString VymModel::getAuthor() { return author; }

void VymModel::setComment(const QString &s)
{
    if (comment != s) {
        saveState(QString("map.setComment (\"%1\")").arg(comment),
                  QString("map.setComment (\"%1\")").arg(s),
                  QString("Set comment of map"));
        comment = s;
    }
}

QString VymModel::getComment() { return comment; }

void VymModel::setMapVersion(const QString &s)
{
    // Version stored in file
    mapVersionInt = s;
}

QString VymModel::mapVersion()
{
    return mapVersionInt;
}

int VymModel::branchCount()
{
    int c = 0;
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        c++;
        nextBranch(cur, prev);
    }
    return c;
}

int VymModel::centerCount() { return rootItem->branchCount(); }

void VymModel::setSortFilter(const QString &s)
{
    sortFilter = s;
    emit sortFilterChanged(sortFilter);
}

QString VymModel::getSortFilter() { return sortFilter; }

void VymModel::setHeading(const VymText &vt, TreeItem *ti)
{
    Heading h_old;
    Heading h_new;
    h_new = vt;
    QString s = vt.getTextASCII();

    TreeItem *selti = getSelectedItem(ti);

    if (selti && selti->hasTypeBranchOrImage()) {
        h_old = selti->heading();
        if (h_old == h_new)
            return;

        QString tiv;    // ti variable in script
        if (selti->hasTypeBranch())
            tiv = setBranchVar((BranchItem*)selti) + "b.";
        else
            tiv = setImageVar((ImageItem*)selti) + "i.";

        QString uc, rc;
        if (h_old.isRichText())
            uc = QString("%1setHeadingRichText(\"%2\");").arg(tiv, quoteQuotes(h_old.getText()));
        else
            uc = QString("%1setHeadingText(\"%2\");").arg(tiv, quoteQuotes(h_old.getText()));
        if (h_new.isRichText())
            rc = QString("%1setHeadingRichText(\"%2\");").arg(tiv, quoteQuotes(h_new.getText()));
        else
            rc = QString("%1setHeadingText(\"%2\");").arg(tiv, quoteQuotes(h_new.getText()));
        saveState( uc, rc, QString("Set heading of %1 to \"%2\"").arg(getObjectName(selti), s));

        // After adding branches or MapCenters interactively we might want to end an undo script
        saveStateEndScript();

        selti->setHeading(vt);
        emitDataChanged(selti);
        emitUpdateQueries();
        mainWindow->updateHeadingEditor(selti);    // Update HeadingEditor with new heading (if required)
        reposition();
    } else
        qWarning() << "VM::setHeading has no branch or image selected!";
}

void VymModel::setHeadingPlainText(const QString &s, TreeItem *ti)
{
    TreeItem *selti = getSelectedItem(ti);
    if (selti && selti->hasTypeBranchOrImage()) {
        VymText vt = selti->heading();
        vt.setPlainText(s);
        if (selti->heading() == vt)
            return;
        setHeading(vt, selti);

        // Set URL
        if (selti->hasTypeBranch()) {
            BranchItem *bi = (BranchItem*)selti;

            if ((s.startsWith("http://") || s.startsWith("https://")) && !bi->hasUrl())
                    setUrl(s, true, bi);
        }
    }
}

QString VymModel::headingText(TreeItem *ti)
{
    // Mainly used for debugging, also works with nullptr
    if (ti)
        return ti->headingPlain();
    else
        return QString("No treeItem available");
}

void VymModel::updateNoteText(const VymText &vt)
{
    VymNote note_new(vt);
    setNote(note_new, nullptr, true);
}

void VymModel::setNote(const VymNote &note_new, BranchItem *bi, bool senderIsNoteEditor)
{
    BranchItem *selbi = getSelectedBranch(bi);
    //qDebug() << "VM::setNote  selbi=" << selbi->headingText() << " n=" << note_new.getText();
    if (selbi) {
        VymNote note_old;
        note_old = selbi->getNote();
        if (note_old == note_new)
            return;

        bool editorStateChanged = false;
        if (note_new.getText() != note_old.getText()) {
            if ((note_new.isEmpty() && !note_old.isEmpty()) ||
                (!note_new.isEmpty() && note_old.isEmpty()))
                editorStateChanged = true;
        }

        //qDebug() << "VM::setNote  selbi=" << selbi->headingText() << " n=" << note_new.getText();

        // branch variable in script
        QString bv = setBranchVar(selbi) + "b.";

        QString uc, rc;
        if (note_old.isRichText())
            uc = QString("%1setNoteRichText(\"%2\");").arg(bv, quoteQuotes(note_old.getText()));
        else
            uc = QString("%1setNoteText(\"%2\");").arg(bv, quoteQuotes(note_old.getText()));
        if (note_new.isRichText())
            rc = QString("%1setNoteRichText(\"%2\");").arg(bv, quoteQuotes(note_new.getText()));
        else
            rc = QString("%1setNoteText(\"%2\");").arg(bv, quoteQuotes(note_new.getText()));

        saveState( uc, rc, QString("Set note of %1 to \"%2\"")
                .arg(getObjectName(selbi), note_new.getTextASCII().left(40)));

        selbi->setNote(note_new);
        if (!senderIsNoteEditor)
            emitNoteChanged(selbi);

        emitDataChanged(selbi);

        // Only update flag, if state has changed
        if (editorStateChanged)
            reposition();

    }
}

bool VymModel::loadNote(const QString &fn, BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        QString n;
        if (!loadStringFromDisk(fn, n))
            qWarning() << QString("VymModel::loadNote Couldn't load '%1'").arg(fn);
        else {
            VymNote vn;
            vn.setAutoText(n);
            setNote(vn);
            emitDataChanged(selbi);
            emitUpdateQueries();
            reposition();
            return true;
        }
    }
    else
        qWarning("VymModel::loadNote no branch selected");
    return false;
}

bool VymModel::saveNote(const QString &fn)
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi) {
        VymNote n = selbi->getNote();
        if (n.isEmpty())
            qWarning() << "VymModel::saveNote  note is empty, won't save to "
                       << fn;
        else {
            if (!saveStringToDisk(fn, n.saveToDir()))
                qWarning() << "VymModel::saveNote Couldn't save " << fn;
            else
                return true;
        }
    }
    else
        qWarning("VymModel::saveNote no branch selected");
    return false;
}

void VymModel::findDuplicateURLs() // FIXME-3 Feature needs GUI for viewing
{
    // Generate multimap containing _all_ URLs and branches
    QMultiMap<QString, BranchItem *> multimap;
    QStringList urls;
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        QString u = cur->url();
        if (!u.isEmpty()) {
            multimap.insert(u, cur);
            if (!urls.contains(u))
                urls << u;
        }
        nextBranch(cur, prev);
    }

    // Extract duplicate URLs
    foreach (auto u, urls) {
        if (multimap.values(u).size() > -1) {
            qDebug() << "URL: " << u;
            foreach(auto *bi, multimap.values(u))
                qDebug() << " - " << bi->headingPlain();
        }
    }
}

bool VymModel::findAll(FindResultModel *rmodel, QString s,
                       Qt::CaseSensitivity cs, bool searchNotes)
{
    rmodel->clear();
    rmodel->setSearchString(s);
    rmodel->setSearchFlags(QTextDocument::FindFlags()); // FIXME-4 translate cs to
                               // QTextDocument::FindFlag
    bool hit = false;

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);

    FindResultItem *lastParent = nullptr;
    while (cur) {
        lastParent = nullptr;
        if (cur->heading().getTextASCII().contains(s, cs)) {
            lastParent = rmodel->addItem(cur);
            hit = true;
        }

        if (searchNotes) {
            QString n = cur->getNoteASCII();
            int i = 0;
            int j = 0;
            while (i >= 0) {
                i = n.indexOf(s, i, cs);
                if (i >= 0) {
                    // If not there yet, add "parent" item
                    if (!lastParent) {
                        lastParent = rmodel->addItem(cur);
                        hit = true;
                        if (!lastParent)
                            qWarning()
                                << "VymModel::findAll still no lastParent?!";
                        /*
                        else
                            lastParent->setSelectable (false);
                        */
                    }

                    // save index of occurence
                    n.replace('\n', ' ');
                    rmodel->addSubItem(
                        lastParent,
                        QString(tr("Note", "FindAll in VymModel") +
                                ": \"...%1...\"")
                            .arg(n.mid(i - 8, 80)),
                        cur, j);
                    j++;
                    i++;
                }
            }
        }
        nextBranch(cur, prev);
    }
    return hit;
}

void VymModel::setUrl(QString url, bool updateFromCloud, BranchItem *bi)    // FIXME-2 does not update flag when unsetting Url
{
    if (!bi) bi = getSelectedBranch();
    if (bi->url() == url)
        return;

    if (bi) {
        QString oldurl = bi->url();
        bi->setUrl(url);
        if (!saveStateBlocked) {
            QString uc = QString("setUrl(\"%1\");").arg(oldurl);
            QString rc = QString("setUrl(\"%1\");").arg(url);
            saveStateBranch(bi, uc, rc,
                QString("set URL of %1 to %2").arg(getObjectName(bi), url));
        }
        if (!url.isEmpty()) {
            if (updateFromCloud) {    // FIXME-3 use oembed.com also for Youtube and other cloud providers
                // Check for Jira
                JiraAgent agent;
                if (agent.setTicket(url)) {
                    setAttribute(bi, "Jira.key", agent.key());

                    // Initially set heading with ticket Id
                    setHeading(agent.key(), bi);

                    // Then try to update heading from Jira
                    getJiraData(false, bi);
                }

                // Check for Confluence
                if (bi->urlType() != TreeItem::JiraUrl)
                    setConfluencePageDetails(false);
            }
        } else {
            // url == ""
            AttributeItem *ai = getAttributeByKey("Jira.issueUrl", bi);
            if (ai)
                deleteItem(ai);
        }
        updateJiraFlag(bi); // Implicitly calls setUrlType()


        emitDataChanged(bi);
        if (!repositionBlocked)
            reposition();
    }
}

QString VymModel::getUrl()
{
    TreeItem *selti = getSelectedItem();
    if (selti)
        return selti->url();
    else
        return QString();
}

QStringList VymModel::getUrls(bool ignoreScrolled)
{
    QStringList urls;
    BranchItem *selbi = getSelectedBranch();
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev, true, selbi);
    while (cur) {
        if (cur->hasUrl() &&
            !(ignoreScrolled && cur->hasScrolledParent()))
            urls.append(cur->url());
        nextBranch(cur, prev, true, selbi);
    }
    return urls;
}

void VymModel::setJiraQuery(const QString &query_new, BranchItem *bi)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem *bi, selbis)
        if (query_new.isEmpty())
            deleteAttribute(bi, "Jira.query");
        else
            setAttribute(bi, "Jira.query", query_new);
}

void VymModel::setFrameAutoDesign(const bool &useInnerFrame, const bool &b) // FIXME-2 missing saveState
{
    QList<BranchItem *> selbis = getSelectedBranches();
    BranchContainer *bc;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        bc->setFrameAutoDesign(useInnerFrame, b);
        if (b && mapDesignInt->frameType(useInnerFrame, selbi->depth()) != bc->frameType(useInnerFrame))
            setFrameType(useInnerFrame, mapDesignInt->frameType(useInnerFrame, selbi->depth()), selbi);
    }
}

void VymModel::setFrameType(const bool &useInnerFrame, const FrameContainer::FrameType &t, BranchItem *bi)   // FIXME-2 update autoDesign and BranchPropertyWindow (similar for other values)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    BranchContainer *bc;
    QString oldName;
    QString newName;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        if (bc->frameType(useInnerFrame) == t)
            break;

        QString uif = toS(useInnerFrame);
        QString uc, rc;

        bool saveCompleteFrame = false;

        if (t == FrameContainer::NoFrame)
            // Save also penWidth, colors, etc. to restore frame on undo
            saveCompleteFrame = true;

        if (saveCompleteFrame) {
            saveStateBeginScript("Set frame parameters");
            QString colorName = bc->framePenColor(useInnerFrame).name();
            uc = QString("setFramePenColor (%1, \"%2\");").arg(uif, colorName);
            saveStateBranch(selbi, uc, "",
                    QString("set pen color of frame to %1").arg(colorName));

            colorName = bc->frameBrushColor(useInnerFrame).name();
            uc = QString("setFrameBrushColor (%1, \"%2\");").arg(uif, colorName);
            saveStateBranch(bi, uc, "",
                    QString("set background color of frame to %1").arg(colorName));

            int i = bc->framePenWidth(useInnerFrame);
            uc = QString("setFramePenWidth (%1, \"%2\");").arg(uif).arg(i);
            saveStateBranch(selbi, uc, "",
                      QString("set pen width of frame to %1").arg(i));

            i = bc->framePadding(useInnerFrame);
            uc = QString("setFramePadding (%1, \"%2\");").arg(uif, i);
            saveStateBranch(selbi, uc, "",
                QString("set padding of frame to %1").arg(i));
        }

        oldName = bc->frameTypeString(useInnerFrame);
        bc->setFrameType(useInnerFrame, t);
        newName = bc->frameTypeString(useInnerFrame);

        uc = QString("setFrameType(%1, \"%2\");").arg(uif, oldName);
        rc = QString("setFrameType(%1, \"%2\");").arg(uif, newName);
        saveStateBranch(selbi, uc, rc,
            QString("set type of frame to %1").arg(newName));

        if (saveCompleteFrame)
            saveStateEndScript();

        emitDataChanged(selbi);  // Notify HeadingEditor to eventually change BG color
    }
    reposition();
}

void VymModel::setFrameType(const bool &useInnerFrame, const QString &s, BranchItem *bi)
{
    setFrameType(useInnerFrame, FrameContainer::frameTypeFromString(s), bi);
}

void VymModel::setFramePenColor(const bool &useInnerFrame, const QColor &col, BranchItem *bi)

{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
        if (bc->frameType(useInnerFrame) != FrameContainer::NoFrame)  {
            QString uif = toS(useInnerFrame);
            QString colorNameOld = bc->framePenColor(useInnerFrame).name();
            QString uc = QString("setFramePenColor (%1, \"%2\");").arg(uif, colorNameOld);
            QString colorNameNew = col.name();
            QString rc = QString("setFramePenColor (%1, \"%2\");").arg(uif, colorNameNew);
            saveStateBranch(selbi, uc, rc,
                    QString("set pen color of frame to %1").arg(colorNameNew));

            bc->setFramePenColor(useInnerFrame, col);
        }
    }
}

void VymModel::setFrameBrushColor(
    const bool &useInnerFrame, const QColor &col, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
        if (bc->frameType(useInnerFrame) != FrameContainer::NoFrame)  {
            QString uif = toS(useInnerFrame);
            QString colorNameOld = bc->framePenColor(useInnerFrame).name();
            QString uc = QString("setFrameBrushColor (%1, \"%2\");").arg(uif, colorNameOld);
            QString colorNameNew = col.name();
            QString rc = QString("setFrameBrushColor (%1, \"%2\");").arg(uif, colorNameNew);
            saveStateBranch(selbi, uc, rc,
                    QString("Set background color of frame to %1").arg(colorNameNew));

            bc->setFrameBrushColor(useInnerFrame, col);
        }
        emitDataChanged(selbi);  // Notify HeadingEditor to eventually change BG color
    }
}

void VymModel::setFramePadding(
    const bool &useInnerFrame, const int &i, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
        if (bc->frameType(useInnerFrame) != FrameContainer::NoFrame)  {
            QString uif = toS(useInnerFrame);
            QString uc = QString("setFramePadding (%1, \"%2\");").arg(uif).arg(bc->framePadding(useInnerFrame));
            QString rc = QString("setFramePadding (%1, \"%2\");").arg(uif).arg(i);
            saveStateBranch(selbi, uc, rc,
                QString("set padding of frame to %1").arg(i));
            bc->setFramePadding(useInnerFrame, i);
        }
    }
    reposition();
}
void VymModel::setFramePenWidth(
    const bool &useInnerFrame, const int &i, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
        if (bc->frameType(useInnerFrame) != FrameContainer::NoFrame)  {
            QString uif = toS(useInnerFrame);
            QString uc = QString("setFramePenWidth (%1, \"%2\");").arg(uif).arg(bc->framePenWidth(useInnerFrame));
            QString rc = QString("setFramePenWidth (%1, \"%2\");").arg(uif).arg(i);
            saveStateBranch(selbi, uc, rc,
                QString("Set pen width of frame to %1").arg(i));

            bc->setFramePenWidth(useInnerFrame, i);
        }
    }
    reposition();
}

void VymModel::setHeadingColumnWidthAutoDesign(const bool &b, BranchItem *bi) // FIXME-2  missing saveState
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    BranchContainer *bc;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        if (bc->columnWidthAutoDesign() != b) {
            if (b)
                bc->setColumnWidth(mapDesignInt->headingColumnWidth(selbi->depth()));
            /* 
            QString v = b ? "Enable" : "Disable";
            saveState(selbi, QString("setRotationsAutoDesign (%1)")
                          .arg(toS(!b)),
                      selbi, QString("setRotationsAutoDesign (%1)").arg(toS(b)),
                      QString("%1 automatic rotations").arg(v));
                      */
            bc->setColumnWidthAutoDesign(b);
            branchPropertyEditor->updateControls();
            emitDataChanged(selbi);
        }
    }

    if (!selbis.isEmpty())
        reposition();
}

void VymModel::setHeadingColumnWidth (const int &i, BranchItem *bi) // FIXME-2 no saveState
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
	if (bc->columnWidth() != i) {

            /*
            saveState(selbi, QString("setRotationHeading (\"%1\")")
                          .arg(bc->rotationHeading()),
                      selbi, QString("setRotationHeading (\"%1\")").arg(i),
                      QString("Set rotation angle of heading and flags to %1").arg(i));
                      */

            bc->setColumnWidth(i);
            emitDataChanged(selbi);
        }
    }

    if (!selbis.isEmpty()) {
        reposition();
    }
}

void VymModel::setRotationAutoDesign(const bool &b, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
        if (bc->rotationsAutoDesign() != b) {
            QString s = b ? "Enable" : "Disable";
            QString c = QString("%1 automatic rotation heading and subtree").arg(s);
            saveStateBeginScript(c);
            if (b) {
                setRotationHeading(mapDesignInt->rotationHeading(selbi->depth()));
                setRotationSubtree(mapDesignInt->rotationSubtree(selbi->depth()));
            }
            QString uc = QString("setRotationAutoDesign(%1);").arg(toS(bc->rotationsAutoDesign()));
            QString rc = QString("setRotationAutoDesign(%1);").arg(toS(b));
            saveStateBranch(selbi, uc, rc);
            bc->setRotationsAutoDesign(b);
            branchPropertyEditor->updateControls();

            saveStateEndScript();
        }
    }

    if (!selbis.isEmpty())
        reposition();
}

void VymModel::setRotationHeading (const int &i, BranchItem* bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
	if (bc->rotationHeading() != i) {

            QString uc = QString("setRotationHeading(\"%1\");").arg(toS(bc->rotationHeading(), 1));
            QString rc = QString("setRotationHeading(\"%1\");").arg(i);
            saveStateBranch(selbi, uc, rc,
                      QString("Set rotation angle of heading and flags to %1").arg(i));

            bc->setRotationHeading(i);
        }
    }

    if (!selbis.isEmpty())
        reposition();
}

void VymModel::setRotationSubtree (const int &i, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        BranchContainer *bc = selbi->getBranchContainer();
	if (bc->rotationSubtree() != i) {
            QString uc = QString("setRotationSubtree(\"%1\");").arg(toS(bc->rotationSubtree(), 1));
            QString rc = QString("setRotationSubtree(\"%1\");").arg(i);
            saveStateBranch(selbi, uc, rc,
                      QString("Set rotation angle of subtree to %1").arg(i));

            bc->setRotationSubtree(i);
	}
    }

    if (!selbis.isEmpty())
        reposition();
}

void VymModel::setScaleAutoDesign (const bool & b, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    BranchContainer *bc;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        if (bc->scaleAutoDesign() != b) {
            QString s = b ? "Enable" : "Disable";
            QString c = QString("%1 automatic scaling").arg(s);
            saveStateBeginScript(c);
            if (b) {
                setScaleHeading(mapDesignInt->scaleHeading(selbi->depth()));
                setScaleSubtree(mapDesignInt->scaleSubtree(selbi->depth()));
            }
            QString uc = QString("setScaleAutoDesign(%1);").arg(toS(bc->scaleAutoDesign()));
            QString rc = QString("setScaleAutoDesign(%1);").arg(toS(b));
            saveStateBranch(selbi, uc, rc);
            bc->setScaleAutoDesign(b);
            branchPropertyEditor->updateControls();

            saveStateEndScript();
        }
    }

    if (!selbis.isEmpty()) {
        reposition();
    }
}

void VymModel::setScaleHeading (const qreal &f, const bool relative, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    BranchContainer *bc;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        qreal f_old = bc->scaleHeading();
        qreal f_new = relative ? f_old + f : f;

	if (bc->scaleHeading() != f_new) {
            QString uc = QString("setScaleHeading(%1);").arg(toS(f_old, 3));
            QString rc = QString("setScaleHeading(%1);").arg(toS(f_new, 3));
            QString c  = QString("Set heading scale factor to %1").arg(f_new);
            saveStateBranch(selbi, uc, rc, c);

            bc->setScaleHeading(f_new);
            branchPropertyEditor->updateControls();
	}
    }

    if (!selbis.isEmpty())
        reposition();
}

qreal VymModel::getScaleHeading ()
{
    QList<BranchItem *> selbis = getSelectedBranches();

    if (selbis.isEmpty()) return 1;

    return selbis.first()->getBranchContainer()->scaleHeading();
}


void VymModel::setScaleSubtree (const qreal &f_new, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    BranchContainer *bc;
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        qreal f_old = bc->scaleSubtree();

	if (f_new != f_old) {
            QString uc = QString("setScaleSubtree(%1);").arg(toS(f_old, 3));
            QString rc = QString("setScaleSubtree(%1);").arg(toS(f_new,3));
            QString c  = QString("Set subtree scale factor to %1").arg(toS(f_new, 3));
            saveStateBranch(selbi, uc, rc, c);

            bc->setScaleSubtree(f_new);
            branchPropertyEditor->updateControls();
	}
    }

    if (!selbis.isEmpty())
        reposition();
}

qreal VymModel::getScaleSubtree ()
{
    QList<BranchItem *> selbis = getSelectedBranches();

    if (selbis.isEmpty()) return 1;

    return selbis.first()->getBranchContainer()->scaleSubtree();
}

void VymModel::setScaleImage(const qreal &f, const bool relative, ImageItem *ii) // FIXME-2 missing saveState
{
    QList<ImageItem *> seliis = getSelectedImages(ii);

    foreach (ImageItem *selii, seliis) {
        qreal f_old = selii->scale();
        qreal f_new = relative ? f_old + f : f;
        if (selii->scale() != f_new) {
            /*saveState(selii, QString("setScale (%1)")
                          .arg(f_old),
                      selii, QString("setScale (%1)").arg(f_new),
                      QString("Set scale of image to %1").arg(f_new));
                      */

            selii->setScale(f_new);
            branchPropertyEditor->updateControls();
        }
    }

    if (!seliis.isEmpty())
        reposition();
}

void VymModel::setScale(const qreal &f, const bool relative)
{
    // Scale branches and/or images
    // Called from scripting or to grow/shrink via shortcuts
    setScaleAutoDesign(false);
    setScaleHeading(f, relative);
    setScaleImage(f, relative);
}

void VymModel::growSelectionSize()
{
    setScale(0.05, true);
}

void VymModel::shrinkSelectionSize()
{
    setScale(- 0.05, true);
}

void VymModel::resetSelectionSize() // FIXME-3 missing saveState. Switch (back?) to autodesign?
{
    ImageItem *selii = getSelectedImage();
    if (selii)
        setScale(1, false);
}

void VymModel::setBranchesLayout(const QString &s, BranchItem *bi)  // FIXME-3 no saveState yet (save: positions, auto, layout!)
{
    qDebug() << "VM::setBranchesLayout for " << headingText(bi) << s;
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    BranchContainer *bc;
    bool repositionRequired = false;
    foreach (BranchItem *selbi, selbis) {
        Container::Layout layout;
        if (selbi) {
            bc = selbi->getBranchContainer();

            if (s == "Auto") {
                bc->branchesContainerAutoLayout = true;
                layout = mapDesignInt->branchesContainerLayout(selbi->depth());
                if (bc->branchesContainerLayout() != layout) {
                    bc->setBranchesContainerLayout(layout);
                    repositionRequired = true;
                }
            } else {
                bc->branchesContainerAutoLayout = false;
                layout = Container::layoutFromString(s);
                if (layout != Container::UndefinedLayout) {
                    bc->setBranchesContainerLayout(layout);
                    repositionRequired = true;
                }
            }
        }
    }

    // Links might have been added or removed, Nested lists, etc...
    foreach (BranchItem *selbi, selbis)
        applyDesignRecursively(MapDesign::LayoutChanged, selbi);

    // Create and delete containers, update their structure
    if (repositionRequired) // FIXME-2 test not needed
        reposition();

}

void VymModel::setImagesLayout(const QString &s, BranchItem *bi)  // FIXME-3 no saveState yet (save positions, too!)
{
    BranchContainer *bc;
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        bc = selbi->getBranchContainer();
        if (s == "Auto") {
            bc->imagesContainerAutoLayout = true;
            bc->setImagesContainerLayout(
                    mapDesignInt->imagesContainerLayout(selbi->depth()));
        } else {
            bc->imagesContainerAutoLayout = false;
            Container::Layout layout;
            layout = Container::layoutFromString(s);
            if (layout != Container::UndefinedLayout)
                bc->setImagesContainerLayout(layout);
        }
    }
    reposition();
}

void VymModel::setHideLinkUnselected(bool b, TreeItem *ti)
{
    QList <TreeItem*> seltis = getSelectedItems(ti);

    foreach (TreeItem *selti, seltis) {
        if (selti->getType() == TreeItem::Image || selti->hasTypeBranch()) {
            QString v = b ? "Hide" : "Show";
            QString tiv;
            if (selti->hasTypeBranch())
                tiv = setBranchVar((BranchItem*)selti, "ti");
            else if (selti->hasTypeImage())
                tiv = setImageVar((ImageItem*)selti, "ti");
            else {
                qWarning() << "VymModel::setHideLinkUnselected  no branch or image";
                return;
            }
            QString uc = tiv + QString("ti.setHideLinkUnselected(%1);").arg(toS(!b));
            QString rc = tiv + QString("ti.setHideLinkUnselected(%1);").arg(toS(b));
            saveState(
                    uc, rc,
                    QString("%1 link if item %2 is not selected").arg(v, getObjectName(selti)));
            ((MapItem *)selti)->setHideLinkUnselected(b);
        }
    }
    branchPropertyEditor->updateControls();
}

void VymModel::setHideExport(bool b, BranchItem *bi)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        if (selbi->hideTemporary() != b) {
            selbi->setHideTemporary(b);
            QString u = toS(!b);
            QString r = toS(b);

            saveStateBranch(selbi,
                    QString("setHideExport (%1)").arg(u),
                    QString("setHideExport (%1)").arg(r),
                    "Set hide export of " + getObjectName(selbi) + " to " + r);
            emitDataChanged(selbi);
        }
    }

    if (!selbis.isEmpty())
        reposition();
}

void VymModel::toggleHideExport()
{
    QList<BranchItem *> selbis = getSelectedBranches();
    foreach (BranchItem *selbi, selbis) {
        bool b = !selbi->hideTemporary();
        setHideExport(b, selbi);
    }
}

void VymModel::toggleTask(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (auto selbi, selbis) {
        QString uc = "toggleTask();";
        saveStateBranch(
            selbi, uc, uc,
            QString("Toggle task of %1").arg(getObjectName(selbi)));
        Task *task = selbi->getTask();
        if (!task) {
            task = taskModel->createTask(selbi);
            taskEditor->select(task);
        }
        else
            taskModel->deleteTask(task);

        emitDataChanged(selbi);
        reposition();
    }
}

bool VymModel::cycleTaskStatus(BranchItem *bi, bool reverse)
{
    bool repositionRequired = false;
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        Task *task = selbi->getTask();
        if (task) {
            QString uc, rc;
            if (!reverse) {
                uc = "cycleTask(true);";
                rc = "cycleTask();";
            } else {
                uc = "cycleTask();";
                rc = "cycleTask(true);";
            }
            saveStateBranch(
                selbi, uc, rc,
                QString("Cycle task of %1").arg(getObjectName(selbi)));
            task->cycleStatus(reverse);
            task->setDateModification();

            // make sure task is still visible  // FIXME-2 for multi-selections?
            taskEditor->select(task);
            emitDataChanged(selbi);
            repositionRequired = true;
        }
    }
    if (repositionRequired) {
        reposition();
        return true;
    }
    return false;
}

bool VymModel::setTaskSleep(const QString &s, BranchItem *bi) // FIXME-2 missing saveState
{
    bool ok = false;
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (auto selbi, selbis) {
        Task *task = selbi->getTask();
        if (task) {
            QDateTime oldSleep = task->getSleep();

            // Parse the string, which could be days, hours or one of several
            // time formats

            if (s == "0") {
                ok = task->setSecsSleep(0);
            }
            else {
                static QRegularExpression re;
                re.setPattern("^\\s*(\\d+)\\s*$");
                re.setPatternOptions(QRegularExpression::InvertedGreedinessOption);
                QRegularExpressionMatch match = re.match(s);
                if (match.hasMatch()) {
                    // Found only digit, considered as days
                    ok = task->setDaysSleep(match.captured(1).toInt());
                }
                else {
                    re.setPattern("^\\s*(\\d+)\\s*h\\s*$");
                    match = re.match(s);
                    if (match.hasMatch()) {
                        // Found digit followed by "h", considered as hours
                        ok = task->setHoursSleep(match.captured(1).toInt());
                    }
                    else {
                        re.setPattern("^\\s*(\\d+)\\s*w\\s*$");
                        match = re.match(s);
                        if (match.hasMatch()) {
                            // Found digit followed by "w", considered as weeks
                            ok = task->setDaysSleep(7 * match.captured(1).toInt());
                        }
                        else {
                            re.setPattern("^\\s*(\\d+)\\s*s\\s*$");
                            match = re.match(s);
                            if (match.hasMatch()) {
                                // Found digit followed by "s", considered as
                                // seconds
                                ok = task->setSecsSleep(match.captured(1).toInt());
                            }
                            else {
                                ok = task->setDateSleep(
                                    s); // ISO date YYYY-MM-DDTHH:mm:ss

                                if (!ok) {
                                    re.setPattern("(\\d+)\\.(\\d+)\\.(\\d+)");
                                    match = re.match(s);
                                    if (match.hasMatch()) {
                                        QDateTime d(
                                            QDate(match.captured(3).toInt(),
                                                  match.captured(2).toInt(),
                                                  match.captured(1).toInt()).startOfDay());
                                        ok = task->setDateSleep(d); 
                                            // German format,
                                            // e.g. 24.12.2012
                                    }
                                    else {
                                        re.setPattern("(\\d+)\\.(\\d+)\\.");
                                        match = re.match(s);
                                        if (match.hasMatch()) {
                                            int month = match.captured(2).toInt();
                                            int day = match.captured(1).toInt();
                                            int year =
                                                QDate::currentDate().year();
                                            QDateTime d(QDate(year, month, day).startOfDay());
                                            // d = QDate(year, month,
                                            // day).startOfDay();
                                            if (QDateTime::currentDateTime()
                                                    .daysTo(d) < 0) {
                                                year++;
                                                d = QDateTime(
                                                    QDate(year, month, day).startOfDay());
                                                // d = QDate(year, month,
                                                // day).startOfDay();
                                            }
                                            ok = task->setDateSleep(d);
                                                // Short German format,
                                                // e.g. 24.12.
                                        }
                                        else {
                                            re.setPattern("(\\d+)\\:(\\d+)");
                                            match = re.match(s);
                                            if (match.hasMatch()) {
                                                int hour = match.captured(1).toInt();
                                                int min = match.captured(2).toInt();
                                                QDateTime d(
                                                    QDate::currentDate(),
                                                    QTime(hour, min));
                                                ok = task->setDateSleep(d); // Time HH:MM
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (ok) {
                QString oldSleepString;
                if (oldSleep.isValid())
                    oldSleepString = oldSleep.toString(Qt::ISODate);
                else
                    oldSleepString =
                        "1970-01-26T00:00:00"; // Some date long ago

                QString newSleepString = task->getSleep().toString(Qt::ISODate);
                task->setDateModification();
                selbi->updateTaskFlag(); // If tasks changes awake mode, then
                                         // flag needs to change
                /*saveState(selbi, QString("setTaskSleep (\"%1\")").arg(oldSleepString),
                    selbi, QString("setTaskSleep (\"%1\")").arg(newSleepString),
                    QString("setTaskSleep (\"%1\")").arg(newSleepString));
                    */
                emitDataChanged(selbi);
                reposition();
            }

        } // Found task
        if (!ok)
            return false;
    }     // Looping over selected branches
    return ok;
}

void VymModel::setTaskPriorityDelta(const int &pd, BranchItem *bi) // FIXME-2 missing saveState
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        Task *task = selbi->getTask();
        if (task) {
            /*saveState(selbi, QString("setTaskPriorityDelta (%1)")
                          .arg(task->getPriorityDelta()),
                      selbi,
                      QString("setTaskPriorityDelta (%1)")
                          .arg(pd),
                      "Set delta for priority of task");
                      */
            task->setPriorityDelta(pd);
            emitDataChanged(selbi);
        }
    }
}

int VymModel::getTaskPriorityDelta(BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        Task *task = selbi->getTask();
        if (task)
            return task->getPriorityDelta();
    }
    return 0;
}

int VymModel::taskCount() { return taskModel->count(this); }

void VymModel::updateTasksAlarm(bool force)
{
    if (taskModel->updateAwake(force) || force) {
        reposition();
    }
}

BranchItem *VymModel::addTimestamp()
{
    BranchItem *selbi = addNewBranch();
    if (selbi) {
        QDate today = QDate::currentDate();
        QChar c = '0';
        selbi->setHeadingPlainText(QString("%1-%2-%3")
                                       .arg(today.year(), 4, 10, c)
                                       .arg(today.month(), 2, 10, c)
                                       .arg(today.day(), 2, 10, c));
        emitDataChanged(selbi);
        reposition();
        select(selbi);
    }
    return selbi;
}

void VymModel::copy()
{
    if (readonly)
        return;

    QList<TreeItem *> itemList = getSelectedItems();

    QStringList clipboardFiles;

    if (itemList.count() > 0) {

        QStringList uids;

        uint i = 1;
        QString fn;
        foreach (TreeItem *ti, itemList) {
            uids << QString("\"%1\"").arg(ti->getUuid().toString());
            fn = QString("%1/%2-%3.xml")
                     .arg(clipboardDir)
                     .arg(clipboardFile)
                     .arg(i);
            QString content = saveToDir(clipboardDir, clipboardFile,
                                        FlagRowMaster::NoFlags, QPointF(), ti);

            if (!saveStringToDisk(fn, content))
                qWarning() << "ME::saveStringToDisk failed: " << fn;
            else {
                i++;
                clipboardFiles.append(fn);
            }
        }
        QClipboard *clipboard = QApplication::clipboard();
        QMimeData *mimeData = new QMimeData;
        mimeData->setData("application/x-vym", clipboardFiles.join(",").toLatin1());
        clipboard->setMimeData(mimeData);

        QString rc = QString("map.selectUids([%1]); map.copy();").arg(uids.join(","));
        QString comment = QString("Copy %1 selected %2 to clipboard: [%3]")
            .arg(itemList.count())
            .arg(pluralize(QString("item"), itemList.count()))
            .arg(uids.join(","));
        saveState("", rc, comment);

    }

    mainWindow->updateActions();
}

void VymModel::paste()
{
    if (readonly)
        return;

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    BranchItem *selbi = getSelectedBranch();
    ImageItem *selii = getSelectedImage();

    // Special case: When image is selected and image is pasted, try to append 
    // pasted image to current set of images in parent
    if (!selbi && selii && mimeData->hasImage())
        selbi = selii->parentBranch();

    if (selbi) {
        if (mimeData->formats().contains("application/x-vym")) {
            QStringList clipboardFiles = QString(mimeData->data("application/x-vym")).split(",");

            QString bv = setBranchVar(selbi);
            QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
            QString rc = bv + QString("b.select(); map.paste();");
            QString comment = QString("Paste to branch \"%1\"").arg(selbi->headingText());
            saveState(uc, rc, comment, selbi);

            bool zippedOrg = zipped;
            foreach(QString fn, clipboardFiles) {
                if (File::Success != loadMap(fn,
                            File::ImportAdd,
                            File::VymMap,
                            VymReader::SlideContent,
                            selbi,
                            selbi->branchCount()))
                    qWarning() << "VM::paste Loading clipboard failed: " << fn;
            }
            zipped = zippedOrg;
        } else if (mimeData->hasImage()) {
            //qDebug() << "VM::paste  mimeData->hasImage";
            QImage image = qvariant_cast<QImage>(mimeData->imageData());
            QString fn = clipboardDir + "/" + "image.png";
            if (!image.save(fn))
                qWarning() << "VM::paste  Could not save copy of image in system clipboard";
            else {
                ImageItem *ii = loadImage(selbi, fn);
                if (ii) {
                    setScaleImage(300.0 / image.width(), false, ii);    // FIXME-3 Better use user-defined fixed width when pasting images

                    if (selii)
                        // In case we pasted onto an existing image, select the new one
                        select(ii);
                }
            }
        } else if (mimeData->hasHtml()) {
            //setText(mimeData->html());
            //setTextFormat(Qt::RichText);
            qDebug() << "VM::paste found html...";
        } else if (mimeData->hasText()) {
            //setText(mimeData->text());
            //setTextFormat(Qt::PlainText);
            qDebug() << "VM::paste found text...";
        } else {
            qWarning() << "VM::paste Cannot paste data, mimeData->formats=" << mimeData->formats();
        }
    }
}

void VymModel::cut()
{
    if (readonly)
        return;

    copy();
    deleteSelection();
}

bool VymModel::canMoveUp(TreeItem *ti)
{
    if (ti) {
        BranchItem *pbi;
        if (ti->hasTypeBranch())
            pbi = ((BranchItem*)ti)->parentBranch();
        else if (ti->hasTypeImage())
            pbi = ((ImageItem*)ti)->parentBranch();
        else 
            return false;

        if (pbi == rootItem)
            return false;

        return (pbi->num(ti) > 0);
    }

    return false;
}

bool VymModel::canMoveDown(TreeItem *ti)
{
    if (ti) {
        BranchItem *pbi;

        if (ti->hasTypeBranch()) {
            pbi = ((BranchItem*)ti)->parentBranch();
            if (pbi == rootItem)
                return false;
            return (pbi->num(ti) < pbi->branchCount() - 1);
        } else if (ti->hasTypeImage()) {
            pbi = ((ImageItem*)ti)->parentBranch();
            return (pbi->num(ti) < pbi->imageCount() - 1);
        }
    }

    return false;
}

void VymModel::moveUp(TreeItem *ti)
{
    if (readonly) return;

    QList<BranchItem *> selbis = getSelectedBranches(ti);

    if (!selbis.isEmpty()){
        foreach (BranchItem *selbi, sortBranchesByNum(selbis, false)) {
            if (canMoveUp(selbi))
                relinkBranch(selbi, selbi->parentBranch(), selbi->num() - 1);
        }
    }

    QList<ImageItem *> seliis = getSelectedImages(ti);

    if (!seliis.isEmpty()){
        foreach (ImageItem *selii, sortImagesByNum(seliis, false)) {
            if (canMoveUp(selii))
                 relinkImage(selii, selii->parentBranch(), selii->num() - 1);
        }
    }
}

void VymModel::moveDown(TreeItem *ti)
{
    if (readonly) return;

    QList<BranchItem *> selbis = getSelectedBranches(ti);
    if (!selbis.isEmpty()) {
        foreach (BranchItem *selbi, sortBranchesByNum(selbis, true))
            if (canMoveDown(selbi))
                 relinkBranch(selbi, selbi->parentBranch(), selbi->num() + 1);
    }

    QList<ImageItem *> seliis = getSelectedImages(ti);
    if (!seliis.isEmpty()) {
        foreach (ImageItem *selii, sortImagesByNum(seliis, true))
            if (canMoveDown(selii))
                 relinkImage(selii, selii->parentBranch(), selii->num() + 1);
    }
}

void VymModel::moveUpDiagonally()
{
    if (readonly) return;   // FIXME-3 readonly needs be checked for every 
                            // public function in model, which modifies data...

    QList<BranchItem *> selbis = getSelectedBranches();

    foreach (BranchItem *selbi, selbis) {
        BranchItem *pbi = selbi->parentBranch();
        if (pbi == rootItem) break;

        int n = selbi->num();
        if (n == 0) break;

        BranchItem *dst = pbi->getBranchNum(n - 1);
        if (!dst) break;

        relinkBranch(selbi, dst, dst->branchCount() + 1);
    }
}

void VymModel::moveDownDiagonally()
{
    if (readonly) return;

    QList<BranchItem *> selbis = getSelectedBranches();
    foreach (BranchItem *selbi, selbis) {
        BranchItem *pbi = selbi->parentBranch();
        if (pbi == rootItem) break;
        BranchItem *parentParent = pbi->parentBranch();
        int n = pbi->num();

        relinkBranch(selbi, parentParent, n + 1);
    }
}

void VymModel::detach(BranchItem *bi)   // FIXME-2 Various issues
                                        // sometines linkSpaceCont and/or reposition missing 
                                        // -1 does not remove link for MainBranch
                                        // does not save old position in relinkBranch()
{
    QList<BranchItem *> selbis;
    if (bi)
        selbis << bi;
    else
        selbis = getSelectedBranches();
    foreach (BranchItem *selbi, selbis) {
        if (selbi->depth() > 0) {
            relinkBranch(selbi, rootItem, -1);
        }
    }
}

QList <BranchItem*> VymModel::sortBranchesByNum(QList <BranchItem*> unsortedList, bool inverse)
{
    // Shortcut
    if (unsortedList.count() < 2)
        return unsortedList;

    // We use QMultiMap because unsortedList might have branches 
    // with identical depths, but different parentBranches e.g.
    // when moving up/down. Then parts of the list would be lost.
    QMultiMap <int, BranchItem*> multimap;
    foreach (BranchItem *bi, unsortedList)
        multimap.insert(bi->num(), bi);

    QList <BranchItem*> sortedList;

    QMultiMapIterator<int, BranchItem*> i(multimap);
    if (inverse) {
            i.toBack();
            while (i.hasPrevious()) {
                i.previous();
                sortedList << i.value();
            }
    } else while (i.hasNext()) {
        i.next();
        sortedList << i.value();
    }

    return sortedList;
}

QList <BranchItem*> VymModel::sortBranchesByHeading(QList <BranchItem*> unsortedList, bool inverse) // FIXME-4 This funcion is currently never called
{
    QMap <QString, BranchItem*> map;
    foreach (BranchItem *bi, unsortedList)
        map.insert(bi->headingPlain(), bi);

    QList <BranchItem*> sortedList;

    if (inverse)
        for (auto i = map.cend(), begin = map.cbegin(); i != begin; --i)
            sortedList << i.value();
    else
        for (auto i = map.cbegin(), end = map.cend(); i != end; ++i)
            sortedList << i.value();

    return sortedList;
}

void VymModel::sortChildren(bool inverse, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    if (selbis.isEmpty()) return;

    foreach (BranchItem *selbi, selbis) {
        if (selbi) {
            if (selbi->branchCount() > 1) {
                QString bv = setBranchVar(selbi);
                QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
                QString com;
                QString rc;
                if (!inverse) {
                    rc = bv + QString("b.sortChildren(false);");
                    com = QString("Sort children of \"%1\"").arg(getObjectName(selbi));
                } else {
                    bv = setBranchVar(selbi);
                    uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
                    rc = bv + QString("b.sortChildren(false);");
                    com = QString("Inverse sort children of \"%1\"").arg(getObjectName(selbi));
                }
                saveState(uc, rc, com, selbi);

                QMultiMap <QString, BranchItem*> multimap;
                for (int i = 0; i < selbi->branchCount(); i++)
                    multimap.insert(selbi->getBranchNum(i)->headingPlain(), selbi->getBranchNum(i));

                int n = 0;
                QMultiMapIterator<QString, BranchItem*> i(multimap);
                if (inverse) {
                    i.toBack();
                    while (i.hasPrevious()) {
                        i.previous();
                        if (i.value()->num() != n) {
                            // Only relink if not already at this position
                            // and don't saveState while relinking
                            bool oldSaveStateBlocked = saveStateBlocked;
                            saveStateBlocked = true;
                            relinkBranch(i.value(), selbi, n);
                            saveStateBlocked = oldSaveStateBlocked;
                        }
                        n++;
                    }
                } else while (i.hasNext())  {
                    i.next();
                    if (i.value()->num() != n) {
                        // Only relink if not already at this position
                        bool oldSaveStateBlocked = saveStateBlocked;
                        saveStateBlocked = true;
                        relinkBranch(i.value(), selbi, n);
                        saveStateBlocked = oldSaveStateBlocked;
                    }
                    n++;
                }
            }
        }
    }
}

QList <ImageItem*> VymModel::sortImagesByNum(QList <ImageItem*> unsortedList, bool inverse)
{   // for moving up/down *multiple* images in lists or grid...

    if (unsortedList.count() < 2)
        return unsortedList;

    // We use QMultiMap because unsortedList might have branches 
    // with identical depths, but different parentBranches e.g.
    // when moving up/down. Then parts of the list would be lost.
    QMultiMap <int, ImageItem*> multimap;
    foreach (ImageItem *ii, unsortedList)
        multimap.insert(ii->num(), ii);

    QList <ImageItem*> sortedList;

    QMultiMapIterator<int, ImageItem*> i(multimap);
    if (inverse) {
            i.toBack();
            while (i.hasPrevious()) {
                i.previous();
                sortedList << i.value();
            }
    } else while (i.hasNext()) {
        i.next();
        sortedList << i.value();
    }

    return sortedList;
}

BranchItem *VymModel::createBranchWhileLoading(BranchItem *dst, int insertPos)
{
    BranchItem *newbi;
    if (!dst || dst == rootItem)
        newbi = addMapCenterAtPos(QPointF(0, 0));
    else {
        if (insertPos < 0)
            newbi = addNewBranchInt(dst, -2);
        else
            newbi = addNewBranchInt(dst, insertPos);
    }

    // Set default design styles, e.g. font
    applyDesign(MapDesign::LoadingMap, newbi);
    return newbi;
}

ImageItem *VymModel::createImage(BranchItem *dst)
{
    if (dst) {
        QModelIndex parix;
        int n;

        ImageItem *newii = new ImageItem();

        emit layoutAboutToBeChanged();

        parix = index(dst);
        if (!parix.isValid())
            qDebug() << "VM::createII invalid index\n";
        n = dst->getRowNumAppend(newii);
        beginInsertRows(parix, n, n);
        dst->appendChild(newii);
        endInsertRows();

        emit layoutChanged();

        dst->addToImagesContainer(newii->createImageContainer());

        latestAddedItemUuid = newii->getUuid();
        reposition();
        return newii;
    }
    return nullptr;
}

bool VymModel::createXLink(XLink *xlink)
{
    BranchItem *begin = xlink->getBeginBranch();
    BranchItem *end = xlink->getEndBranch();

    if (!begin || !end) {
        qWarning() << "VM::createXLinkNew part of XLink is nullptr";
        return false;
    }

    if (begin == end) {
        if (debug)
            qDebug() << "VymModel::createXLink begin==end, aborting";
        return false;
    }

    // check, if link already exists
    foreach (XLink *l, xlinks) {
        if ((l->getBeginBranch() == begin && l->getEndBranch() == end) ||
            (l->getBeginBranch() == end && l->getEndBranch() == begin)) {
            qWarning() << "VymModel::createXLink link exists already, aborting";
            return false;
        }
    }

    QModelIndex parix;
    int n;

    XLinkItem *newli = new XLinkItem();
    newli->setXLink(xlink);
    xlink->setBeginXLinkItem(newli);

    emit layoutAboutToBeChanged();

    parix = index(begin);
    n = begin->getRowNumAppend(newli);
    beginInsertRows(parix, n, n);
    begin->appendChild(newli);
    endInsertRows();

    newli = new XLinkItem();
    newli->setXLink(xlink);
    xlink->setEndXLinkItem(newli);

    parix = index(end);
    n = end->getRowNumAppend(newli);
    beginInsertRows(parix, n, n);
    end->appendChild(newli);
    endInsertRows();

    emit layoutChanged();

    xlinks.append(xlink);
    xlink->activate();

    latestAddedItemUuid = newli->getUuid();

    if (!xlink->getXLinkObj()) {
        xlink->createXLinkObj();
        reposition();
    }
    else
        xlink->updateXLink();

    xlink->setStyleBegin(mapDesignInt->defXLinkStyleBegin());
    xlink->setStyleEnd(mapDesignInt->defXLinkStyleEnd());

    QString xv = setXLinkVar(xlink, "xl");
    QString uc = xv + "map.removeXLink(xl);";
    QString rc = QString("map.loadDataInsert(\"REDO_PATH\");");
    QString com = QString("Add XLink from \"%1\" to \"%2\"")
        .arg(getObjectName(begin), getObjectName(end));
    saveState(uc, rc, com, nullptr, xlink->beginXLinkItem());

    return true;
}

QColor VymModel::getXLinkColor()
{
    XLink *xl = getSelectedXLink();
    if (xl)
        return xl->getPen().color();
    else
        return QColor();
}

int VymModel::getXLinkWidth()
{
    XLink *xl = getSelectedXLink();
    if (xl)
        return xl->getPen().width();
    else
        return -1;
}

Qt::PenStyle VymModel::getXLinkStyle()
{
    XLink *xl = getSelectedXLink();
    if (xl)
        return xl->getPen().style();
    else
        return Qt::NoPen;
}

QString VymModel::getXLinkStyleBegin()
{
    XLink *xl = getSelectedXLink();
    if (xl)
        return xl->getStyleBeginString();
    else
        return QString();
}

QString VymModel::getXLinkStyleEnd()
{
    XLink *xl = getSelectedXLink();
    if (xl)
        return xl->getStyleEndString();
    else
        return QString();
}
AttributeItem *VymModel::setAttribute( // FIXME-3 saveState( missing. For bulk changes like Jira maybe save whole branch use Script...
        BranchItem *dst,
        const QString &key,
        const QVariant &value,
        bool removeIfEmpty)
{
    if (!dst) return nullptr;

    bool keyFound = false;
    AttributeItem *ai;

    for (int i = 0; i < dst->attributeCount(); i++) {
        // Check if there is already an attribute with same key
        ai = dst->getAttributeNum(i);
        if (ai->key() == key)
        {
            keyFound = true;
            if (value.toString().isEmpty() && removeIfEmpty) {
                // Remove attribute
                deleteAttribute(dst, key);
                ai = nullptr;
            } else {
                // Set new value (if required)
                if (value != ai->value())
                    ai->setValue(value);
            }
            break;
        }
    }
    if (!keyFound) {
        // Create new attribute
        ai = new AttributeItem(key, value);

        emit layoutAboutToBeChanged();

        QModelIndex parix = index(dst);
        int n = dst->getRowNumAppend(ai);
        beginInsertRows(parix, n, n);
        dst->appendChild(ai);
        endInsertRows();
        emit layoutChanged();

        // Jira attributes
        if (ai->key() == "Jira.issueUrl") {
            dst->setUrlType(TreeItem::JiraUrl);
            updateJiraFlag(dst);
        }
    }

    emitDataChanged(dst);
    reposition();

    return ai;
}

void VymModel::deleteAttribute(BranchItem *dst, const QString &key)
{
    AttributeItem *ai;

    for (int i = 0; i < dst->attributeCount(); i++) {
        // Check if there is already an attribute with same key
        ai = dst->getAttributeNum(i);
        if (ai->key() == key)
        {
            // Key exists, delete attribute
            deleteItem(ai);
            break;
        }
    }
}

void VymModel::deleteAttributesKeyStartingWith(BranchItem *dst, const QString &key_start)
{
    AttributeItem *ai;

    for (int i = 0; i < dst->attributeCount(); i++) {
        // Check if there is already an attribute with same key
        ai = dst->getAttributeNum(i);
        if (ai->key().startsWith(key_start))
        {
            // Key exists, delete attribute
            deleteItem(ai);
            break;
        }
    }
}

AttributeItem *VymModel::getAttributeByKey(const QString &key, TreeItem *ti)
{
    TreeItem *selti = getSelectedItem(ti);
    if (selti) {
        for (int i = 0; i < selti->attributeCount(); i++) {
            AttributeItem *ai = selti->getAttributeNum(i);
            if (ai->key() == key)
                return ai;
        }
    }
    return nullptr;
}

BranchItem *VymModel::addMapCenter(bool interactive)
{
    if (interactive) {
        // Start to build undo/redo scripts
        // These script will be finished later when setHeading() is called
        if (hasContextPos)
            saveStateBeginScript(
                    QString("Add new MapCenter at (%1)").arg(toS(contextPos)));
        else
            saveStateBeginScript("Add new MapCenter");
    }

    if (!hasContextPos) {
        // E.g. when called via keypresss:
        // Place new MCO in middle of existing ones,
        // Useful for "brainstorming" mode...
        contextPos = QPointF();
        BranchItem *bi;
        BranchContainer *bc;
        for (int i = 0; i < rootItem->branchCount(); ++i) {
            bi = rootItem->getBranchNum(i);
            bc = bi->getBranchContainer();
            if (bc)
                contextPos += bc->pos();
        }
        if (rootItem->branchCount() > 1)
            contextPos *= 1 / (qreal)(rootItem->branchCount());
    }

    BranchItem *newbi = addMapCenterAtPos(contextPos, interactive);

    if (interactive)
        mapEditor->editHeading(newbi);

    updateActions();
    emitShowSelection();

    emitUpdateLayout();
    return newbi;
}

BranchItem *VymModel::addMapCenterAtPos(QPointF absPos, bool interactive)
// createMapCenter could then probably be merged with createBranch
{
    // Create TreeItem
    QModelIndex parix = index(rootItem);

    BranchItem *newbi = new BranchItem(rootItem);
    int n = rootItem->getRowNumAppend(newbi);

    emit layoutAboutToBeChanged();
    beginInsertRows(parix, n, n);

    rootItem->appendChild(newbi);

    endInsertRows();
    emit layoutChanged();

    // Create BranchContainer
    BranchContainer *bc = newbi->createBranchContainer(getScene());
    if (bc) {
        bc->setPos(absPos);

        if (!saveStateBlocked) {
            // Don't apply design while loading map
            applyDesign(MapDesign::CreatedByUser, newbi);

            QString uc, rc, com;
            com = QString("Add new MapCenter at (%1)").arg(toS(absPos));
            uc = setBranchVar(newbi) + "map.removeBranch(b);";
            rc = setBranchVar(rootItem) + QString(" b.loadBranchInsert(\"REDO_PATH\", %1);").arg(newbi->num());
            saveState( uc, rc, com, nullptr, newbi);

        }
    }

    reposition();
    return newbi;
}

BranchItem *VymModel::addNewBranchInt(BranchItem *dst, int pos)
{
    // Depending on pos:
    // -3	insert in children of parent  above selection
    // -2	add branch to selection
    // -1	insert in children of parent below selection
    // 0..n	insert in children of parent at pos

    // Create TreeItem
    BranchItem *parbi = dst;
    int n;
    BranchItem *newbi = new BranchItem;

    emit layoutAboutToBeChanged();

    if (pos == -2) {
        n = parbi->getRowNumAppend(newbi);
        beginInsertRows(index(parbi), n, n);
        parbi->appendChild(newbi);
        endInsertRows();
    }
    else if (pos == -1 || pos == -3) {
        // insert below selection
        parbi = dst->parentBranch();

        n = dst->childNumber() + (3 + pos) / 2; //-1 |-> 1;-3 |-> 0
        beginInsertRows(index(parbi), n, n);
        parbi->insertBranch(n, newbi);
        endInsertRows();
    }
    else { // pos >= 0
        n = parbi->getRowNumAppend(newbi) - (parbi->branchCount() - pos);
        beginInsertRows(index(parbi), n, n);
        parbi->insertBranch(pos, newbi);
        endInsertRows();
    }
    emit layoutChanged();

    // Create Container
    newbi->createBranchContainer(getScene());

    // Update parent item and stacking order of container to match order in model
    newbi->updateContainerStackingOrder();

    // Reposition now for correct position of e.g. LineEdit later and upLink
    reposition();

    return newbi;
}

BranchItem *VymModel::addNewBranch(BranchItem *bi, int pos, bool interactive)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (!selbi)
        return nullptr;

    QString comment;
    if (interactive) {
        // saveStateEndScript will be called in VymModel::setHeading()
        if (pos == -2)
            comment = QString("Add new branch to %1").arg(getObjectName(selbi));
        else if (pos < -2)
            comment = QString("Add new branch above %1").arg(getObjectName(selbi));
        else
            comment = QString("Add new branch below %1").arg(getObjectName(selbi));
        saveStateBeginScript(comment);
    }

    BranchItem *newbi = addNewBranchInt(selbi, pos);

    // Required to initialize styles
    if (!saveStateBlocked)
        // Don't apply design while loading map
        applyDesign(MapDesign::CreatedByUser, newbi);

    if (newbi) {
        QString uc, rc;
        BranchItem *pbi;
        if (pos == -2)
            pbi = selbi;
        else
            pbi = selbi->parentBranch();

        uc = setBranchVar(newbi) + "map.removeBranch(b);";
        rc = setBranchVar(pbi) + QString(" b.loadBranchInsert(\"REDO_PATH\", %1);").arg(newbi->num());
        saveState( uc, rc, "", nullptr, newbi);

        latestAddedItemUuid = newbi->getUuid();
        // In Network mode, the client needs to know where the new branch
        // is, so we have to pass on this information via saveState.
        // TODO: Get rid of this positioning workaround
        /* FIXME  network problem:  QString ps=toS
           (newbo->getAbsPos()); sendData ("selectLatestAdded ()"); sendData
           (QString("move %1").arg(ps)); sendSelection();
           */
    }

    if (interactive) {
        select(newbi);
        mapEditor->editHeading();
    }

    return newbi;
}

BranchItem *VymModel::addNewBranchBefore(BranchItem *bi, bool interactive)    // FIXME-2 enable one history step
{
    BranchItem *newbi = nullptr;
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi && selbi->getType() == TreeItem::Branch)  // FIXME-2 Better check for depth...
    // We accept no MapCenter here, so we _have_ a parent
    {
        QString comment;
        if (interactive) {
            // saveStateEndScript will be called in VymModel::setHeading()
            comment = QString("Add new branch before %1").arg(getObjectName(selbi));
            saveStateBeginScript(comment);
        }

        // add below selection
        newbi = addNewBranchInt(selbi, -1);

        if (newbi) {
            // Required to initialize styles
            if (!saveStateBlocked)
                // Don't apply design while loading map
                applyDesign(MapDesign::CreatedByUser, newbi);

            // newbi->move2RelPos (p);

            // Move selection to new branch
            // relink() would create another saveStateScript, block saveState for now
            bool saveStateBlockedOrg = saveStateBlocked;
            saveStateBlocked = true;
            relinkBranch(selbi, newbi, 0);
            saveStateBlocked = saveStateBlockedOrg;

            QString uc = setBranchVar(newbi) + "map.removeKeepChildren(b);";
            QString rc = setBranchVar(selbi) + "map.loadBranchReplace(\"REDO_PATH\",b);";
            saveState(uc, rc, "", nullptr, newbi);

            emitDataChanged(newbi);
        }
    }

    if (interactive) {
        select(newbi);
        mapEditor->editHeading();
    }

    return newbi;
}

bool VymModel::relinkBranch(BranchItem *branch, BranchItem *dst, int num_dst)
{
    if (!branch)
        return false;

    QList <BranchItem*> branches = {branch};
    return relinkBranches(branches, dst, num_dst);
}

bool VymModel::relinkBranches(QList <BranchItem*> branches, BranchItem *dst, int num_dst)   
{
    // qDebug() << "VM::relink " << branches.count() << " branches to " << headingText(dst) << "num_dst=" << num_dst;

    // Selection is lost when removing rows from model
    QList <TreeItem*> selectedItems = getSelectedItems();

    if (branches.isEmpty())
        branches = getSelectedBranches();

    if (!dst || branches.isEmpty())
        return false;

    if (num_dst < 0 || num_dst >= dst->branchCount())
        num_dst = dst->branchCount();

    if (!saveStateBlocked)
        // When ordering branches, we already saveState there and not for 
        // each branch individually
        saveStateBeginScript(
            QString("Relink %1 objects to \"%2\"")
                .arg(branches.count())
                .arg(dst->headingPlain()));

    BranchItem* bi_prev = nullptr;  // FIXME-2 never really set and thus used
    foreach (BranchItem *bi, branches) {
        // Check if we link to ourself
        if (dst == bi) {
            qWarning() << "VM::relinkBranch  Attempting to relink to myself: " << bi->headingPlain();
            return false;
        }

        // Check if we relink down to own children
        if (dst->isChildOf(bi))
            return false;

        // Save old selection for savestate
        QString preNumString = QString::number(bi->num(), 10);
        QString preParUidString = bi->parent()->getUuid().toString();

        // Remember original position for saveState
        bool rememberPos = false;
        BranchItem *pbi = bi->parentBranch();
        if (pbi == rootItem)
        {
            // Remember position of MapCenter
            rememberPos = true;
        } else {
            BranchContainer *pbc = pbi->getBranchContainer();
            if (pbc->hasFloatingBranchesLayout())
                rememberPos = true;
        }

        // Prepare BranchContainers
        BranchContainer *bc = bi->getBranchContainer();
        BranchContainer *dstBC = dst->getBranchContainer(); // might be nullptr for MC!

        // Keep position when detaching
        bool keepPos;
        QPointF preDetachPos;
        if (dst == rootItem) {
            keepPos = true;
            preDetachPos = bc->getHeadingContainer()->scenePos();
        } else
            keepPos = false;

        // What kind of relinking are we doing? Important for style updates
        MapDesign::UpdateMode updateMode = MapDesign::RelinkedByUser; // FIXME-2 not used later   also not considering detaching

        BranchItem *branchpi = bi->parentBranch();

        // Remove at current position
        int removeRowNum = bi->childNum();

        QModelIndex pix = index(branchpi);
        /* FIXME-2 remove debug stuff
        std::cout << "  VM::relink removing " << bi << " " << bi->headingPlain().toStdString()
                  << " at n=" << removeRowNum
                  << " from " << branchpi << "  " << branchpi->headingPlain().toStdString()
                  << " to " << dst << " " << dst->headingPlain().toStdString() << endl;
        std::cout << "      persIxList: " << (this->persistentIndexList()).count() << endl;
        std::cout << "         num_dst: " << num_dst << endl;
        */

        QModelIndex dix = index(dst);
        int dstRowNum = num_dst;
        if (branchpi == dst && num_dst > removeRowNum) {
            // When moving down with same parent:
            // Be careful to insert *before* destination index using beginMoveRows
            // https://doc.qt.io/qt-6/qabstractitemmodel.html#moveRows
            dstRowNum = num_dst + 1;
            if (num_dst > removeRowNum + 1)
                // if not just moving down one level but further, adapt num_dst
                num_dst--;
        }

        emit layoutAboutToBeChanged();
        bool b = beginMoveRows(pix, removeRowNum, removeRowNum, dix, dstRowNum);
        Q_ASSERT(b);
        /*
        std::cout << "  beginMoveRows=" << toS(b).toStdString()
                  << "  removeRowNum=" << removeRowNum
                  << " num_dst=" << num_dst
                  << " dstRowNum=" << dstRowNum << endl;
          */
        branchpi->removeChild(removeRowNum);
        dst->insertBranch(num_dst, bi);
        endMoveRows();
        emit layoutChanged();

        // Update upLink of BranchContainer to *parent* BC of destination
        bc->linkTo(dstBC);

        // Update parent item and stacking order of container
        bi->updateContainerStackingOrder();

        // reset parObj, fonts, frame, etc in related branch-container or other view-objects
        applyDesign(MapDesign::RelinkedByUser, bi);


        // Keep position when detaching
        if (keepPos) {
            bc->setPos(preDetachPos);
        }

        // Savestate, but not if just moving up/down
        if (!saveStateBlocked) {
            QString uc, rc;

            if (rememberPos) {
                // For undo move back to original position in old floating layout
                uc = QString("setPos %1;").arg(toS(bc->getOriginalPos()));
                rc = "";

                saveStateBranch( bi, uc, rc, QString("Move %1") .arg(headingText(bi)));
            }

            QString postNumString = QString::number(bi->num(), 10);

            QString bv = setBranchVar(bi);
            if (pbi == rootItem)
                uc = bv + " detach ()";
            else {
                uc = bv + QString(" dst = map.findBranchById(\"%1\");").arg(preParUidString);
                uc += QString(" b.relinkToBranchAt (dst, \"%1\");").arg(preNumString);
            }
            rc = bv + QString(" dst = map.findBranchById(\"%1\");").arg(dst->getUuid().toString());
            rc += QString(" b.relinkToBranchAt (dst, \"%1\");").arg(postNumString);

            saveState(uc, rc,
                      QString("Relink %1 to %2")
                          .arg(getObjectName(bi), getObjectName(dst)));

            if (dstBC && dstBC->hasFloatingBranchesLayout()) {
                // Save current position for redo
                saveStateBranch(bi, "", 
                          QString("setPos %1;").arg(toS(bc->pos())),
                          QString("Move %1")
                              .arg(getObjectName(bi)));
            }
        } // saveState not blocked
        bi_prev = bi;
    }   // Iterating over selbis    

    reposition();

    if (!saveStateBlocked)
        saveStateEndScript();

    // Restore selection, which was lost when removing rows
    select(selectedItems);

    return true;
}

bool VymModel::relinkImage(ImageItem* image, TreeItem *dst_ti, int num_new) {
    if (!image)
        return false;

    QList <ImageItem*> images = {image};
    return relinkImages(images, dst_ti, num_new);
}

bool VymModel::relinkImages(QList <ImageItem*> images, TreeItem *dst_ti, int num_new)
{
    // Selection is lost when removing rows from model
    QList <TreeItem*> selectedItems = getSelectedItems();

    BranchItem *dst;

    if (images.isEmpty())
        images = getSelectedImages();

    if (!dst_ti || images.isEmpty())
        return false;

    if (dst_ti->hasTypeImage()) {
        // Allow dropping on images,
        // append at this num in parentBranch then:
        dst = ((ImageItem*)dst_ti)->parentBranch();
        if (!dst || dst == rootItem)
            return false;
        num_new = dst_ti->num();
    } else if (dst_ti->hasTypeBranch()) {
        dst = (BranchItem*)dst_ti;
    } else
        return false;

    if (num_new < 0 || num_new >= dst->imageCount())
        num_new = dst->imageCount();

    if (!saveStateBlocked)
        // When ordering branches, we already saveState there and not for 
        // each branch individually
        saveStateBeginScript(
            QString("Relink %1 objects to \"%2\"")
                .arg(images.count())
                .arg(dst->headingPlain()));

    foreach(ImageItem *ii, images) {
        emit layoutAboutToBeChanged();

        BranchItem *pi = (BranchItem *)(ii->parent());
        // Remove at current position
        int n = ii->childNum();
        beginRemoveRows(index(pi), n, n);
        pi->removeChild(n);
        endRemoveRows();

        // Insert again
        int insertRowNum;
        if (dst->imageCount() == 0)
            // Append as last image to dst
            insertRowNum = 0;
        else
            insertRowNum = dst->getFirstImage()->childNumber() + num_new;
        QModelIndex dstix = index(dst);
        n = dst->getRowNumAppend(ii);

        beginInsertRows(dstix, insertRowNum + num_new, insertRowNum + num_new);
        dst->insertImage(num_new, ii);
        endInsertRows();

        emit layoutChanged();

        ii->updateContainerStackingOrder();
        // FIXME-2 relinkImages issues:
        // - What about updating links of images (later)?
        // - What about updating design (later)?
        // - in ImageWrapper: num_new missing
        // - does not save positions

        QString iv = setImageVar(ii);
        QString uc = setBranchVar(pi) + iv + "i.relinkToBranch(b);";
        QString rc = setBranchVar(dst) + iv + "i.relinkToBranch(b);";
        QString com = QString("Relink image to %1").arg(getObjectName(dst));

        saveState(uc, rc, com);
    }

    reposition();

    if (!saveStateBlocked)
        saveStateEndScript();

    // Restore selection, which was lost when removing rows
    select(selectedItems);

    return true;
}

bool VymModel::relinkTo(const QString &dstString, int num)
{
    TreeItem *selti = getSelectedItem();
    if (!selti)
        return false; // Nothing selected to relink

    TreeItem *dst = findBySelectString(dstString);
    if (!dst)
        return false; // Could not find destination

    if (!dst->hasTypeBranch())
        return false; // Relinking only allowed to branchLike destinations

    if (selti->hasTypeBranch()) {
        BranchItem *selbi = (BranchItem *)selti;

        if (relinkBranch(selbi, (BranchItem *)dst, num))
            return true;

    } else if (selti->hasTypeImage()) {
        if (relinkImage(((ImageItem *)selti), (BranchItem *)dst))
            return true;
    }
    return false; // Relinking failed
}

void VymModel::deleteSelection(ulong selID)
{
    QList<ulong> selectedIDs;
    if (selID > 0)
        selectedIDs << selID;
    else
        selectedIDs = getSelectedIDs();

    unselectAll();

    mapEditor->stopContainerAnimations();  // FIXME-5 better tell ME about deleted items, so that ME can take care of race conditions, e.g. also deleting while moving objects

    foreach (ulong id, selectedIDs) {
        TreeItem *ti = findID(id);
        BranchItem *pbi;
        if (ti) {
            pbi = (BranchItem*)(ti->parent());
            if (pbi && !pbi->hasTypeBranch())
                pbi = nullptr;

            if (ti->hasTypeBranch()) { // Delete branch
                BranchItem *bi = (BranchItem *)ti;
                BranchItem *pbi = bi->parentBranch();
                QString bv = setBranchVar(bi);
                QString pbv = setBranchVar(pbi, "pb");
                QString uc = pbv + QString("pb.loadBranchInsert(\"UNDO_PATH\", %1)").arg(bi->num());
                QString rc;
                rc = bv + "map.removeBranch(b);";
                saveState(uc, rc,
                        QString("Remove branch \"%1\"").arg(bi->headingText()),
                            bi, nullptr);

                BranchItem *pi = (BranchItem *)(deleteItem(bi));
                if (pi) {
                    if (pi->isScrolled() && pi->branchCount() == 0)
                        pi->unScroll();
                    emitDataChanged(pi);    // FIXME-2 duplicate call, already in deleteItem
                    select(pi);
                }
                else
                    emitDataChanged(rootItem);
                ti = nullptr;
            } else if (ti->getType() == TreeItem::Image) {
                QString iv = setImageVar((ImageItem*)ti);
                QString bv = setBranchVar(pbi);
                QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
                QString rc = iv + QString("map.removeImage(i);");
                QString com = QString("Remove image \"%1\" from branch \"%2\"").arg(getObjectName(ti), getObjectName(pbi));
                saveState(uc, rc, com, pbi);

                deleteItem(ti);
                emitDataChanged(pbi);
                select(pbi);
            } else if (ti->getType() == TreeItem::XLinkItemType) {
                deleteXLink(((XLinkItem*)ti)->getXLink());
            } else if (ti->getType() == AttributeItem::Attribute && pbi) {
                AttributeItem *ai = (AttributeItem*)ti;
                QString av = setAttributeVar((AttributeItem*)ti);
                QString bv = setBranchVar(pbi);
                QString uc = bv + QString("b.setAttribute(\"%1\",\"%2\");").arg(ai->key(), ai->value().toString());
                QString rc = QString("map.removeImage(i);");
                QString com = QString("Remove image \"%1\" from branch \"%2\"").arg(getObjectName(ti), getObjectName(pbi));
                saveState(uc, rc, com, pbi);
                deleteItem(ti);
            } else
                qWarning("VymmModel::deleteSelection()  unknown type?!");
        } // ti found
    } // Loop over selectedIDs

    emptyXLinksTrash();

    reposition();
}

void VymModel::deleteKeepChildren(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *selbi, selbis) {
        if (selbi->depth() < 1) {
            while (selbi->branchCount() > 0)
                detach(selbi->getBranchNum(0));

            deleteSelection(selbi->getID());
            //saveStateEndScript(); // FIXME-2 needed?
        } else {
            // Check if we have children at all to keep
            if (selbi->branchCount() == 0)
                deleteSelection();
            else {
                unselectAll();

                bool oldSaveState = saveStateBlocked;
                saveStateBlocked = true;

                BranchItem *pi = (BranchItem *)(selbi->parent());

                QString pbv = setBranchVar(pi, "pb");
                QString bv = setBranchVar(selbi);
                QString uc = pbv + "map.loadBranchReplace(\"UNDO_PATH\", pb);";
                QString rc = bv + "map.removeKeepChildren(b);";
                saveState(uc, rc,
                    QString("Remove branch \"%1\" and keep children").arg(selbi->headingText()),
                    pi);

                QString sel = getSelectString(selbi);
                int num_dst = selbi->num();
                BranchItem *bi = selbi->getFirstBranch();
                while (bi) {
                    relinkBranch(bi, pi, num_dst);
                    bi = selbi->getFirstBranch();
                    num_dst++;
                }
                deleteItem(selbi);
                reposition(); //FIXME-2 not necessary, already called above in deleteItem
                saveStateBlocked = oldSaveState;
                emitDataChanged(pi);    // FIXME-2 probably also not necessary
                select(sel);
            }
        }
    }

    emptyXLinksTrash();

}

void VymModel::deleteChildren(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        QString bv = setBranchVar(selbi);
        QString uc = bv + "map.loadBranchReplace(\"UNDO_PATH\", b);";
        QString rc = bv + "b.removeChildren();";
        saveState(uc, rc,
                QString("Remove children of \"%1\"").arg(selbi->headingText()),
                selbi);
        emit layoutAboutToBeChanged();

        QModelIndex ix = index(selbi);
        int n = selbi->childCount() - 1;
        beginRemoveRows(ix, 0, n);
        removeRows(0, n + 1, ix);
        endRemoveRows();
        if (selbi->isScrolled()) unscrollBranch(selbi);

        updateJiraFlag(selbi);
        emit layoutChanged();

        emitDataChanged(selbi);
        reposition();
    }
    emptyXLinksTrash();
}

void VymModel::deleteChildrenBranches(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        if (selbi->branchCount() > 0) {
            int n_first = selbi->getFirstBranch()->childNum();
            int n_last  = selbi->getLastBranch()->childNum();

            QString bv = setBranchVar(selbi);
            QString uc = bv + "map.loadBranchReplace(\"UNDO_PATH\", b);";
            QString rc = bv + "b.removeChildrenBranches();";
            saveState(uc, rc,
                    QString("Remove children branches of \"%1\"").arg(selbi->headingText()),
                    selbi);

            emit layoutAboutToBeChanged();

            QModelIndex ix = index(selbi);
            beginRemoveRows(ix, n_first, n_last);
            if (!removeRows(n_first, n_last - n_first + 1, ix))
                qWarning() << "VymModel::deleteChildBranches removeRows() failed";
            endRemoveRows();
            if (selbi->isScrolled()) unscrollBranch(selbi);

            emit layoutChanged();
        }
    }

    if (selbis.count() > 0)  {
        emptyXLinksTrash();
        reposition();
    }
}

TreeItem *VymModel::deleteItem(TreeItem *ti)    // FIXME-2 remove debug stuff
{
    if (ti) {
        TreeItem *pi = ti->parent();

        bool wasAttribute = ti->hasTypeAttribute();
        TreeItem *parentItem = ti->parent();

        QModelIndex parentIndex = index(pi);

        emit layoutAboutToBeChanged();

        int n = ti->childNum();
        //std::cout << "VM::deleteItem a) ti=" << ti << "  " << headingText(ti).toStdString() << " pi=" << headingText(pi).toStdString() << "  n=" << n << endl;    // FIXME-2 Debugging
        beginRemoveRows(parentIndex, n, n);
        //qDebug() << "VM::deleteItem b) ";
        bool r = removeRows(n, 1, parentIndex);  // Deletes object!
        //qDebug() << "VM::deleteItem c) ";
        endRemoveRows();
        //std::cout << "VM::deleteItem d) r=" << r << endl;

        emit layoutChanged();
        //qDebug() << "VM::deleteItem e) ";

        emitUpdateQueries();

        //qDebug() << "VM::deleteItem f) ";
        if (wasAttribute) {
            updateJiraFlag(parentItem);
            emitDataChanged(parentItem);
        }
        //qDebug() << "VM::deleteItem g) ";
        reposition();
        //std::cout << "VM::deleteItem h) " << endl;

        if (pi->depth() >= 0)
            return pi;
    }
    return nullptr;
}

void VymModel::deleteXLink(XLink *xlink)
{
    //qDebug() << "VM::deleteXLink start";


    QString xv = setXLinkVar(xlink);
    QString uc = QString("map.loadDataInsert(\"UNDO_PATH\");");
    QString rc = xv + QString("map.removeXLink(x);");

    QString com = QString("Remove XLink");
    saveState(uc, rc, com, xlink->beginXLinkItem());

    deleteXLinkInt(xlink);
}

void VymModel::deleteXLinkLater(XLink *xlink)
{
    //qDebug() << "VM::deleteXLinkLater: " << xlink;

    if (!xlinksTrash.contains(xlink)) {
        xlinksTrash << xlink;
        xlinks.removeOne(xlink);
    }
}

void VymModel::emptyXLinksTrash()
{
    //qDebug() << "*** VM::emptyXLinksTrash " << xlinksTrash;

    foreach (XLink *xlink, xlinksTrash)
        deleteXLinkInt(xlink);

    xlinksTrash.clear();
}

void VymModel::deleteXLinkInt(XLink *xlink)
{
    //qDebug() << __func__ << xlink;

    if (!xlink) {
        qWarning() << __func__ << "No xlink ?!";
        return;
    }

    // Remove XLinkItems from TreeModel
    XLinkItem *xli;
    xli = xlink->beginXLinkItem();
    if (xli) {
        //qDebug() << "  Deleting begin xli" << xli;
        xli->setXLink(nullptr);
        deleteItem(xli);
    }
    xli = xlink->endXLinkItem();
    if (xli) {
        //qDebug() << "  Deleting end xli" << xli;
        xli->setXLink(nullptr);
        deleteItem(xli);
    }

    // Remove from list of items and delete xlink itself, including XLinkObj
    if (xlinks.removeOne(xlink)) {
        //qDebug() << "  Removing xlink from xlinks";
        delete (xlink);
    }
    if (xlinksTrash.removeOne(xlink)) {
        //qDebug() << "  Removing xlink from xlinksTrash";
        delete (xlink);
    }
}

bool VymModel::scrollBranch(BranchItem *bi)
{
    if (bi) {
        if (bi->isScrolled())
            return false;
        if (bi->branchCount() == 0)
            return false;
        if (bi->depth() == 0)
            return false;
        if (bi->toggleScroll()) {
            QString u, r;
            r = setBranchVar(bi) + " b.scroll();";
            u = setBranchVar(bi) + " b.unscroll();";
            saveState(u, r, QString("Scroll %1").arg(getObjectName(bi)));
            emitDataChanged(bi);
            reposition();
            return true;
        }
    }
    return false;
}

bool VymModel::unscrollBranch(BranchItem *bi)
{
    if (bi) {
        if (!bi->isScrolled())
            return false;
        if (bi->toggleScroll()) {
            QString u, r;
            u = setBranchVar(bi) + " b.scroll();";
            r = setBranchVar(bi) + " b.unscroll();";
            saveState(u, r, QString("Uncroll %1").arg(getObjectName(bi)));
            emitDataChanged(bi);

            reposition();
            return true;
        }
    }
    return false;
}

void VymModel::toggleScroll(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        if (selbi->isScrolled())
            unscrollBranch(selbi);
        else
            scrollBranch(selbi);
        // Note: saveState & reposition are called in above functions
    }
}

void VymModel::unscrollSubtree(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        QString bv = setBranchVar(selbi);
        QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
        QString rc = bv + QString("b.unscrollSubtree();");
        QString comment = QString("Unscroll branch \"%1\" and all its scrolled children").arg(selbi->headingText());
        saveState(uc, rc, comment, selbi);

        BranchItem *prev = nullptr;
        BranchItem *cur = nullptr;
        nextBranch(cur, prev, true, selbi);
        while (cur) {
            if (cur->isScrolled()) {
                cur->toggleScroll();
                emitDataChanged(cur);
            }
            nextBranch(cur, prev, true, selbi);
        }
    }
    updateActions();
    reposition();
}

void VymModel::emitExpandAll() { emit expandAll(); }

void VymModel::emitExpandOneLevel() { emit expandOneLevel(); }

void VymModel::emitCollapseOneLevel() { emit collapseOneLevel(); }

void VymModel::emitCollapseUnselected() { emit collapseUnselected(); }

void VymModel::toggleTarget(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        selbi->toggleTarget();
        saveStateBranch(
                selbi,
                QString("toggleTarget(%1);").arg(toS(!selbi->isTarget())),
                QString("toggleTarget(%1);").arg(toS(selbi->isTarget())),
                "Toggle target flag of branch");
    }
    reposition();
}

ItemList VymModel::getLinkedMaps()
{
    ItemList targets;

    // rmodel->setSearchString (s);

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);

    QString s;

    while (cur) {
        if (cur->hasActiveSystemFlag("system-target") && cur->hasVymLink()) {
            s = cur->heading().getTextASCII();
            static QRegularExpression re;
            re.setPattern("\n+");
            s.replace(re, " ");
            re.setPattern("\\s+");
            s.replace(re, " ");
            re.setPattern("\\s+");
            s.replace(re, "");

            QStringList sl;
            sl << s;
            sl << cur->vymLink();

            targets[cur->getID()] = sl;
        }
        nextBranch(cur, prev);
    }
    return targets;
}

ItemList VymModel::getTargets()
{
    ItemList targets;

    // rmodel->setSearchString (s);

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);

    QString s;

    QRegularExpression re;
    while (cur) {
        if (cur->hasActiveSystemFlag("system-target")) {
            s = cur->heading().getTextASCII();
            re.setPattern("\n+");
            s.replace(re, " ");
            re.setPattern("\\s+");
            s.replace(re, " ");
            re.setPattern("^\\s+");
            s.replace(re, "");

            QStringList sl;
            sl << s;

            targets[cur->getID()] = sl;
        }
        nextBranch(cur, prev);
    }
    return targets;
}

Flag* VymModel::findFlagByName(const QString &name)
{
    BranchItem *bi = getSelectedBranch();

    if (bi) {
        Flag *f = standardFlagsMaster->findFlagByName(name);
        if (!f) {
            f = userFlagsMaster->findFlagByName(name);
            if (!f) {
                qWarning() << "VymModel::findFlagByName failed for flag named "
                           << name;
                return nullptr;
            }
        }
        return f;
    }

    // Nothing selected, so no flag found
    return nullptr;
}

void VymModel::setFlagByName(const QString &name, BranchItem *bi, bool useGroups)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem* selbi, selbis)
    if (!selbi->hasActiveFlag(name))
        toggleFlagByName(name, selbi, useGroups);
}

void VymModel::unsetFlagByName(const QString &name, BranchItem *bi)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem* selbi, selbis)
        if (selbi->hasActiveFlag(name))
            toggleFlagByName(name, selbi);
}

void VymModel::toggleFlagByUid( const QUuid &uid, BranchItem *bi, bool useGroups)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem* selbi, selbis) {
        // For undo save all currently set flags. (Independely of usage of flag groups)
        QList <QUuid> oldFlags = selbi->activeFlagUids();
        QStringList sl;
        foreach (QUuid id, oldFlags)
            sl << QString("\"%1\"").arg(id.toString());

        QString uc = QString("setOnlyFlags([%1]);").arg(sl.join(","));

        Flag *flag = selbi->toggleFlagByUid(uid, useGroups);
        if (flag) {
            QString fname = flag->getName();
            QString rc = QString("toggleFlagByUid(\"%1\");").arg(uid.toString());
            QString com = QString("Toggle flag %1 of %2").arg(fname, getObjectName(selbi));
            saveStateBranch(selbi, uc, rc, com);
            emitDataChanged(selbi);
        } else
            qWarning() << "VymModel::toggleFlag failed for flag with uid "
                       << uid;
    }
    reposition();
}

void VymModel::toggleFlagByName(const QString &name, BranchItem *bi, bool useGroups)
{
    QList <BranchItem*> selbis = getSelectedBranches(bi);

    foreach (BranchItem* bi, selbis) {
        Flag *flag = standardFlagsMaster->findFlagByName(name);
        if (!flag) {
            flag = userFlagsMaster->findFlagByName(name);
            if (!flag) {
                qWarning() << "VymModel::toggleFlag failed to find flag named "
                           << name;
                return;
            }
        }

        QUuid uid = flag->getUuid();

        toggleFlagByUid(uid, bi, useGroups);
    }
}

void VymModel::setOnlyFlags(QList <QUuid> uids, BranchItem *bi)
{
    if (!bi) {
        qWarning() << __FUNCTION__ << "bi == nullptr";
        return;
    }

    QList <QUuid> oldUids = bi->activeFlagUids();
    foreach (QUuid uid, oldUids)
        if (!uids.contains(uid))
            // Unset flag
            bi->toggleFlagByUid(uid);

    foreach (QUuid uid, uids)
        if (!oldUids.contains(uid))
            // Set flag
            bi->toggleFlagByUid(uid);

        QStringList sl_old;
        QStringList sl_new;
        foreach (QUuid id, oldUids)
            sl_old << QString("\"%1\"").arg(id.toString());
        foreach (QUuid id, uids)
            sl_new << QString("\"%1\"").arg(id.toString());

        QString uc = QString("setOnlyFlags([%1]);").arg(sl_old.join(","));
        QString rc = QString("setOnlyFlags([%1]);").arg(sl_new.join(","));

            QString com = QString("Set only some flags %1").arg(getObjectName(bi));
            saveStateBranch(bi, uc, rc, com);
    emitDataChanged(bi);
    reposition();
}

void VymModel::clearFlags(BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        selbi->deactivateAllStandardFlags();
        reposition();
        emitDataChanged(selbi);
        setChanged();
    }
}

void VymModel::colorBranch(QColor c, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);
    foreach (BranchItem *selbi, selbis) {
        QString uc = QString("colorBranch (\"%1\");")
                      .arg(selbi->headingColor().name());
        QString rc = QString("colorBranch (\"%1\");").arg(c.name());
        saveStateBranch(selbi, uc, rc,
                  QString("Set color of %1 to %2")
                      .arg(getObjectName(selbi), c.name()));
        selbi->setHeadingColor(c); // color branch
        selbi->getBranchContainer()->updateUpLink();
        emitDataChanged(selbi);
        taskEditor->showSelection();
    }
    mapEditor->getScene()->update();
}

void VymModel::colorSubtree(QColor c, BranchItem *bi)
{
    QList<BranchItem *> selbis = getSelectedBranches(bi);

    foreach (BranchItem *bi, selbis) {
        QString bv = setBranchVar(bi);
        QString uc = bv + "map.loadBranchReplace(\"UNDO_PATH\", b);";
        QString rc = bv + QString("b.colorSubtree (\"%1\")").arg(c.name());
        saveState(uc, rc,
                        QString("Set color of %1 and children to %2")
                          .arg(getObjectName(bi), c.name()),
                        bi, nullptr);
        BranchItem *prev = nullptr;
        BranchItem *cur = nullptr;
        nextBranch(cur, prev, true, bi);
        while (cur) {
            cur->setHeadingColor(c); // color links, color children
            cur->getBranchContainer()->updateUpLink();
            emitDataChanged(cur);
            nextBranch(cur, prev, true, bi);
        }
    }
    taskEditor->showSelection();
    mapEditor->getScene()->update();
}

QColor VymModel::getCurrentHeadingColor()
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi)
        return selbi->headingColor();

    QMessageBox::warning(
        0, "Warning",
        "Can't get color of heading,\nthere's no branch selected");
    return Qt::black;
}

void VymModel::note2URLs() // FIXME-3 No saveState yet
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi) {
        /*
        saveStateChangingPart(  // note2Urls
            selbi, selbi, QString("note2URLs()"),
            QString("Extract URLs from note of %1").arg(getObjectName(selbi)));
        */

        QString n = selbi->getNoteASCII();
        if (n.isEmpty())
            return;
        static QRegularExpression re;
        re.setPattern("(http.*)(\\s|\"|')");
        re.setPatternOptions(QRegularExpression::InvertedGreedinessOption);

        BranchItem *bi;
        int pos = 0;
        QRegularExpressionMatch match;
        while (pos >= 0) {
            match = re.match(n, pos);
            if (match.hasMatch()) {
                bi = addNewBranch(selbi);
                bi->setHeadingPlainText(match.captured(1));
                bi->setUrl(match.captured(1));
                emitDataChanged(bi);
                pos = match.capturedEnd();
            } else
                pos = -1;
        }
        reposition();
    }
}

void VymModel::editHeading2URL()
{
    TreeItem *selti = getSelectedItem();
    if (selti)
        setUrl(selti->headingPlain());
}

void VymModel::getJiraData(bool subtree, BranchItem *bi)
// getJiraData FIXME-3 check if jiraClientAvail is set correctly
{
    if (!JiraAgent::available()) {
        WarningDialog dia;
        QString w = QObject::tr("JIRA agent not setup.");
        dia.setText(w);
        dia.setWindowTitle( tr("Warning") + ": " + w);
        dia.setShowAgainName("/JiraAgent/notdefined");
        dia.exec();

        if (!mainWindow->settingsJIRA())
            return;
    }

    BranchItem *selbi = getSelectedBranch(bi);

    if (selbi) {
        BranchItem *prev = nullptr;
        BranchItem *cur = nullptr;
        nextBranch(cur, prev, true, selbi);
        while (cur) {
            QString query = cur->attributeValue("Jira.query").toString();

            bool startAgent = false;
            JiraAgent *agent = new JiraAgent;
            if (!agent->setBranch(cur))
                qWarning () << "Could not set branch in JiraAgent to " << cur->headingPlain();
            else {
                if (!query.isEmpty() && agent->setQuery(query)) {
                    // Branch has a query defined in attribute, try to use that
                    agent->setJobType(JiraAgent::Query);
                    connect(agent, &JiraAgent::jiraQueryReady, this, &VymModel::processJiraJqlQuery);
                    startAgent = true;
                } else {
                    QString key = cur->attributeValue("Jira.key").toString();
                    if (!key.isEmpty() && agent->setTicket(key)) {
                        // Branch has issueKey, get info for ticket
                        startAgent = true;
                        connect(agent, &JiraAgent::jiraTicketReady, this, &VymModel::processJiraTicket);
                    } else {
                        if (agent->setTicket(cur->headingPlain())) {
                            connect(agent, &JiraAgent::jiraTicketReady, this, &VymModel::processJiraTicket);
                            startAgent = true;
                        } else {
                            mainWindow->statusMessage(tr("Could not setup JiraAgent to retrieve data from Jira"));
                        }
                    }
                }
            } // Successfully set branch in agent

            if (startAgent) {
                agent->startJob();
                mainWindow->statusMessage(tr("Contacting Jira...", "VymModel"));
            } else
                delete agent;

            if (subtree)
                nextBranch(cur, prev, true, selbi);
            else
                cur = nullptr;
        }
    }
}

void VymModel::initAttributesFromJiraIssue(BranchItem *bi, const JiraIssue &ji)
{
    if (!bi) {
        qWarning() << __FUNCTION__ << " called without BranchItem";
        return;
    }

    setAttribute(bi, "Jira.assignee", ji.assignee());
    setAttribute(bi, "Jira.components", ji.components());
    //setAttribute(bi, "Jira.description", ji.description());
    bi->setNote(VymText(ji.description()));
    setAttribute(bi, "Jira.fixVersions", ji.fixVersions());
    setAttribute(bi, "Jira.issuetype", ji.issueType());
    setAttribute(bi, "Jira.issueUrl", ji.url());
    setAttribute(bi, "Jira.key", ji.key());
    setAttribute(bi, "Jira.parentKey", ji.parentKey());
    setAttribute(bi, "Jira.status", ji.status());
    setAttribute(bi, "Jira.reporter", ji.reporter());
    setAttribute(bi, "Jira.resolution", ji.resolution());
    setAttribute(bi, "Jira.issueLinks", ji.issueLinks());
    setAttribute(bi, "Jira.subTasks", ji.subTasks());
}

void VymModel::updateJiraFlag(TreeItem *ti)
{
    if(!ti) return;

    AttributeItem *ai = getAttributeByKey("Jira.issueUrl", ti);
    if (ai)
        ti->setUrlType(TreeItem::JiraUrl);
    else {
        ai = getAttributeByKey("Jira.query");
        if (ai)
            ti->setUrlType(TreeItem::JiraUrl);  // FIXME-3 Dedicated flag for query missing
        else {
            if (ti->url().isEmpty())
                ti->setUrlType(TreeItem::NoUrl);
            else
                ti->setUrlType(TreeItem::GeneralUrl);
        }
    }

    // Update UrlType
    if (ti->urlType() == TreeItem::JiraUrl) {
        ti->activateSystemFlagByName("system-jira");
        ti->deactivateSystemFlagByName("system-url");
    } else {
        ti->deactivateSystemFlagByName("system-jira");
        if (ti->urlType() == TreeItem::GeneralUrl)
            ti->activateSystemFlagByName("system-url");
        else
            ti->deactivateSystemFlagByName("system-url");
    }
    emitDataChanged(ti);
}


void VymModel::processJiraTicket(QJsonObject jsobj)
{
    int branchID = jsobj["vymBranchId"].toInt();

    repositionBlocked = true; // FIXME-2 block reposition during processing of Jira query?

    BranchItem *bi = (BranchItem*)findID(branchID);
    if (bi) {
        JiraIssue ji;
        ji.initFromJsonObject(jsobj);
        initAttributesFromJiraIssue(bi, ji);

        QString keyName = ji.key();
        if (ji.isFinished())    {
            keyName = "(" + keyName + ")";
            colorSubtree (vymBlue, bi);
        }

        setHeadingPlainText(keyName + ": " + ji.summary(), bi);
        setUrl(ji.url(), false, bi);
        bi->setUrlType(TreeItem::JiraUrl);
        updateJiraFlag(bi);

        emitDataChanged(bi);

        // Pretty print JIRA ticket
        ji.print();
    }

    repositionBlocked = false;

    mainWindow->statusMessage(tr("Received Jira data.", "VymModel"));

    // Update note
    reselect();

    reposition();
}

void VymModel::processJiraJqlQuery(QJsonObject jsobj)
{
    // Debugging only
    //qDebug() << "VM::processJiraJqlQuery result...";

    int branchId = jsobj.value("vymBranchId").toInt();
    BranchItem *bi = (BranchItem*)findID(branchId);
    if (!bi) {
        mainWindow->statusMessage("VM::processJiraJqlQUery could not find branch with ID=" + jsobj.value("vymBranchId").toString());
        return;
    }
    QJsonArray issues = jsobj["issues"].toArray();

    QString bv = setBranchVar(bi);
    QString uc = bv + QString("map.loadBranchReplace(\"UNDO_PATH\", b);");
    QString rc = bv + QString("b.getJiraData(%1);").arg(toS(jsobj["doSubtree"].toBool()));
    QString comment = QString("Process Jira Jql query on \"%1\"").arg(bi->headingText());
    saveState(uc, rc, comment, bi->parentBranch());


    saveStateBlocked = true;
    repositionBlocked = true;

    for (int i = 0; i < issues.size(); ++i) {
        QJsonObject issue = issues[i].toObject();
        JiraIssue ji(issue);

        BranchItem *bi2 = addNewBranchInt(bi, -2);
        if (bi2) {
            QString keyName = ji.key();
            if (ji.isFinished())    {
                keyName = "(" + keyName + ")";
                colorSubtree (vymBlue, bi2);
            }

            setHeadingPlainText(keyName + ": " + ji.summary(), bi2);
            setUrl(ji.url(), false, bi2);
            initAttributesFromJiraIssue(bi2, ji);
        }

        // Pretty print JIRA ticket
        // ji.print();
    }

    setAttribute(bi, "Jira.query", jsobj["vymJiraLastQuery"].toString());

    saveStateBlocked = false;
    repositionBlocked = false;

    mainWindow->statusMessage(tr("Received Jira data.", "VymModel"));

    reposition();
}

void VymModel::setConfluencePageDetails(bool recursive)
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi) {
        QString url = selbi->url();
        if (!url.isEmpty() &&
                settings.contains("/atlassian/confluence/url") &&
                url.contains(settings.value("/atlassian/confluence/url").toString())) {

            ConfluenceAgent *ca_setHeading = new ConfluenceAgent(selbi);
            ca_setHeading->setPageURL(url);
            if (recursive)
                ca_setHeading->setJobType(ConfluenceAgent::GetPageDetailsRecursively);
            else
                ca_setHeading->setJobType(ConfluenceAgent::GetPageDetails);
            ca_setHeading->startJob();
        }
    }
}

void VymModel::setVymLink(const QString &s, BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        QString uc = QString("setVymLink(\"%1\");").arg(selbi->vymLink());
        QString rc = QString("setVymLink(\"%1\");").arg(s);
        saveStateBranch(selbi, uc, rc,
            QString("Set vymlink of %1 to %2").arg(getObjectName(selbi), s));
        selbi->setVymLink(s);
        emitDataChanged(selbi);
        reposition();
    }
}

void VymModel::deleteVymLink()
{
    setVymLink("");
}

QString VymModel::getVymLink()
{
    BranchItem *bi = getSelectedBranch();
    if (bi)
        return bi->vymLink();
    else
        return "";
}

QStringList VymModel::getVymLinks()
{
    QStringList links;
    BranchItem *selbi = getSelectedBranch();
    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev, true, selbi);
    while (cur) {
        if (cur->hasVymLink())
            links.append(cur->vymLink());
        nextBranch(cur, prev, true, selbi);
    }
    return links;
}

void VymModel::followXLink(int i)
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi) {
        selbi = selbi->getXLinkItemNum(i)->getPartnerBranch();
        if (selbi)
            select(selbi);
    }
}

void VymModel::editXLink()
{
    XLink *xlink = getSelectedXLink();
    if (xlink) {
        EditXLinkDialog dia;
        dia.setLink(xlink);
        if (dia.exec() == QDialog::Accepted) {
            if (dia.useSettingsGlobal()) {
                setDefXLinkPen(xlink->getPen());
                setDefXLinkStyleBegin(xlink->getStyleBeginString());
                setDefXLinkStyleEnd(xlink->getStyleEndString());
            }
        }
    }
}

void VymModel::setXLinkColor(const QString &new_col, XLink *xl)
{
    XLink *xlink = getSelectedXLink(xl);
    if (xlink) {
        QPen pen = xlink->getPen();
        QColor new_color = QColor(new_col);
        QColor old_color = pen.color();
        if (new_color == old_color)
            return;
        QString xv = setXLinkVar(xlink, "xl");
        QString uc = xv + QString("xl.setColor(\"%1\");").arg(old_color.name());
        QString rc = xv + QString("xl.setColor(\"%1\");").arg(new_color.name());
        QString com = QString("Set xlink color to \"%1\"").arg(new_color.name());
        saveState(uc, rc, com);

        pen.setColor(new_color);
        xlink->setPen(pen);
    }
}

void VymModel::setXLinkStyle(const QString &new_style, XLink *xl)
{
    XLink *xlink = getSelectedXLink(xl);
    if (xlink) {
        QPen pen = xlink->getPen();
        QString old_style = penStyleToString(pen.style());
        if (new_style == old_style)
            return;
        QString xv = setXLinkVar(xlink, "xl");
        QString uc = xv + QString("xl.setStyle(\"%1\");").arg(old_style);
        QString rc = xv + QString("xl.setStyle(\"%1\");").arg(new_style);
        QString com = QString("Set xlink style to \"%1\"").arg(new_style);
        saveState(uc, rc, com);

        bool ok;
        pen.setStyle(penStyle(new_style, ok));
        xlink->setPen(pen);
    }
}

void VymModel::setXLinkStyleBegin(const QString &new_style, XLink *xl)
{
    XLink *xlink = getSelectedXLink(xl);
    if (xlink) {
        QString old_style = xlink->getStyleBeginString();
        if (new_style == old_style)
            return;
        QString xv = setXLinkVar(xlink, "xl");
        QString uc = xv + QString("xl.setStyleBegin(\"%1\");").arg(old_style);
        QString rc = xv + QString("xl.setStyleBegin(\"%1\");").arg(new_style);
        QString com = QString("Set xlink begin style to \"%1\"").arg(new_style);
        saveState(uc, rc, com);
        xlink->setStyleBegin(new_style);
    }
}

void VymModel::setXLinkStyleEnd(const QString &new_style, XLink *xl)
{
    XLink *xlink = getSelectedXLink(xl);
    if (xlink) {
        QString old_style = xlink->getStyleEndString();
        if (new_style == old_style)
            return;

        QString xv = setXLinkVar(xlink, "xl");
        QString uc = xv + QString("xl.setStyleEnd(\"%1\");").arg(old_style);
        QString rc = xv + QString("xl.setStyleEnd(\"%1\");").arg(new_style);
        QString com = QString("Set xlink end style to \"%1\"").arg(new_style);
        saveState(uc, rc, com);
        xlink->setStyleEnd(new_style);
    }
}

void VymModel::setXLinkWidth(int new_width, XLink *xl)
{
    XLink *xlink = getSelectedXLink(xl);
    if (xlink) {
        QPen pen = xlink->getPen();
        int old_width = pen.width();
        if (new_width == old_width)
            return;
        QString xv = setXLinkVar(xlink, "xl");
        QString uc = xv + QString("xl.setWidth(%1);").arg(old_width);
        QString rc = xv + QString("xl.setWidth(%1);").arg(new_width);
        QString com = QString("Add xlink width to \"%1\"").arg(new_width);
        saveState(uc, rc, com);

        pen.setWidth(new_width);
        xlink->setPen(pen);
    }
}

//////////////////////////////////////////////
// Scripting
//////////////////////////////////////////////

QVariant VymModel::execute( const QString &script) // FIXME-0 really still needed?
                           // Called from these places:
                           //
                           // scripts/vym-ruby.rb  (and adaptormodel) used for
                           // testing Main::callMacro Main::checkReleaseNotes
                           // VymModel::undo
                           // VymModel::redo
                           // VymModel::exportLast
                           // VymModel::updateSlideSelection
{
    // qDebug()<<"VM::execute called: "<<script;
    return mainWindow->runScript(script);
}

void VymModel::setExportMode(bool b)
{
    // should be called before and after exports
    // depending on the settings
    if (b && settings.value("/export/useHideExport", "true") == "true")
        setHideTmpMode(TreeItem::HideExport);
    else
        setHideTmpMode(TreeItem::HideNone);
}

QPointF VymModel::exportImage(QString fname, bool askName, QString format)  // FIXME-2 Ignores hiddenExportParent
{
    QPointF offset; // set later, when getting image from MapEditor

    if (fname == "") {
        if (!askName) {
            qWarning("VymModel::exportImage called without filename (and "
                     "askName==false)");
            return offset;
        }

        fname = lastImageDir.absolutePath() + "/" + getMapName() + ".png";
        format = "PNG";
    }

    ExportBase ex;
    ex.setName("Image");
    ex.setModel(this);
    ex.setFilePath(fname);
    ex.setWindowTitle(tr("Export map as image"));
    ex.addFilter(
        "PNG (*.png);;All (* *.*)"); //  imageIO.getFilters().join(";;")
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (askName) {
        if (!ex.execDialog())
            return offset;
        fname = ex.getFilePath();
        lastImageDir = QDir(fname);
    }

    setExportMode(true);

    mapEditor->minimizeView();

    QImage img(mapEditor->getImage(offset));
    if (!img.save(fname, format.toLocal8Bit())) {
        QMessageBox::critical(
            0, tr("Critical Error"),
            tr("Couldn't save QImage %1 in format %2").arg(fname, format));
        ex.setResult(ExportBase::Failed);
    } else
        ex.setResult(ExportBase::Success);

    setExportMode(false);

    ex.completeExport();

    return offset;
}

void VymModel::exportPDF(QString fname, bool askName)
{
    if (fname == "") {
        if (!askName) {
            qWarning("VymModel::exportPDF called without filename (and "
                     "askName==false)");
            return;
        }

        fname = lastExportDir.absolutePath() + "/" + getMapName() + ".pdf";
    }

    ExportBase ex;
    ex.setName("PDF");
    ex.setModel(this);
    ex.setFilePath(fname);
    ex.setWindowTitle(tr("Export map as PDF"));
    ex.addFilter("PDF (*.pdf);;All (* *.*)");
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (askName) {
        if (!ex.execDialog())
            return;
        fname = ex.getFilePath();
    }

    setExportMode(true);

    // To PDF
    QPrinter pdfPrinter(QPrinter::HighResolution);
    pdfPrinter.setOutputFormat(QPrinter::PdfFormat);
    pdfPrinter.setOutputFileName(fname);
    pdfPrinter.setPageSize(QPageSize(QPageSize::A3));
    QRectF bbox = mapEditor->getTotalBBox();

    if (bbox.width() > bbox.height())
        // recommend landscape
        pdfPrinter.setPageOrientation(QPageLayout::Landscape);
    else
        // recommend portrait
        pdfPrinter.setPageOrientation(QPageLayout::Portrait);

    QPainter *pdfPainter = new QPainter(&pdfPrinter);
    getScene()->render(pdfPainter);
    pdfPainter->end();
    delete pdfPainter;

    setExportMode(false);

    ex.completeExport();
}

QPointF VymModel::exportSVG(QString fname, bool askName)
{
    QPointF offset; // FIXME-3 not needed?

    if (fname == "") {
        if (!askName) {
            qWarning("VymModel::exportSVG called without filename (and "
                     "askName==false)");
            return offset;
        }

        fname = lastImageDir.absolutePath() + "/" + getMapName() + ".svg";
    }

    ExportBase ex;
    ex.setName("SVG");
    ex.setModel(this);
    ex.setFilePath(fname);
    ex.setWindowTitle(tr("Export map as SVG"));
    ex.addFilter("SVG (*.svg);;All (* *.*)");

    if (askName) {
        if (!ex.execDialog())
            return offset;
        fname = ex.getFilePath();
        lastImageDir = QDir(fname);
    }

    setExportMode(true);

    QSvgGenerator generator;
    generator.setFileName(fname);
    QSize sceneSize = getScene()->sceneRect().size().toSize();
    generator.setSize(sceneSize);
    generator.setViewBox(QRect(0, 0, sceneSize.width(), sceneSize.height()));
    QPainter *svgPainter = new QPainter(&generator);
    getScene()->render(svgPainter);
    svgPainter->end();
    delete svgPainter;

    setExportMode(false);
    ex.completeExport();

    return offset;
}

void VymModel::exportTaskJuggler(QString fname, bool askForName) // FIXME-3 not scriptable yet
{
    if (fname == "") {
        if (!askForName) {
            qWarning("VymModel::exportTaskJuggler called without filename (and "
                     "askName==false)");
            return;
        }

        fname = lastImageDir.absolutePath() + "/" + getMapName() + ".svg";
    }

    ExportTaskJuggler ex;
    ex.setModel(this);
    ex.setFilePath(fname);
    ex.setWindowTitle(vymName + " - " + tr("Export to") + " Taskjuggler" +
                          tr("(still experimental)"));
    ex.addFilter("Taskjuggler (*.tjp)");

    if (askForName) {
        if (!ex.execDialog())
            return;
        fname = ex.getFilePath();
        lastImageDir = QDir(fname);
    }

    setExportMode(true);

    ex.doExport();

    setExportMode(false);

    ex.completeExport();
}

void VymModel::exportXML(QString fpath, bool useDialog)
{
    ExportBase ex;
    ex.setName("XML");
    ex.setModel(this);
    ex.setWindowTitle(tr("Export map as XML"));
    ex.addFilter("XML (*.xml);;All (* *.*)");
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (useDialog) {
        QFileDialog fd;
        fd.setWindowTitle(vymName + " - " + tr("Export XML to directory"));
        QStringList filters;
        filters << "XML data (*.xml)";
        fd.setNameFilters(filters);
        fd.setOption(QFileDialog::DontConfirmOverwrite, true);
        fd.setAcceptMode(QFileDialog::AcceptSave);
        fd.selectFile(mapName + ".xml");

        if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
            return;

        fpath = fd.selectedFiles().first();

    }
    QString dpath = fpath.left(fpath.lastIndexOf("/"));
    if (!confirmDirectoryOverwrite(QDir(dpath)))
        return;

    ex.setFilePath(fpath);

    QString mname = basename(fpath);

    // Hide stuff during export, if settings want this
    setExportMode(true);

    // Create subdirectories
    makeSubDirs(dpath);

    // write image and calculate offset (Remember old mapSaved setting while
    // exporting image)
    bool mchanged = mapChanged;
    bool munsaved = mapUnsaved;

    QPointF offset =
        exportImage(dpath + "/images/" + mname + ".png", false, "PNG");

    mapChanged = mchanged;
    mapUnsaved = munsaved;

    // write to directory   //FIXME-3 check totalBBox here...
    exportBoundingBoxes = true;
    QString saveFile =
        saveToDir(dpath, mname + "-", FlagRowMaster::AllFlags, offset, nullptr);
    exportBoundingBoxes = false;

    QFile file;

    file.setFileName(fpath);
    if (!file.open(QIODevice::WriteOnly)) {
        // This should neverever happen
        QMessageBox::critical(0, tr("Critical Export Error"),
                              QString("VymModel::exportXML couldn't open %1")
                                  .arg(file.fileName()));
        return;
    }

    // Write it finally, and write in UTF8, no matter what
    QTextStream ts(&file);
    ts << saveFile;
    file.close();


    ex.setResult(ExportBase::Success);

    setExportMode(false);

    QStringList args;
    args << fpath;
    args << dpath;
    ex.completeExport(args);
}

void VymModel::exportAO(QString fname, bool askName)
{
    ExportAO ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".txt");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.setDirPath(lastExportDir.absolutePath());
        ex.execDialog();
    }
    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportASCII(const QString &fname, bool listTasks, bool askName)
{
    ExportASCII ex;
    ex.setModel(this);
    ex.setListTasks(listTasks);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".txt");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.setDirPath(lastExportDir.absolutePath());
        ex.execDialog();
    }

    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportCSV(const QString &fname, bool askName)
{
    ExportCSV ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".csv");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.addFilter("CSV (*.csv);;All (* *.*)");
        ex.setDirPath(lastExportDir.absolutePath());
        ex.setWindowTitle(vymName + " -" + tr("Export as csv") + " " +
                          tr("(still experimental)"));
        ex.execDialog();
    }

    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportFirefoxBookmarks(const QString &fname, bool askName)
{
    ExportFirefox ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".csv");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.addFilter("JSON (*.json);;All (* *.*)");
        ex.setDirPath(lastExportDir.absolutePath());
        ex.setWindowTitle(vymName + " -" + tr("Export as csv") + " " +
                          tr("(still experimental)"));
        ex.execDialog();
    }

    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportHTML(const QString &fpath, const QString &dpath,
                          bool useDialog)
{
    ExportHTML ex(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (!dpath.isEmpty())
        ex.setDirPath(dpath);
    if (!fpath.isEmpty())
        ex.setFilePath(fpath);

    exportBoundingBoxes = true;
    ex.doExport(useDialog);
    exportBoundingBoxes = false;
}

void VymModel::exportConfluence(bool createPage, const QString &pageURL,
                                const QString &pageName, bool useDialog)
{
    ExportConfluence ex(this);
    ex.setCreateNewPage(createPage);
    ex.setUrl(pageURL);
    ex.setPageName(pageName);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    ex.doExport(useDialog);
}

void VymModel::exportImpress(QString fname, QString configFile, bool useDialog)
{
    if (fname == "")
        fname = mapName + ".odp";

    if (useDialog) {
        ExportImpressFileDialog fd;
        fd.setWindowTitle(vymName + " - " + tr("Export to") + " LibreOffice Impress");
        fd.setDirectory(lastExportDir);
        fd.setAcceptMode(QFileDialog::AcceptSave);
        fd.setFileMode(QFileDialog::AnyFile);
        if (fd.foundConfig()) {
            if (fd.exec() == QDialog::Accepted) {
                configFile = fd.selectedConfig();
                if (!fd.selectedFiles().isEmpty()) {
                    QString fname = fd.selectedFiles().first();
                    if (!fname.contains(".odp"))
                        fname += ".odp";
                }
            }
        }
        else {
            QMessageBox::warning(
                0, tr("Warning"),
                tr("Couldn't find configuration for export to LibreOffice Impress\n"));
            return;
        }
    }


    ExportImpress ex;
    ex.setFilePath(fname);

    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (ex.setConfigFile(configFile)) {
        QString lastCommand =
            settings.localValue(filePath, "/export/last/command", "")
                .toString();

        setExportMode(true);
        ex.exportPresentation();
        setExportMode(false);

        QString command =
            settings.localValue(filePath, "/export/last/command", "")
                .toString();
        if (lastCommand != command)
            setChanged();

        //lastExportDir = fname.left(fname.findRev ("/"));
    }
}

bool VymModel::exportLastAvailable(QString &description, QString &command,
                                   QString &dest)
{
    command =
        settings.localValue(filePath, "/export/last/command", "").toString();
    static QRegularExpression re;
    re.setPattern("exportMap\\((\".*)\\)");
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString matched = match.captured(1); // matched == "23 def"
        command = QString("vym.currentMap().exportMap([%1]);").arg(match.captured(1));
        settings.setLocalValue(filePath, "/export/last/command", command);
        qDebug() << "Rewriting last export command to version " << vymVersion << " format: " << command;
    }

    description = settings.localValue(filePath, "/export/last/description", "")
                      .toString();
    dest = settings.localValue(filePath, "/export/last/displayedDestination", "")
               .toString();
    if (!command.isEmpty() && command.contains("exportMap"))
        return true;
    else
        return false;
}

void VymModel::setExportLastCommand(const QString &cmd)
{
    settings.setLocalValue(filePath, "/export/last/command", cmd);
}

void VymModel::setExportLastDescription(const QString &desc)
{
    settings.setLocalValue(filePath, "/export/last/description", desc);
}

void VymModel::setExportLastDestination(const QString &displayedDest)
{
    settings.setLocalValue(filePath, "/export/last/displayedDestination", displayedDest);
}

void VymModel::exportLast()
{
    QString desc, command,
        dest; // FIXME-3 better integrate configFile into command
    if (exportLastAvailable(desc, command, dest)) {
        //qDebug() << "VM::exportLast: " << command;
        execute(command);
    }
}

void VymModel::exportLaTeX(const QString &fname, bool askName)
{
    ExportLaTeX ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".tex");
    else
        ex.setFilePath(fname);

    if (askName)
        ex.execDialog();
    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportOrgMode(const QString &fname, bool askName)
{
    ExportOrgMode ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".org");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.setDirPath(lastExportDir.absolutePath());
        ex.execDialog();
    }

    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}

void VymModel::exportMarkdown(const QString &fname, bool askName)
{
    ExportMarkdown ex;
    ex.setModel(this);
    ex.setLastCommand(
        settings.localValue(filePath, "/export/last/command", "").toString());

    if (fname == "")
        ex.setFilePath(mapName + ".md");
    else
        ex.setFilePath(fname);

    if (askName) {
        ex.setDirPath(lastExportDir.absolutePath());
        ex.execDialog();
    }

    if (!ex.canceled()) {
        setExportMode(true);
        ex.doExport();
        setExportMode(false);
    }
}
//////////////////////////////////////////////
// View related
//////////////////////////////////////////////

void VymModel::registerMapEditor(QWidget *e) { mapEditor = (MapEditor *)e; }

void VymModel::setMapZoomFactor(const double &d)
{
    zoomFactor = d;
    mapEditor->setZoomFactorTarget(d);
}

void VymModel::setMapRotation(const double &a)
{
    if (a < 1)
        // Round to zero, otherwise selectionMode in MapEditor might be 
        // "Geometric" when it should be "Classic"
        mapRotationInt = 0;
    else
        mapRotationInt = a;
    mapEditor->setRotationTarget(mapRotationInt);
}

void VymModel::setMapAnimDuration(const int &d) { animDuration = d; }

void VymModel::setMapAnimCurve(const QEasingCurve &c) { animCurve = c; }

bool VymModel::centerOnID(const QString &id)
{
    TreeItem *ti = findUuid(QUuid(id));
    if (ti && (ti->hasTypeBranch() || ti->hasTypeImage())) {
        Container *c = ((MapItem*)ti)->getContainer();
        if (c && zoomFactor > 0 ) {
            mapEditor->setViewCenterTarget(c->mapToScene(c->rect().center()), zoomFactor,
                                           mapRotationInt, animDuration,
                                           animCurve);
            return true;
        }
    }
    return false;
}

void VymModel::setContextPos(QPointF p)
{
    contextPos = p;
    hasContextPos = true;
}

void VymModel::unsetContextPos()
{
    contextPos = QPointF();
    hasContextPos = false;
}

void VymModel::reposition(bool force)
{
    if (!force && repositionBlocked)
        return;

    // qDebug() << "VM::reposition start force=" << force; // FIXME-2 check when and how often reposition  is called

    // Reposition containers
    BranchItem *bi;
    for (int i = 0; i < rootItem->branchCount(); i++) {
        bi = rootItem->getBranchNum(i);
        bi->repositionContainers();
    }

    repositionXLinks();

    mapEditor->minimizeView();  // FIXME-2 really needed every time?

    // FIXME-4 needed? everytime? mapEditor->minimizeView();
    //qDebug() << "VM::reposition end";
    if (force)
        qApp->processEvents();
}

void VymModel::repositionXLinks()
{
    // Reposition xlinks
    foreach (XLink *xlink, xlinks)
        xlink->updateXLink();
}

MapDesign* VymModel::mapDesign()
{
    return mapDesignInt;
}

void VymModel::applyDesign(     // FIXME-1 Check handling of autoDesign option
        MapDesign::UpdateMode updateMode,
        BranchItem *bi)
{
    /*
    qDebug() << "VM::applyDesign  mode="
        << MapDesign::updateModeString(updateMode)
        << " of " << headingText(bi);
    */

    QList<BranchItem *> selbis = getSelectedBranches(bi);

    bool saveStateBlockedOrg = saveStateBlocked;
    saveStateBlocked = true;

    bool updateRequired;
    foreach (BranchItem *selbi, selbis) {
        int depth = selbi->depth();
        bool selbiChanged = false;
        BranchContainer *bc = selbi->getBranchContainer();

        // Color of heading
        QColor col = mapDesignInt->headingColor(
                        updateMode,
                        selbi,
                        updateRequired);

        if (updateRequired)
            colorBranch(col, selbi);

        // Frames   // FIXME-2 mapDesign missing for penWidth
        if (updateMode == MapDesign::CreatedByUser ||
                (updateMode == MapDesign::RelinkedByUser && mapDesignInt->updateFrameWhenRelinking(true, depth))) {
            bc->setFrameType(true, mapDesignInt->frameType(true, depth));
            bc->setFrameBrushColor(true, mapDesignInt->frameBrushColor(true, depth));
            bc->setFramePenColor(true, mapDesignInt->framePenColor(true, depth));
            bc->setFramePenWidth(true, mapDesignInt->framePenWidth(true, depth));
        }
        if (updateMode == MapDesign::CreatedByUser ||
                (updateMode == MapDesign::RelinkedByUser && mapDesignInt->updateFrameWhenRelinking(false, depth))) {
            bc->setFrameType(false, mapDesignInt->frameType(false, depth));
            bc->setFrameBrushColor(false, mapDesignInt->frameBrushColor(false, depth));
            bc->setFramePenColor(false, mapDesignInt->framePenColor(false, depth));
            bc->setFramePenWidth(false, mapDesignInt->framePenWidth(false, depth));
        }

        // Column width and font
        if (updateMode & MapDesign::CreatedByUser || updateMode & MapDesign::LoadingMap) {
            HeadingContainer *hc = bc->getHeadingContainer();
            hc->setColumnWidth(mapDesignInt->headingColumnWidth(depth));
            hc->setFont(mapDesignInt->font());
        }

        // Layouts
        if (bc->branchesContainerAutoLayout) {
                bc->setBranchesContainerLayout(
                        mapDesignInt->branchesContainerLayout(depth));
                selbiChanged = true;
                        mapDesignInt->branchesContainerLayout(depth);
        }

        if (bc->imagesContainerAutoLayout) {
                bc->setImagesContainerLayout(
                        mapDesignInt->imagesContainerLayout(depth));
                selbiChanged = true;
        }
	bc->setBranchesContainerVerticalAlignment(
		mapDesignInt->branchesContainerVerticalAlignment(depth));
	bc->setBranchesContainerAndOrnamentsVertical(
		mapDesignInt->branchesContainerAndOrnamentsVertical(depth));

        // Links and bottomlines
        bc->updateUpLink();


        // Rotations
        if (bc->rotationsAutoDesign()) {
            qreal a = mapDesignInt->rotationHeading(depth);
            if (a != bc->rotationHeading()) {
                bc->setRotationHeading(a);
                selbiChanged = true;
            }
            a = mapDesignInt->rotationSubtree(depth);
            if (a != bc->rotationHeading()) {
                bc->setRotationHeading(a);
                selbiChanged = true;
            }
        }

        if (bc->scaleAutoDesign()) {
            qreal z = mapDesignInt->scaleHeading(depth);
            if (z != bc->scaleHeading()) {
                bc->setScaleHeading(z);
                selbiChanged = true;
            }
            z = mapDesignInt->scaleSubtree(depth);
            if (z != bc->scaleSubtree()) {
                bc->setScaleSubtree(z);
                selbiChanged = true;
            }
        }

        if (selbiChanged)
            emitDataChanged(selbi);
    }

    saveStateBlocked = saveStateBlockedOrg;
}

void VymModel::applyDesignRecursively(
        MapDesign::UpdateMode updateMode,
        BranchItem *bi)
{
    if (!bi) return;

    for (int i = 0; i < bi->branchCount(); ++i)
        applyDesign(updateMode, bi->getBranchNum(i));
}

void VymModel::setDefaultFont(const QFont &font)    // FIXME-3 no saveState, no updates of existing headings ("applyDesign")
{
    mapDesignInt->setFont(font);
}

bool VymModel::setLinkStyle(const QString &newStyleString, int depth) // FIXME-2 saveState needs to be adapted, command new param depth
                                                                      // See also mainWindow->updateActions context menu
                                                                      // FIXME MapDesign setting with depth passed as argument is moved to MapDesign when parsing .xml
                                                                      // Somehow MD needs to return undo command when setting an element
{
    // Default depth == -1 is used for legacy styles from version < 2.9.518
    // or for using global setting from context menu

    if (depth >= 0) {
        QString currentStyleString = LinkObj::styleString(mapDesignInt->linkStyle(depth));

        saveState(QString("map.setLinkStyle (\"%1\");").arg(newStyleString),
                  QString("map.setLinkStyle (\"%1\");").arg(currentStyleString),
                  QString("Set map link style (\"%1\")").arg(newStyleString));
    }

    auto style = LinkObj::styleFromString(newStyleString);

    mapDesignInt->setLinkStyle(style, depth);

    // If whole map is used e.g. for legacy maps, only apply the "thick" part
    // on first level
    if (depth < 0) {
        if (style == LinkObj::PolyLine) 
            mapDesignInt->setLinkStyle(LinkObj::Line, 1);
        else if (style == LinkObj::PolyParabel) 
            mapDesignInt->setLinkStyle(LinkObj::Parabel, 1);
    }

    applyDesignRecursively(MapDesign::LinkStyleChanged, rootItem);
    reposition();

    return true;
}

uint VymModel::modelId() { return modelIdInt; }

void VymModel::setView(VymView *vv) { vymView = vv; }

void VymModel::setDefaultLinkColor(const QColor &col)   // FIXME-2 Missing command?
{
    if (!col.isValid()) return;

    saveState(
        QString("map.setDefaultLinkColor (\"%1\");").arg(mapDesignInt->defaultLinkColor().name()),
        QString("map.setDefaultLinkColor (\"%1\");").arg(col.name()),
        QString("Set map link color to %1").arg(col.name()));

    mapDesignInt->setDefaultLinkColor(col);

    // Set color for "link arrows" in TreeEditor
    vymView->updateColors();

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        BranchContainer *bc = cur->getBranchContainer();
        bc->updateUpLink();
        // for (int i = 0; i < cur->imageCount(); ++i)
        // FIXME-2 images not supported yet cur->getImageNum(i)->getLMO()->setLinkColor(col);

        nextBranch(cur, prev);
    }
    updateActions();
}

void VymModel::setLinkColorHint(const LinkObj::ColorHint &hint)  // FIXME-2 saveState missing. No MapDesign yet.
{
    mapDesignInt->setLinkColorHint(hint);

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev);
    while (cur) {
        BranchContainer *bc = cur->getBranchContainer();
        LinkObj *upLink = bc->getLink();
        if (upLink)
            upLink->setLinkColorHint(hint);

        // FIXME-2 setLinkColorHint: image link color not supported yet
        //for (int i = 0; i < cur->imageCount(); ++i)
        //    cur->getImageNum(i)->getLMO()->setLinkColor();
        //
        nextBranch(cur, prev);
    }

    applyDesignRecursively(MapDesign::LinkStyleChanged, rootItem);
    reposition();
}

void VymModel::toggleLinkColorHint()
{
    if (mapDesignInt->linkColorHint() == LinkObj::HeadingColor)
        setLinkColorHint(LinkObj::DefaultColor);
    else
        setLinkColorHint(LinkObj::HeadingColor);
}

void VymModel::setBackgroundColor(QColor col)   // FIXME-3 Missing saveState command?
{
    QColor oldcol = mapDesignInt->backgroundColor();
    saveState(QString("map.setBackgroundColor (\"%1\");").arg(oldcol.name()),
              QString("map.setBackgroundColor (\"%1\");").arg(col.name()),
              QString("Set background color of map to %1").arg(col.name()));
    mapDesignInt->setBackgroundColor(col);  // Used for backroundRole in TreeModel::data()
    vymView->updateColors();
}

bool VymModel::setBackgroundImage( const QString &fn)   // FIXME-3 missing saveState
{
    /*
    QColor oldcol=mapEditor->getScene()->backgroundBrush().color();
    saveState( selection, QString ("setBackgroundImage (%1)").arg(oldcol.name()),
    selection,
    QString ("setBackgroundImage (%1)").arg(col.name()),
    QString("Set background color of map to %1").arg(col.name()));
    */

    // FIXME-3 maybe also use: view.setCacheMode(QGraphicsView::CacheBackground);

    if (mapDesignInt->setBackgroundImage(fn)) {
        vymView->updateColors();
        return true;
    } else
        return false;
}

void VymModel::setBackgroundImageName( const QString &s) // FIXME-3 missing saveState
{
    mapDesignInt->setBackgroundImageName(s);
}

void VymModel::unsetBackgroundImage()   // FIXME-3 missing saveState
{
    /*
    QColor oldcol=mapEditor->getScene()->backgroundBrush().color();
    saveState( selection, QString ("setBackgroundImage (%1)").arg(oldcol.name()),
    selection,
    QString ("setBackgroundImage (%1)").arg(col.name()),
    QString("Set background color of map to %1").arg(col.name()));
    */
    mapDesignInt->unsetBackgroundImage();
    vymView->updateColors();
}

bool VymModel::hasBackgroundImage()
{
    return mapDesignInt->hasBackgroundImage();
}

QString VymModel::backgroundImageName()
{
    return mapDesignInt->backgroundImageName();
}

void VymModel::setDefXLinkPen(const QPen &p)
{
    mapDesignInt->setDefXLinkPen(p);
}

void VymModel::setDefXLinkStyleBegin(const QString &s)
{
    mapDesignInt->setDefXLinkStyleBegin(s);
}

void VymModel::setDefXLinkStyleEnd(const QString &s)
{
    mapDesignInt->setDefXLinkStyleEnd(s);
}

void VymModel::setPos(const QPointF &pos_new, TreeItem *selti)
{
    QList<TreeItem *> selItems;
    if (selti)
        selItems.append(selti);
    else
        selItems = getSelectedItems();

    saveStateBeginScript("Move items (non-interactive)");
    foreach (TreeItem *ti, selItems) {
        if (ti->hasTypeBranch() || ti->hasTypeImage())
        {
            Container *c = ((MapItem*)ti)->getContainer();
            QString pos_new_str = toS(pos_new);

            QString uc, rc, itemVar;
            if (ti->hasTypeBranch())
                itemVar = setBranchVar((BranchItem*)ti) + "b.";
            else 
                itemVar = setImageVar((ImageItem*)ti) + "i.";
            uc = QString("%1.setPos%2;").arg(itemVar, toS(c->getOriginalPos(), 5));
            rc = QString("%1.setPos%2;").arg(itemVar, toS(c->pos(), 5)),
            saveState(uc, rc); 
            c->setPos(pos_new);
        }
    }
    saveStateEndScript();
    reposition();
}

void VymModel::sendSelection()
{
    if (netstate != Server)
        return;
    sendData(QString("select (\"%1\")").arg(getSelectString()));
}

void VymModel::newServer()
{
    port = 54321;
    sendCounter = 0;
    tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, port)) {
        QMessageBox::critical(nullptr, "vym server",
                              QString("Unable to start the server: %1.")
                                  .arg(tcpServer->errorString()));
        // FIXME needed? we are no widget any longer... close();
        return;
    }
    connect(tcpServer, SIGNAL(newConnection()), this, SLOT(newClient()));
    netstate = Server;
    qDebug() << "Server is running on port " << tcpServer->serverPort();
}

void VymModel::connectToServer()
{
    port = 54321;
    server = "salam.suse.de";
    server = "localhost";
    clientSocket = new QTcpSocket(this);
    clientSocket->abort();
    clientSocket->connectToHost(server, port);
    connect(clientSocket, SIGNAL(readyRead()), this, SLOT(readData()));
    connect(clientSocket, SIGNAL(error(QAbstractSocket::SocketError)), this,
            SLOT(displayNetworkError(QAbstractSocket::SocketError)));
    netstate = Client;
    qDebug() << "connected to " << qPrintable(server) << " port " << port;
}

void VymModel::newClient()
{
    QTcpSocket *newClient = tcpServer->nextPendingConnection();
    connect(newClient, SIGNAL(disconnected()), newClient, SLOT(deleteLater()));

    qDebug() << "ME::newClient  at "
             << qPrintable(newClient->peerAddress().toString());

    clientList.append(newClient);
}

void VymModel::sendData(const QString &s)
{
    if (clientList.size() == 0)
        return;

    // Create bytearray to send
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_0);

    // Reserve some space for blocksize
    out << (quint16)0;

    // Write sendCounter
    out << sendCounter++;

    // Write data
    out << s;

    // Go back and write blocksize so far
    out.device()->seek(0);
    quint16 bs = (quint16)(block.size() - 2 * sizeof(quint16));
    out << bs;

    if (debug)
        qDebug() << "ME::sendData  bs=" << bs << "  counter=" << sendCounter
                 << "  s=" << qPrintable(s);

    for (int i = 0; i < clientList.size(); ++i) {
        // qDebug() << "Sending \""<<qPrintable (s)<<"\" to "<<qPrintable
        // (clientList.at(i)->peerAddress().toString());
        clientList.at(i)->write(block);
    }
}

void VymModel::readData()   // FIXME-5 not used currently
{
    while (clientSocket->bytesAvailable() >= (int)sizeof(quint16)) {
        if (debug)
            qDebug() << "readData  bytesAvail="
                     << clientSocket->bytesAvailable();
        quint16 recCounter;
        quint16 blockSize;

        QDataStream in(clientSocket);
        in.setVersion(QDataStream::Qt_4_0);

        in >> blockSize;
        in >> recCounter;

        QString t;
        in >> t;
        if (debug)
            qDebug() << "VymModel::readData  command=" << qPrintable(t);
        // bool noErr;
        // QString errMsg;
        // parseAtom (t,noErr,errMsg);
    }
    return;
}

void VymModel::displayNetworkError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        QMessageBox::information(nullptr, vymName + " Network client",
                                 "The host was not found. Please check the "
                                 "host name and port settings.");
        break;
    case QAbstractSocket::ConnectionRefusedError:
        QMessageBox::information(nullptr, vymName + " Network client",
                                 "The connection was refused by the peer. "
                                 "Make sure the fortune server is running, "
                                 "and check that the host name and port "
                                 "settings are correct.");
        break;
    default:
        QMessageBox::information(nullptr, vymName + " Network client",
                                 QString("The following error occurred: %1.")
                                     .arg(clientSocket->errorString()));
    }
}

void VymModel::downloadImage(const QUrl &url, BranchItem *bi)
{
    if (!bi)
        bi = getSelectedBranch();
    if (!bi) {
        qWarning("VM::download bi==nullptr");
        return;
    }

    // FIXME-3 download img to tmpfile and delete after running script in
    // mainWindow
    QString script;
    script += QString("m = vym.currentMap();m.selectID(\"%1\");")
                  .arg(bi->getUuid().toString());
    script += QString("m.loadImage(\"$TMPFILE\");");

    DownloadAgent *agent = new DownloadAgent(url);
    agent->setFinishedAction(this, script);
    connect(agent, SIGNAL(downloadFinished()), mainWindow,
            SLOT(downloadFinished()));
    QTimer::singleShot(0, agent, SLOT(execute()));
}

void VymModel::setSelectionPenColor(QColor col) // FIXME-2 command missing?
{
    if (!col.isValid())
        return;

    QPen selPen = mapDesignInt->selectionPen();
    saveState(QString("map.setSelectionPenColor (\"%1\");")
                  .arg(selPen.color().name()),
              QString("map.setSelectionPenColor (\"%1\");").arg(col.name()),
              QString("Set pen color of selection box to %1").arg(col.name()));

    selPen.setColor(col);
    mapDesignInt->setSelectionPen(selPen);
    vymView->updateColors();
}

QColor VymModel::getSelectionPenColor() {
    return mapDesignInt->selectionPen().color();
}

void VymModel::setSelectionPenWidth(qreal w) // FIXME-2 command missing?
{
    QPen selPen = mapDesignInt->selectionPen();
    
    saveState(QString("map.setSelectionPenWidth (\"%1\");")
                  .arg(mapDesignInt->selectionPen().width()),
              QString("map.setSelectionPenWidth (\"%1\");").arg(w),
              QString("Set pen width of selection box to %1").arg(w));

    selPen.setWidth(w);
    mapDesignInt->setSelectionPen(selPen);
    vymView->updateColors();
}

qreal VymModel::getSelectionPenWidth() {
    return mapDesignInt->selectionPen().width();
}

void VymModel::setSelectionBrushColor(QColor col)   // FIXME-2 command missing?
{
    if (!col.isValid())
        return;

    QBrush selBrush = mapDesignInt->selectionBrush();
    saveState(QString("map.setSelectionBrushColor (\"%1\");")
                  .arg(selBrush.color().name()),
              QString("map.setSelectionBrushColor (\"%1\");").arg(col.name()),
              QString("Set Brush color of selection box to %1").arg(col.name()));

    selBrush.setColor(col);
    mapDesignInt->setSelectionBrush(selBrush);
    vymView->updateColors();
}

QColor VymModel::getSelectionBrushColor() {
    return mapDesignInt->selectionBrush().color();
}

void VymModel::newBranchIterator(
        const QString &itname,
        BranchItem *bi,
        bool deepLevelsFirst)
{
    Q_UNUSED(deepLevelsFirst);

    // Remove existing iterators first
    branchIterators.remove(itname);
    branchIteratorsCurrentIndex.remove(itname);
    branchIteratorsCurrentIndex[itname] = -1;

    BranchItem *cur = nullptr;
    BranchItem *prev = nullptr;
    nextBranch(cur, prev, true, bi);
    while (cur) {
        branchIterators[itname].append(cur->getUuid());
        //qDebug() << "VM::newBranchIterator Adding " << headingText(cur) << " to " << itname;
        nextBranch(cur, prev, true, bi);
    }
}

BranchItem* VymModel::nextBranchIterator(const QString &itname)
{
    //qDebug() << "VM::nextBranchIterator itname=" << itname << " index=" << branchIteratorsCurrentIndex;
    if (branchIterators.keys().indexOf(itname) < 0) {
        qWarning()
            << QString("VM::nextIterator couldn't find %1 in hash of iterators")
                   .arg(itname);
        return nullptr;
    }

    branchIteratorsCurrentIndex[itname]++;

    if (branchIteratorsCurrentIndex[itname] < 0 || branchIteratorsCurrentIndex[itname] > branchIterators[itname].size() - 1)
        return nullptr;

    BranchItem *bi = (BranchItem *)(findUuid(branchIterators[itname].at(branchIteratorsCurrentIndex[itname])));
    if (!bi) {
        qWarning() << "VM::nextIterator couldn't find branch with Uuid in list.";
        return nullptr;
    }

    return bi;
}

void VymModel::setHideTmpMode(TreeItem::HideTmpMode mode)
{
    if (hideMode == mode)
        return;

    hideMode = mode;
    for (int i = 0; i < rootItem->branchCount(); i++)
        rootItem->getBranchNum(i)->setHideMode(mode);
    reposition();
    if (mode == TreeItem::HideExport)
        unselectAll();
    else
        reselect();

    reposition();

    qApp->processEvents();
}

void VymModel::toggleHideTmpMode() {
    if (hideMode == TreeItem::HideNone)
        setHideTmpMode(TreeItem::HideExport);
    else
        setHideTmpMode(TreeItem::HideNone);
}

//////////////////////////////////////////////
// Selection related
//////////////////////////////////////////////

void VymModel::updateSelection(QItemSelection newsel, QItemSelection dsel)
{
    // Set selection status in objects
    // Temporary unscroll or rescroll as required

    //qDebug() << "VM::updateSel  newsel=" << newsel << " dsel=" << dsel;
    QModelIndex ix;
    MapItem *mi;
    BranchItem *bi;
    bool do_reposition = false;
    // Unselect objects (if not part of selection)
    foreach (ix, dsel.indexes()) {
        mi = static_cast<MapItem *>(ix.internalPointer());

        if (mi->hasTypeBranch() || mi->getType() == TreeItem::Image || mi->getType() == TreeItem::XLinkItemType) {
            if (mi->hasTypeBranch()) {
                ((BranchItem*)mi)->getBranchContainer()->unselect();
                do_reposition =
                    do_reposition || ((BranchItem *)mi)->resetTmpUnscroll();
            }
            if (mi->hasTypeImage())
                ((ImageItem*)mi)->getImageContainer()->unselect();
            if (mi->hasTypeXLink()) {
                ((XLinkItem*)mi)->getXLinkObj()->unselect();
                XLink *li = ((XLinkItem *)mi)->getXLink();

                do_reposition =
                    do_reposition || li->getBeginBranch()->resetTmpUnscroll();
                do_reposition =
                    do_reposition || li->getEndBranch()->resetTmpUnscroll();
            }
        }
    }

    foreach (ix, newsel.indexes()) {
        mi = static_cast<MapItem *>(ix.internalPointer());
        if (mi->hasTypeBranch()) {
            bi = (BranchItem *)mi;
            bi->getBranchContainer()->select();
            if (bi->hasScrolledParent()) {
                bi->tmpUnscroll();
                do_reposition = true;
            }
        }
        if (mi->hasTypeImage())
            ((ImageItem*)mi)->getImageContainer()->select();

        if (mi->getType() == TreeItem::XLinkItemType) {
            XLinkItem *xli = (XLinkItem*)mi;
            xli->setSelectionType();
            xli->getXLinkObj()->select(
                mapDesign()->selectionPen(),
                mapDesign()->selectionBrush());

            // begin/end branches need to be tmp unscrolled
            XLink *xl = ((XLinkItem *)mi)->getXLink();
            bi = xl->getBeginBranch();
            if (bi->hasScrolledParent()) {
                bi->tmpUnscroll();
                do_reposition = true;
            }
            bi = xl->getEndBranch();
            if (bi->hasScrolledParent()) {
                bi->tmpUnscroll();
                do_reposition = true;
            }
        }
        /* FIXME-2 ME::updateSelection - hide links of unselected objects
         * also for unselect below
        lmo = mi->getLMO(); // FIXME-X xlink does return nullptr
        if (lmo)
            mi->getLMO()->updateVisibility();
        */
    }

    // Show count of multiple selected items
    int selCount = selModel->selection().indexes().count();
    if (selCount > 1)
        mainWindow->statusMessage(
            tr("%1 items selected","Status message when selecting multiple items").arg(selCount));
    else
        mainWindow->statusMessage("");

    if (do_reposition)
        reposition();
}

void VymModel::setSelectionModel(QItemSelectionModel *sm) { selModel = sm; }

QItemSelectionModel *VymModel::getSelectionModel() { return selModel; }

void VymModel::setSelectionBlocked(bool b) { selectionBlocked = b; }

bool VymModel::isSelectionBlocked() { return selectionBlocked; }

bool VymModel::select(const QString &s)
{
    if (s.isEmpty())
        return false;

    QStringList list = s.split(";");

    unselectAll();
    foreach (QString t, list) {
        TreeItem *ti = findBySelectString(t);
        if (ti)
            selectToggle(ti);
        else
            return false;
    }
    return true;
}

bool VymModel::selectUids(QStringList  uids)
{
    if (uids.isEmpty())
        return false;

    unselectAll();
    foreach (auto uid, uids) {
        TreeItem *ti = findUuid(QUuid(uid));
        if (!ti)
            return false;
        selectToggle(ti);
    }
    return true;
}

bool VymModel::selectToggle(TreeItem *ti)
{
    if (ti) {
        selModel->select(index(ti), QItemSelectionModel::Toggle);
        // appendSelectionToHistory();	// FIXME-3 selection history not implemented yet
        // for multiselections
        lastToggledUuid = ti->getUuid();
        return true;
    }
    return false;
}

bool VymModel::selectToggle(const uint &id)
{
    TreeItem *ti = findID(id);
    return selectToggle(ti);
}

bool VymModel::selectToggle(const QUuid &uid)
{
    TreeItem *ti = findUuid(uid);
    return selectToggle(ti);
}

bool VymModel::selectToggle(const QString &selectString)
{
    TreeItem *ti = findBySelectString(selectString);
    return selectToggle(ti);
}

bool VymModel::select(TreeItem *ti)
{
    if (ti)
        return select(index(ti));
    else
        return false;
}

bool VymModel::select(const QUuid &uuid)
{
    TreeItem *ti = findUuid(uuid);
    return select(ti);
}

bool VymModel::select(const QModelIndex &index)
{
    if (index.isValid()) {
        TreeItem *ti = getItem(index);
        if (ti->hasTypeBranch()) {
            if (((BranchItem *)ti)->tmpUnscroll())
                reposition();
        }
        selModel->select(index, QItemSelectionModel::ClearAndSelect);
        appendSelectionToHistory();
        return true;
    }
    return false;
}

void VymModel::select(QList <BranchItem*> selbis)
{
    unselectAll();
    foreach (BranchItem* selbi, selbis)
        selectToggle(selbi);
}

void VymModel::select(QList <TreeItem*> tis)
{
    unselectAll();
    foreach (TreeItem* ti, tis)
        selectToggle(ti);
}

void VymModel::unselectAll() {
    lastSelectString = getSelectString();
    selModel->clearSelection();
}

bool VymModel::reselect()
{
    bool b = select(lastSelectString);
    return b;
}

bool VymModel::canSelectPrevious()
{
    if (currentSelection > 0)
        return true;
    else
        return false;
}

bool VymModel::selectPrevious()
{
    keepSelectionHistory = true;
    bool result = false;
    while (currentSelection > 0) {
        currentSelection--;
        TreeItem *ti = findID(selectionHistory.at(currentSelection));
        if (ti) {
            result = select(ti);
            break;
        }
        else
            selectionHistory.removeAt(currentSelection);
    }
    keepSelectionHistory = false;
    return result;
}

bool VymModel::canSelectNext()
{
    if (currentSelection < selectionHistory.count() - 1)
        return true;
    else
        return false;
}

bool VymModel::selectNext()
{
    keepSelectionHistory = true;
    bool result = false;
    while (currentSelection < selectionHistory.count() - 1) {
        currentSelection++;
        TreeItem *ti = findID(selectionHistory.at(currentSelection));
        if (ti) {
            result = select(ti);
            break;
        }
        else
            selectionHistory.removeAt(currentSelection);
    }
    keepSelectionHistory = false;
    return result;
}

void VymModel::resetSelectionHistory()
{
    selectionHistory.clear();
    currentSelection = -1;
    keepSelectionHistory = false;
    appendSelectionToHistory();
}

void VymModel::appendSelectionToHistory() // FIXME-3 history unable to cope with multiple
                                          // selections
{
    uint id = 0;
    TreeItem *ti = getSelectedItem();
    if (ti && !keepSelectionHistory) {
        if (ti->hasTypeBranch())
            ((BranchItem *)ti)->setLastSelectedBranch();
        id = ti->getID();
        selectionHistory.append(id);
        currentSelection = selectionHistory.count() - 1;
        updateActions();
    }
}

void VymModel::emitShowSelection(bool scaled, bool rotated)
{
    if (!repositionBlocked)
        emit showSelection(scaled, rotated);
}

TreeItem* VymModel::lastToggledItem()
{
    return findUuid(lastToggledUuid);
}

void VymModel::emitNoteChanged(TreeItem *ti)
{
    QModelIndex ix = index(ti);
    emit noteChanged(ix);
    mainWindow->updateNoteEditor(ti);
}

void VymModel::emitDataChanged(TreeItem *ti)
{
    //qDebug() << "VM::emitDataChanged ti=" << ti;
    if (ti) {
        QModelIndex ix = index(ti);
        emit dataChanged(ix, ix);

        // Update taskmodel and recalc priorities there
        if (ti->hasTypeBranch() && ((BranchItem *)ti)->getTask()) {
            taskModel->emitDataChanged(((BranchItem *)ti)->getTask());
            taskModel->recalcPriorities();
        }
    }
}

void VymModel::emitUpdateQueries()
{
    // Used to tell MainWindow to update query results
    if (repositionBlocked) return;

    emit updateQueries(this);
}
void VymModel::emitUpdateLayout()
{
    if (settings.value("/mainwindow/autoLayout/use", "true") == "true")
        emit updateLayout();
}

bool VymModel::selectFirstBranch(BranchItem *bi)
{
    BranchItem* selbi = getSelectedBranch(bi);
    if (selbi) {
        TreeItem *par = selbi->parent();
        if (par) {
            TreeItem *ti2 = par->getFirstBranch();
            if (ti2)
                return select(ti2);
        }
    }
    return false;
}

bool VymModel::selectFirstChildBranch(BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        BranchItem *bi2 = selbi->getFirstBranch();
        if (bi2)
            return select(bi2);
    }
    return false;
}

bool VymModel::selectLastBranch(BranchItem *bi)
{
    BranchItem* selbi = getSelectedBranch(bi);
    if (selbi) {
        TreeItem *par = selbi->parent();
        if (par) {
            BranchItem *bi2 = par->getLastBranch();
            if (bi2)
                return select(bi2);
        }
    }
    return false;
}

bool VymModel::selectLastChildBranch(BranchItem *bi)
{
    BranchItem *selbi = getSelectedBranch(bi);
    if (selbi) {
        BranchItem *bi2 = selbi->getLastBranch();
        if (bi2)
            return select(bi2);
    }
    return false;
}

bool VymModel::selectLastSelectedBranch()
{
    BranchItem *bi = getSelectedBranch();
    if (bi) {
        bi = bi->getLastSelectedBranch();
        if (bi)
            return select(bi);
    }
    return false;
}

bool VymModel::selectLatestAdded()
{
    TreeItem *ti = findUuid(latestAddedItemUuid);
        return select(ti);
}

bool VymModel::selectParent(TreeItem *ti)
{
    TreeItem *selti = getSelectedItem(ti);
    TreeItem *par;
    if (selti) {
        par = selti->parent();
        if (par)
            return select(par);
    }
    return false;
}

TreeItem::Type VymModel::selectionType()
{
    TreeItem *ti = getSelectedItem();
    if (ti)
        return ti->getType();
    else
        return TreeItem::Undefined;
}

BranchItem *VymModel::getSelectedBranch(BranchItem *bi)
{
    if (bi) return bi;

    // Return selected branch,
    // if several are selected, return last selected
    QList<BranchItem *> bis = getSelectedBranches();
    if (bis.count() == 0) return nullptr;

    return bis.last();
}

QList<BranchItem *> VymModel::getSelectedBranches(TreeItem *ti)
{
    // Return list of selected branches.
    // If ti != nullptr and is branch, return only this one
    QList<BranchItem *> selbis;

    if (ti && ti->hasTypeBranch()) {
        selbis << (BranchItem*)ti;
        return selbis;
    }

    foreach (TreeItem *ti, getSelectedItems()) {
        if (ti->hasTypeBranch())
            selbis.append((BranchItem *)ti);
    }
    return selbis;
}

ImageItem *VymModel::getSelectedImage()
{
    // Return selected image,
    // if several are selected, return last selected
    QList<ImageItem *> iis = getSelectedImages();
    if (iis.count() == 0) return nullptr;

    return iis.last();
}

QList<ImageItem *> VymModel::getSelectedImages(TreeItem *ti)
{
    // Return list of selected images.
    // If ii != nullptr, return only this one
    QList<ImageItem *> iis;

    if (ti && ti->hasTypeImage()) {
        iis << (ImageItem*)ti;
        return iis;
    }

    foreach (TreeItem *ti, getSelectedItems()) {
        if (ti->hasTypeImage())
            iis.append((ImageItem *)ti);
    }
    return iis;
}

Task *VymModel::getSelectedTask()
{
    BranchItem *selbi = getSelectedBranch();
    if (selbi)
        return selbi->getTask();
    else
        return nullptr;
}

XLink *VymModel::getSelectedXLink(XLink *xl)
{
    if (xl)
        return xl;

    XLinkItem *xli = getSelectedXLinkItem();
    if (xli)
        return xli->getXLink();
    return nullptr;
}

XLinkItem *VymModel::getSelectedXLinkItem()
{
    TreeItem *ti = getSelectedItem();
    if (ti && ti->getType() == TreeItem::XLinkItemType)
        return (XLinkItem *)ti;
    else
        return nullptr;
}

AttributeItem *VymModel::getSelectedAttribute()
{
    TreeItem *ti = getSelectedItem();
    if (ti && ti->getType() == TreeItem::Attribute)
        return (AttributeItem *)ti;
    else
        return nullptr;
}

TreeItem *VymModel::getSelectedItem(TreeItem *ti)
{
    if (ti) return ti;

    if (!selModel)
        return nullptr;
    QModelIndexList list = selModel->selectedIndexes();
    if (list.count() == 1)
        return getItem(list.first());
    else
        return nullptr;
}

QList<TreeItem *> VymModel::getSelectedItems(TreeItem *ti)
{
    QList<TreeItem *> seltis;
    if (ti) {
        seltis << ti;
        return seltis;
    }

    if (!selModel)
        return seltis;

    QModelIndexList list = selModel->selectedIndexes();
    foreach (QModelIndex ix, list)
        seltis.append(getItem(ix));
    return seltis;
}

QList<TreeItem *> VymModel::getSelectedItemsReduced()
{
    // Remove items, whose have parents already in list

    QList<TreeItem *> list = getSelectedItems();

    if (list.isEmpty()) return list;

    // Bubble sort items by depth first
    for (int n = list.size(); n > 1; n--)
        for (int i = 0; i < n - 1; i++)
            if (list.at(i)->depth() > list.at(i + 1)->depth() )
                list.swapItemsAt(i, i + 1);

    // Remove items, which have parents which have smaller depth
    // (closer to center)
    int i = list.size() - 1;
    while (i > 0) {
        for (int j = 0; j < i; j++)
            if (list.at(i)->isChildOf(list.at(j))) {
                list.removeAt(i);
                break;
            }
        i--;
    }

    return list;
}

QModelIndex VymModel::getSelectedIndex()
{
    QModelIndexList list = selModel->selectedIndexes();
    if (list.count() == 1)
        return list.first();
    else
        return QModelIndex();
}

QList<ulong> VymModel::getSelectedIDs()
{
    QList<ulong> uids;
    foreach (TreeItem *ti, getSelectedItems())
        uids.append(ti->getID());
    return uids;
}

QStringList VymModel::getSelectedUUIDs()
{
    QStringList uids;
    foreach (TreeItem *ti, getSelectedItems())
        uids.append(ti->getUuid().toString());
    return uids;
}

bool VymModel::isSelected(TreeItem *ti)
{
    return getSelectedItems().contains(ti);
}

QString VymModel::getSelectString()
{
    QStringList list;
    QList <TreeItem*> seltis = getSelectedItems();
    foreach (TreeItem* selti, seltis)
        list << getSelectString(selti);

    return list.join(";");
}

QString VymModel::getSelectString(TreeItem *ti)
{
    QString s;
    if (!ti || ti->depth() < 0)
        return s;
    switch (ti->getType()) {
    case TreeItem::MapCenter:
        s = "mc:";
        break;
    case TreeItem::Branch:
        s = "bo:";
        break;
    case TreeItem::Image:
        s = "fi:";
        break;
    case TreeItem::Attribute:
        s = "ai:";
        break;
    case TreeItem::XLinkItemType:
        s = "xl:";
        break;
    default:
        s = "unknown type in VymModel::getSelectString()";
        break;
    }
    s = s + QString("%1").arg(ti->num());
    if (ti->depth() > 0)
        // call myself recursively
        s = getSelectString(ti->parent()) + "," + s;
    return s;
}

QString VymModel::getSelectString(BranchItem *bi)
{
    return getSelectString((TreeItem *)bi);
}

QString VymModel::getSelectString(const uint &i)
{
    return getSelectString(findID(i));
}

SlideModel *VymModel::getSlideModel() { return slideModel; }

int VymModel::slideCount() { return slideModel->count(); }

SlideItem *VymModel::addSlide()     // FIXME-3 missing saveState
{
    SlideItem *si = slideModel->getSelectedItem();
    if (si)
        si = slideModel->addSlide(nullptr, si->childNumber() + 1);
    else
        si = slideModel->addSlide();

    TreeItem *seli = getSelectedItem();

    if (si) {
        if (seli) {
            QString inScript;
            if (!loadStringFromDisk(vymBaseDir.path() +
                                        "/macros/slideeditor-snapshot.vys",
                                    inScript)) {
                qWarning() << "VymModel::addSlide couldn't load template for "
                              "taking snapshot";
                return nullptr;
            }

            inScript.replace(
                "CURRENT_ZOOM",
                QString().setNum(getMapEditor()->zoomFactorTarget()));
            inScript.replace("CURRENT_ANGLE",
                             QString().setNum(getMapEditor()->rotationTarget()));
            inScript.replace("CURRENT_ID",
                             "\"" + seli->getUuid().toString() + "\"");

            si->setInScript(inScript);
            slideModel->setData(slideModel->index(si), seli->headingPlain());
        }

        /*
        QString s = "<vymmap>" + si->saveToDir() + "</vymmap>";
        int pos = si->childNumber();
        saveStateold(File::PartOfMap, getSelectString(),    // FIXME addAddSlide
                  QString("removeSlide (%1)").arg(pos), getSelectString(),
                  QString("addMapInsert (\"PATH\",%1)").arg(pos), "Add slide", nullptr,
                  s);
          */
    }
    return si;
}

void VymModel::deleteSlide(SlideItem *si)  // FIXME-3 missing saveState
{
    if (si) {
        QString s = "<vymmap>" + si->saveToDir() + "</vymmap>";
        /*
        int pos = si->childNumber();
        saveStateold(File::PartOfMap, getSelectString(),    // FIXME deleteAddSlide
                  QString("addMapInsert (\"PATH\",%1)").arg(pos),
                  getSelectString(), QString("removeSlide (%1)").arg(pos),
                  "Remove slide", nullptr, s);
                  */
        slideModel->deleteSlide(si);
    }
}

void VymModel::deleteSlide(int n) { deleteSlide(slideModel->getSlide(n)); }

void VymModel::relinkSlide(SlideItem *si, int pos)
{
    if (si && pos >= 0)
        slideModel->relinkSlide(si, si->parent(), pos);
}

bool VymModel::moveSlideDown(int n)
{
    SlideItem *si = nullptr;
    if (n < 0) // default if called without parameters
    {
        si = slideModel->getSelectedItem();
        if (si)
            n = si->childNumber();
        else
            return false;
    }
    else
        si = slideModel->getSlide(n);
    if (si && n >= 0 && n < slideModel->count() - 1) {
        blockSlideSelection = true;
        slideModel->relinkSlide(si, si->parent(), n + 1);
        blockSlideSelection = false;
        QString uc = QString("map.moveSlideUp (%1);").arg(n + 1);
        QString rc = QString("map.moveSlideDown (%1);").arg(n);
        saveState(uc, rc,
                  QString("Move slide %1 down").arg(n));
        return true;
    }
    else
        return false;
}

bool VymModel::moveSlideUp(int n)
{
    SlideItem *si = nullptr;
    if (n < 0) // default if called without parameters
    {
        si = slideModel->getSelectedItem();
        if (si)
            n = si->childNumber();
        else
            return false;
    }
    else
        si = slideModel->getSlide(n);
    if (si && n > 0 && n < slideModel->count()) {
        blockSlideSelection = true;
        slideModel->relinkSlide(si, si->parent(), n - 1);
        blockSlideSelection = false;
        QString uc = QString("map.moveSlideDown (%1);").arg(n - 1);
        QString rc = QString("map.moveSlideUp  (%1);").arg(n);
        saveState(uc, rc,
                  QString("Move slide %1 up").arg(n));
        return true;
    }
    else
        return false;
}

void VymModel::updateSlideSelection(QItemSelection newsel, QItemSelection)
{
    if (blockSlideSelection)
        return;
    QModelIndex ix;
    foreach (ix, newsel.indexes()) {
        SlideItem *si = static_cast<SlideItem *>(ix.internalPointer());
        QString inScript = si->getInScript();

        // show inScript in ScriptEditor
        scriptEditor->setSlideScript(modelIdInt, si->getID(), inScript);

        // Execute inScript
        execute(inScript);
    }
}

void VymModel::logDebug(const QString &comment, const QString &caller)
{
    QString place = QString("\"%1\"").arg(fileName);
    if (!caller.isEmpty()) place += "  Called by: " + caller + "()";

    QString log = QString("\n// %1 [Debug] %2\n// MapID: %3 Map: %4").arg(
            QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
            comment,
            QString::number(modelIdInt),
            place);

    std::cout << log.toStdString() << std::endl << std::flush;

    if (!useActionLog) return;

    appendStringToFile(actionLogPath, log);
}

void VymModel::logInfo(const QString &comment, const QString &caller)
{
    if (!useActionLog) return;

    QString log = QString("\n// %1 [Info VymModel::%2 \"%3\"] %4").arg(
            QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
            caller,
            fileName,
            comment
//            QString::number(modelIdInt)
    );

    std::cout << log.toStdString() << std::endl << std::flush;

    appendStringToFile(actionLogPath, log);
}

void VymModel::logCommand(const QString &command, const QString &comment, const QString &caller)
{
    QString place = QString("\"%1\"").arg(fileName);
    if (!caller.isEmpty()) place += "  Called by: " + caller + "()";

    QString log = QString("\n// %1 [Command] %2\n// MapID: %3 Map: %4").arg(
            QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
            comment,
            QString::number(modelIdInt),
            place);

    std::cout << log.toStdString() << std::endl << std::flush;

    if (!useActionLog) return;

    appendStringToFile(actionLogPath, log + "\n" + command + "\n");
}
