#include "vym-wrapper.h"

#include <QJSValue> 

#include "branchitem.h"
#include "confluence-agent.h"
#include "file.h"
#include "mainwindow.h"
#include "mapeditor.h"
#include "misc.h"
#include "xlink.h"


extern Main *mainWindow;
extern QDir vymBaseDir;
extern QString vymVersion;
extern QString vymHome;

extern bool usingDarkTheme;

#include "vymmodelwrapper.h"

VymWrapper::VymWrapper()
{
    qDebug() << "Constr. VymWrapper";
}

VymWrapper::~VymWrapper()
{
    qDebug() << "Destr. VymWrapper";
}

void VymWrapper::clearConsole() { mainWindow->clearScriptOutput(); }

bool VymWrapper::closeMapWithID(uint n)
{
    bool r = mainWindow->closeModelWithId(n);
    if (!r) {
        mainWindow->abortScript(
                QJSValue::ReferenceError, 
                QString("Map '%1' not available.").arg(n));
        return false;
    }
    mainWindow->setScriptResult(r);
    return r;
}

QString VymWrapper::currentColor()
{
    QString r = mainWindow->getCurrentColor().name();
    mainWindow->setScriptResult(r);
    return r;
}

QObject *VymWrapper::currentMap()
{
    QObject * mw = mainWindow->getCurrentModelWrapper();
    if (!mw) {
        mainWindow->abortScript(
                QJSValue::ReferenceError,
                "No current model available");
    }
    return mw;
}

void VymWrapper::editHeading()
{
    MapEditor *me = mainWindow->currentMapEditor();
    if (me) me->editHeading();
}

bool VymWrapper::directoryIsEmpty(const QString &directoryName)
{
    QDir d(directoryName);
    return d.isEmpty();
}

bool VymWrapper::directoryExists(const QString &directoryName)
{
    QDir d(directoryName);
    return d.exists();
}

bool VymWrapper::fileCopy(const QString &srcPath, QString dstPath)
{
    QFile file(srcPath);
    if (dstPath.endsWith("/"))
        dstPath = dstPath + basename(srcPath);

    QFile dst(dstPath);
    if (dst.exists())
        // Overwrite dst!
        dst.remove();

    bool r; 
    if (!file.exists()) {
        qDebug() << "VymWrapper::fileCopy()   srcPath does not exist:" << srcPath;
        mainWindow->abortScript(
                QJSValue::ReferenceError, 
                QString("File '%1' does not exist.").arg(srcPath));
        r = false;
    } else
        r = file.copy(dstPath);

    if (!r) {
        QString msg = QString("VymWrapper::fileCopy:  Failed to copy %1 to %2").arg(srcPath, dstPath);
        mainWindow->abortScript(msg);
    }

    mainWindow->setScriptResult(r);
    return r;
}

bool VymWrapper::fileExists(const QString &fileName)
{
    bool r = QFile::exists(fileName);
    mainWindow->setScriptResult(r);
    return r;
}

bool VymWrapper::fileRemove(const QString &fileName)
{
    QFile file(fileName);
    bool r = file.remove();
    mainWindow->setScriptResult(r);
    return r;
}

void VymWrapper::gotoMap(uint n)
{
    if (!mainWindow->gotoModelWithId(n)) {
        mainWindow->abortScript(
                QJSValue::ReferenceError, 
                QString("Map '%1' not available.").arg(n));
        return;
    }
}

bool VymWrapper::isConfluenceAgentAvailable()
{
    bool r = ConfluenceAgent::available();
    mainWindow->setScriptResult(r);
    return r;
}

QString VymWrapper::loadFile(
    const QString
        &filename) // FIXME-3 error handling missing (in vymmodel and here)
{
    QString r;
    loadStringFromDisk(filename, r);
    mainWindow->setScriptResult(r);
    return r;
}

bool VymWrapper::loadMap(QString filename)
{
    bool r;
    if (!filename.startsWith("/"))
        filename = vymHome + "/" + filename;

    if (File::Success == mainWindow->fileLoad(filename, File::NewMap, File::VymMap))
        r = true;
    else
        r = false;
    mainWindow->setScriptResult(r);
    return r;
}

int VymWrapper::mapCount()
{
    int r = mainWindow->modelCount();
    mainWindow->setScriptResult(r);
    return r;
}

bool VymWrapper::mkdir(const QString &directoryName)
{
    QDir d;
    return d.mkpath(directoryName);
}

void VymWrapper::print(const QString &s)
{
    mainWindow->scriptPrint(s);
}

bool VymWrapper::removeDirectory(const QString &directoryName)
{
    QDir d(directoryName);
    qWarning() << "VW::removeDir " << directoryName;
    return false;
    return d.removeRecursively();
}

bool VymWrapper::removeFile(const QString &fileName)
{
    return QFile::remove(fileName);
}

void VymWrapper::statusMessage(const QString &s)
{
    mainWindow->statusMessage(s);
}

void VymWrapper::selectQuickColor(int n)
{
    mainWindow->selectQuickColor(n);
}

uint VymWrapper::currentMapID()
{
    uint r = mainWindow->currentMapId();
    mainWindow->setScriptResult(r);
    return r;
}

void VymWrapper::toggleTreeEditor() { mainWindow->windowToggleTreeEditor(); }

void VymWrapper::saveFile(
    const QString &filename,
    const QString &s) // FIXME-3 error handling missing (in vymmodel and here)
{
    saveStringToDisk(filename, s);
}

bool VymWrapper::usesDarkTheme() {
    mainWindow->setScriptResult(usingDarkTheme);
    return usingDarkTheme;
}

QString VymWrapper::version() {
    qDebug() << "VymWrapper::version  v=" << vymVersion;
    QString r = vymVersion;
    mainWindow->setScriptResult(r);
    return r;
}

QString VymWrapper::vymBaseDir() {
    return ::vymBaseDir.path();
}

Selection::Selection() { modelWrapper = nullptr; }  // FIXME-2 needed?

void Selection::test()
{
    qDebug() << "Selection::testSelection called"; // TODO debug
    /*
    if (modelWrapper)
        modelWrapper->setHeadingPlainText("huhu!");
    */
}

void Selection::setModel(VymModelWrapper *mw)
{
    qDebug() << "Selection::setModel called: " << mw; // TODO debug
    modelWrapper = mw;
}
