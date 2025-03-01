#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QJSEngine>
#include <QMessageBox>
#include <QStyleFactory>

#include "branchpropeditor.h"
#include "command.h"
#include "debuginfo.h"
#include "findresultwidget.h"
#include "findwidget.h"
#include "flagrow-master.h"
#include "headingeditor.h"
#include "macros.h"
#include "mainwindow.h"
#include "noteeditor.h"
#include "options.h"
#include "scripteditor.h"
#include "scriptoutput.h"
#include "settings.h"
#include "shortcuts.h"
#include "taskeditor.h"
#include "taskmodel.h"
#include "version.h"
#include "warningdialog.h"
#include "zip-agent.h"

#if defined(VYM_DBUS)
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#endif

QString vymName;
QString vymVersion;
QString vymHome;
QString vymBuildDate;
QString vymCodeName;
QString vymCodeQuality;
QString vymInstanceName;
QString vymPlatform;
QString localeName;

QTextStream vout(stdout); // vymout - Testing for now. Flush after writing...

// Accessing JIRA and Confluence is done using agents
// Credentials may be stored in settings, but only on request
QString jiraPassword;   
QString confluencePassword;

TaskModel *taskModel;
HeadingEditor *headingEditor;
NoteEditor *noteEditor;
BranchPropertyEditor *branchPropertyEditor;

// initialized in mainwindow
Main *mainWindow;
ScriptEditor *scriptEditor;
ScriptOutput *scriptOutput;
FindWidget *findWidget;
FindResultWidget *findResultWidget;
TaskEditor *taskEditor;

FlagRowMaster *systemFlagsMaster;
FlagRowMaster *standardFlagsMaster;
FlagRowMaster *userFlagsMaster;

Macros macros;

ulong itemLastID = 0;  // Unique ID for all items in all models
ulong imageLastID = 0; // Unique ID for caching images, also flags not in tree

QDir tmpVymDir;          // All temp files go there, created in mainwindow
QDir cacheDir;            // tmp dir with cached svg files in tmpVymDir
QString clipboardDir;    // Clipboard used in all mapEditors
QString clipboardFile;   // Clipboard used in all mapEditors

QDir vymBaseDir;            // Containing all styles, scripts, images, ...

bool useActionLog;          // Write logfile. No GUI yet to enable, only for debugging
QString actionLogPath;      // Path to logfile

QDir vymTranslationsDir;    // Translation files (*.qm)
QTranslator vymTranslator;

QDir lastImageDir;
QDir lastMapDir;
QDir lastExportDir;
QDir lastScriptDir;

#if defined(Q_OS_WINDOWS)
QDir vymInstallDir;
#endif
QString iconPath;  // Pointing to icons used for toolbars
QString flagsPath; // Pointing to flags

bool debug;                // global debugging flag
bool testmode;             // Used to disable saving of autosave setting
bool restoreMode = false;  // During restore, existing lockfiles are ignored

QStringList ignoredLockedFiles;
QStringList lastSessionFiles;   //! Will be overwritten in setting after load, so read initially

Switchboard switchboard;

Settings settings("InSilmaril", QString(__VYM_NAME).toLower()); // Organization, Application name

bool zipToolAvailable = false;
bool unzipToolAvailable = false;
QString zipToolPath;	// Platform dependant zip tool
QString unzipToolPath;  // Platform dependant unzip tool

QList<Command *> vymCommands;
QList<Command *> modelCommands;
QList<Command *> branchCommands;
QList<Command *> imageCommands;
QList<Command *> xlinkCommands;

Options options;
ImageIO imageIO;

bool usingDarkTheme;    // Influences some color schemes
bool systemSeemsDark;   // Text brighter than background?
QString iconPrefix;     // "dark-" when usingDarkTheme  // FIXME-2 obsolete by iconTheme
QString iconTheme;      // "bright" or "dark" depending on usingDarkTheme
QColor vymBlue;

int warningCount = 0;
int criticalCount = 0;
int fatalCount = 0;

QString editorFocusStyle =
    QString(" border-color: #3daee9; border-style:outset; border-width:3px; "
            "color:black;");

void msgHandler(QtMsgType type, const QMessageLogContext &context,
                const QString &msg)
{
    QByteArray localMsg;
    /*
    if (msg.startsWith("\"") && msg.endsWith("\"")) {
        QString s = msg;
        localMsg = s.remove(s.length() - 1, 1).remove(0,1).toLocal8Bit();
    } else
    */
        localMsg = msg.toLocal8Bit();

    switch (type) {
        case QtDebugMsg:
            fprintf(stderr, "%s\n", localMsg.constData());
            break;
        case QtWarningMsg:
            fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(),
                    context.file, context.line, context.function);
            warningCount++;
            break;
        case QtCriticalMsg:
            fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(),
                    context.file, context.line, context.function);
            criticalCount++;
            break;
        case QtFatalMsg:
            fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(),
                    context.file, context.line, context.function);
            fatalCount++;
            break;
        default:
            fprintf(stderr, "Info: %s\n", localMsg.constData());
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Define some constants shared in various places
    vymName = __VYM_NAME;
    vymVersion = __VYM_VERSION;
    vymBuildDate = __VYM_BUILD_DATE;
    vymCodeName = __VYM_CODENAME;
    vymCodeQuality = __VYM_CODE_QUALITY;
    vymHome = __VYM_HOME;

    // Install our own handler for messages
    qInstallMessageHandler(msgHandler);

    // Reading and initializing options commandline options
    options.add("batch", Option::Switch, "b", "batch");
    options.add("commands", Option::Switch, "c", "commands");
    options.add("commandslatex", Option::Switch, "cl", "commandslatex");
    options.add("debug", Option::Switch, "d", "debug");
    options.add("help", Option::Switch, "h", "help");
    options.add("load", Option::String, "L", "load");
    options.add("local", Option::Switch, "l", "local");
    options.add("locale", Option::String, "locale", "locale");
    options.add("name", Option::String, "n", "name");
    options.add("quit", Option::Switch, "q", "quit");
    options.add("run", Option::String, "R", "run");
    options.add("recover", Option::Switch, "recover", "recover");
    options.add("restore", Option::Switch, "r", "restore");
    options.add("shortcuts", Option::Switch, "s", "shortcuts");
//    options.add("shortcutsLaTeX", Option::Switch, "sl", "shortcutsLaTeX") // FIXME-3 not really used?
    options.add("testmode", Option::Switch, "t", "testmode");
    options.add("version", Option::Switch, "v", "version");
    options.setHelpText(
        "VYM - View Your Mind\n"
        "--------------------\n\n"
        "Information about vym can be found in vym.pdf,\n"
        "which should be part of the vym package.\n"
        "It is also available at the project homepage:\n\n"
        "http://www.InSilmaril.de/vym\n\n"
        "Usage: vym [OPTION]... [FILE]... \n"
        "Open FILEs with vym\n\n"
        "-b           batch         batch mode: hide windows\n"
        "-c           commands      List all available commands\n"
        "-cl          commandslatex List commands in LaTeX format\n"
        "-d           debug         Show debugging output\n"
        "-h           help          Show this help text\n"
        "-L           load          Load script\n"
        "-l           local         Run with resources in current directory\n"
        "--locale     locale        Override system locale setting to select\n"
        "                           language\n"
        "-n  STRING   name          Set name of instance for DBus access\n"
        "-q           quit          Quit immediately after start for benchmarking\n"
        "-R  FILE     run           Run script\n"
        "-r           restore       Restore last session\n"
        "--recover    recover       Delete lockfiles during initial loading of\n"
        "                           files\n"
        "-s           shortcuts     Show Keyboard shortcuts on start\n"
        "-t           testmode      Test mode, e.g. no autosave and changing\n"
        "                           of its setting\n"
        "-v           version       Show vym version\n");

    if (options.parse()) {
        std::cout << std::endl << qPrintable(options.getHelpText()) << std::endl;
        return 1;
    }

    if (options.isActive("version")) {
        QString s = QString("VYM - View Your Mind (c) 2004-%1").arg(QDate::currentDate().year());
        s += " Uwe Drechsel\n";
        s += "   Version: " + vymVersion;
        if (!vymCodeName.isEmpty())
            s += QString(" - \"%1\"").arg(vymCodeName);
        s += "\n";
        s += "   Quality: " + vymCodeQuality + "\n";
        s += "Build date: " + vymBuildDate + "\n";
        std::cout << s.toStdString();

        return 0;
    }

    taskModel = new TaskModel();

    debug = options.isActive("debug");

    testmode = options.isActive("testmode");

    QString pidString = QString::number(QCoreApplication::applicationPid());

#if defined(VYM_DBUS)
    // Register for DBUS
    QDBusConnection dbusConnection = QDBusConnection::sessionBus();
    if (!dbusConnection.registerService("org.insilmaril.vym-" + pidString)) {
        fprintf(
            stderr, "%s\n",
            qPrintable(QDBusConnection::sessionBus().lastError().message()));
        exit(1);
    }
#endif

    if (options.isActive("name"))
        vymInstanceName = options.getArg("name");
    else
        vymInstanceName = pidString;

#ifdef QT_DEBUG
    qDebug() << "QT_DEBUG is set";
    debug = true;
#endif

    // Use /usr/share/vym or /usr/local/share/vym or . ?
    // First try options
    if (options.isActive("local")) {
        vymBaseDir.setPath(vymBaseDir.currentPath());
    } else
        // then look for environment variable
        if (getenv("VYMHOME") != 0) {
            vymBaseDir.setPath(getenv("VYMHOME"));
        } else {
            // ok, let's find vymBaseDir on my own

#if defined(Q_OS_MACX)
            // Executable is in vym.app/Contents/MacOS, so go up first:
            vymBaseDir.setPath(QCoreApplication::applicationDirPath());
            vymBaseDir.cdUp();
            vymBaseDir.cd("Resources");
#elif defined(Q_OS_WINDOWS)
            vymBaseDir.setPath(QCoreApplication::applicationDirPath());
#else
            vymBaseDir.setPath(VYMBASEDIR);
#endif
        }

    // Platform specific settings
    vymPlatform = QSysInfo::prettyProductName();

#if defined(Q_OS_WINDOWS)
    // Only Windows 10 has tar for zip and unzip.
    // Older windows versions not supported.
    zipToolPath = "tar";
    unzipToolPath = "tar";
#else
    zipToolPath = "/usr/bin/zip";       // FIXME-4 no longer installed per default in openSUSE 15.6
    unzipToolPath = "/usr/bin/unzip";
#endif
    iconPath = vymBaseDir.path() + "/icons/";
    flagsPath = vymBaseDir.path() + "/flags/";

    // When running locally, use local macros. Otherwise settings are used
    if (options.isActive("local"))
        macros.setPath(vymBaseDir.path() + "/macros/macros.vys");
    else
        macros.setPath(
            settings
            .value("/macros/path", vymBaseDir.path() + "/macros/macros.vys")
            .toString());

    // Some directories
    QDir useDir;
    if (options.isActive("local"))
        useDir = QDir().current();
    else
        if (getenv("VYMHOME") != 0)
            useDir = vymBaseDir;
        else
            useDir = QDir().home();

    lastImageDir = useDir;
    lastMapDir = useDir;
    lastExportDir = useDir;
    lastScriptDir = useDir;

    if (options.isActive("help")) {
        std::cout << qPrintable(options.getHelpText()) << std::endl;
        return 0;
    }

    // Initialize translations
    if (options.isActive("locale"))
        localeName = options.getArg("locale");

    // Use dark theme depending on system appearance and preferences
    int text_hsv_value = app.palette().color(QPalette::WindowText).value();
    int bg_hsv_value = app.palette().color(QPalette::Base).value();
    systemSeemsDark = (text_hsv_value > bg_hsv_value);
    QString settingsDarkTheme = settings.value("/system/darkTheme", "system").toString();
    usingDarkTheme = false;
    iconPrefix = "bright-";
    iconTheme = "bright";
    if (settingsDarkTheme != "never") {
        if (settingsDarkTheme == "always" || (settingsDarkTheme == "system" && systemSeemsDark)) {
            usingDarkTheme = true;
            iconPrefix = "dark-";
            iconTheme = "dark";
        }
    }

    if (usingDarkTheme && settingsDarkTheme == "always") {
        qApp->setStyle(QStyleFactory::create("fusion"));
        //qApp->setStyle(QStyleFactory::create("Windows"));
        //qApp->setStyle(QStyleFactory::create("windowsvista"));

        // On Windows, there is no dark palette predefined, let's do that on our own
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(53,53,53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(27, 30, 32));
        palette.setColor(QPalette::AlternateBase, QColor(53,53,53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        // Unused: QPalette::PlaceHolderText
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53,53,53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Highlight, QColor(142,45,197).lighter());
        palette.setColor(QPalette::HighlightedText, Qt::black);

        // FIXME-3 palette experiments on Windows
        //palette.setColor(QPalette::Light, Qt::green);
        //palette.setColor(QPalette::Midlight, Qt::red);
        qApp->setPalette(palette);
    }

    // Prepare and check translations
    vymTranslationsDir = QDir(vymBaseDir.path() + "/translations");
    vymTranslationsDir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    bool translationsMissing = false;
    if(!vymTranslationsDir.exists())
        translationsMissing = true;
    else if (vymTranslationsDir.isEmpty())
        translationsMissing = true;

    if (translationsMissing) {
        WarningDialog warn;
        warn.setMinimumWidth(800);
        warn.setMinimumHeight(350);
        warn.showCancelButton(false);
        warn.setShowAgainName("mainwindow/translations/qmFilesMissing");
        warn.setCaption("Translations not available");
        warn.setText(
                "vym has not been built correctly and only will be available in English: \n\n"
                "No translation files in\n" +
                vymTranslationsDir.path().toLatin1() + "\n\n" +
                "Please get vym from\n"
                " * https://sourceforge.net/projects/vym/  or \n"
                " * https://software.opensuse.org//download.html?project=home%3Ainsilmaril&package=vym");
        warn.exec();
    } else {
        bool ok;
        if (!localeName.isEmpty())
            // Use localeName to load specific language
            ok = vymTranslator.load(QString("vym.%1.qm").arg(localeName), vymTranslationsDir.path());
        else {
            ok = vymTranslator.load(QLocale(), "vym", ".", vymTranslationsDir.path(), ".qm");
            if (!ok)
                // No system locale found, go for English
                ok = vymTranslator.load(QString("vym.en.qm"), vymTranslationsDir.path());
        }

        if (!ok) {
            WarningDialog warn;
            warn.showCancelButton(false);
            warn.setText(
                QString("Couldn't load translation for locale \"%1\" from\n%2")
                    .arg(localeName)
                    .arg(vymTranslationsDir.path()));
            warn.setShowAgainName("mainwindow/translations/localeMissing");
            warn.exec();
        } else
            QCoreApplication::installTranslator(&vymTranslator);
    }

    // Initializing the master rows of flags
    systemFlagsMaster = new FlagRowMaster;
    systemFlagsMaster->setName("systemFlagsMaster");

    standardFlagsMaster = new FlagRowMaster;
    standardFlagsMaster->setName("standardFlagsMaster");
    standardFlagsMaster->setPrefix("standard/");

    userFlagsMaster = new FlagRowMaster;
    userFlagsMaster->setName("userFlagsMaster");
    userFlagsMaster->setPrefix("user/");

    // Initialize editors
    noteEditor = new NoteEditor("noteeditor");
    noteEditor->setWindowIcon(QPixmap(":/vym-editor.png"));
    headingEditor = new HeadingEditor("headingeditor");
    branchPropertyEditor = new BranchPropertyEditor();

    // Initially read filenames of last session, before settings are 
    // overwritten during loading of maps
    lastSessionFiles = settings.value("/mainwindow/sessionFileList", QStringList()).toStringList();


    // Logfiles (no GUI yet for settings)
    useActionLog = settings.value("/logfile/enabled", false).toBool();
    actionLogPath = settings.value("/logfile/path", QDir::homePath() + "/vym.log").toString();

    // Create MainWindow
    Main m;

    // Check for zip tools
    zipToolAvailable = ZipAgent::checkZipTool();
    unzipToolAvailable = ZipAgent::checkUnzipTool();

#if defined(Q_OS_WINDOWS)
    if (!zipToolAvailable || QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10) {
        QMessageBox::critical(
            0, QObject::tr("Critical Error"),
            QObject::tr("Couldn't find tool to zip/unzip data,"
                        "or your Windows version is older than Windows 10."));
    }
#else
    if (!zipToolAvailable)
        QMessageBox::critical(
            0, QObject::tr("Critical Error"),
            QObject::tr("Couldn't find tar tool to zip data. "));
    if (!unzipToolAvailable)
        QMessageBox::critical(
            0, QObject::tr("Critical Error"),
            QObject::tr("Couldn't find tar tool to unzip data. "));
#endif

    m.setWindowIcon(QPixmap(":/vym.png"));
    m.fileNew();

    if (debug)
        // Show debug info AFTER creating MainWindow
        std::cout << debugInfo().toStdString() << std::endl;

    if (options.isActive("commands")) { // FIXME-2 Merge with Main::helpScriptingCommands
        std::cout << "Available commands in vym:\n";
        std::cout << "=========================:\n";
        foreach (Command *c, vymCommands)
            std::cout << c->description().toStdString() << std::endl;

        std::cout << "Available commands in map:\n";
        std::cout << "=========================:\n";
        foreach (Command *c, modelCommands)
            std::cout << c->description().toStdString() << std::endl;

        std::cout << "Available commands for branch:\n";
        std::cout << "=============================:\n";
        foreach (Command *c, branchCommands)
            std::cout << c->description().toStdString() << std::endl;

        return 0;
    }

    if (options.isActive("commandslatex")) {
        foreach (Command *c, vymCommands)
            std::cout << c->descriptionLaTeX().toStdString() << std::endl;
        foreach (Command *c, modelCommands)
            std::cout << c->descriptionLaTeX().toStdString() << std::endl;
        foreach (Command *c, branchCommands)
            std::cout << c->descriptionLaTeX().toStdString() << std::endl;
        foreach (Command *c, imageCommands)
            std::cout << c->descriptionLaTeX().toStdString() << std::endl;
        return 0;
    }

    if (options.isActive("batch"))
        m.hide();
    else {
        // Paint Mainwindow first time
        qApp->processEvents();
        m.show();
    }

    // Show release notes and afterwards updates
    m.checkReleaseNotesAndUpdates();

    if (options.isActive("shortcuts"))
        switchboard
            .printASCII(); // FIXME-5 global switchboard and exit after listing

    m.loadCmdLine();

    //m.resize(1600, 900);    // only for screencasts

    // Restore last session
    if (options.isActive("restore"))
        m.fileRestoreSession();

    // Load script
    if (options.isActive("load")) {
        QString fn = options.getArg("load");
        if (!scriptEditor->loadScript(fn)) {
            QString error(QObject::tr("Error"));
            QString msg(QObject::tr("Couldn't open \"%1\"\n.").arg(fn));
            if (options.isActive("batch"))
                qWarning() << error + ": " + msg;
            else
                QMessageBox::warning(0, error, msg);
            return 0;
        }
    }

    // Run script
    if (options.isActive("run")) {
        QString fn = options.getArg("run");
        if (!scriptEditor->loadScript(fn)) {
            QString error(QObject::tr("Error"));
            QString msg(QObject::tr("Couldn't open \"%1\"\n.").arg(fn));
            if (options.isActive("batch"))
                qWarning() << error + ": " + msg;
            else
                QMessageBox::warning(0, error, msg);
            return 0;
        }
        m.runScript(scriptEditor->getScriptFile());
    }

    // For benchmarking we may want to quit instead of entering event loop
    if (options.isActive("quit"))
        return 0;

    // Enable some last minute cleanup
    QObject::connect(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));

    app.exec();

    // Cleanup
    delete noteEditor;

    int s = warningCount + criticalCount + fatalCount;
    if (s > 0)
        qDebug() << "vym exiting with:\n"
                 << warningCount << " warning messages\n"
                 << criticalCount << " critical messages\n"
                 << fatalCount << " fatal messages";
    return s;
}
