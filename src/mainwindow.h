#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QItemSelection>
#include <QJSValue>
#include <QMainWindow>
#include <QProgressDialog>
#include <QTextStream>

#include "file.h"
#include "flag.h"

#include "settings.h"

class QPrinter;
class QJSEngine;

class HistoryWindow;
class MapEditor;
class TreeItem;
class VymText;
class VymModel;
class VymView;
class VymWrapper;

class Main : public QMainWindow {
    Q_OBJECT

  public:
    /*! Modifier modes are used when SHIFT together with a mouse button is
     * pressed */
    enum ModMode {
        ModModeUndefined,  //!< Unused
        ModModePoint,      //!< Regular mode: Point and relink items
        ModModeColor,      //!< Pick color from object
        ModModeXLink,      //!< Create a XLink (XLinkObj) from selected object
        ModModeMoveObject, //!< Move object without linking
        ModModeMoveView    //!< Move view without changing
    };

    Main(QWidget *parent = 0);
    ~Main();
    void loadCmdLine();

  private:
    QProgressDialog progressDialog;
    int progressMax;
    int progressCounter;
    int progressCounterTotal;

  public:
    void logInfo(const QString &comment, const QString &caller = "");
    void statusMessage(const QString &, int timeout = 10000);
    void setProgressMaximum(int max);
    void addProgressValue(float v);
    void initProgressCounter(uint n = 1);
    void removeProgressCounter();

  public slots:
    void fileNew();
    void fileNewCopy();

  protected:
    void closeEvent(QCloseEvent *);

  public:
    QPrinter *setupPrinter();

  private:
    void setupAPI();

    /*! Helper method to clone actions later in MapEditor */
    void cloneActionMapEditor(QAction *a, QKeySequence ks);

    void setupFileActions();
    void setupEditActions();
    void setupSelectActions();
    void setupFormatActions();
    void setupViewActions();
    void setupConnectActions();
    void setupModeActions();
    void setupWindowActions();
    void setupFlagActions();

  public slots:
    void addUserFlag();

  public:
    Flag *setupFlag(const QString &path, Flag::FlagType type,
                    const QString &name, const QString &tooltip,
                    const QUuid &uid = QUuid(), const QKeySequence &ks = 0);

  private:
    void setupNetworkActions();
    void setupSettingsActions();
    void setupTestActions();
    void setupHelpActions();
    void setupContextMenus();
    void setupRecentMapsMenu();
    void setupMacros();
    void setupToolbars();
    VymView *currentView() const;
    VymView *view(const int i);

  public:
    MapEditor *currentMapEditor() const;
    VymModel *currentModel() const;
    uint currentMapId() const;
    int currentMapIndex() const;
    VymModel *getModel(uint);
    bool gotoModel(VymModel *m);
    bool gotoModelWithId(uint id);
    bool closeModelWithId(uint id);
    int modelCount();
    void updateTabName(VymModel *vm);

  private slots:
    void editorChanged();

  public slots:
    File::ErrorCode fileLoad(QString, const File::LoadMode &, const File::FileType &ftype);
    void fileLoad(const File::LoadMode &);
  private slots:
    void fileLoad();
    void fileSaveSession();
  public slots:
    void fileRestoreSession();
  private slots:
    void fileLoadRecent();
    void fileClearRecent();
    void addRecentMap(const QString &);
    void fileSave(VymModel *, const File::SaveMode &);
    void fileSave();
  public slots:
    void fileSave(VymModel *); // autosave from MapEditor
  private slots:
    void fileSaveAs();
    void fileSaveAs(const File::SaveMode &);
    void fileSaveAsDefault();
    void fileImportFirefoxBookmarks();
    void fileImportFreemind();
    void fileImportIThoughts();
    void fileImportMM();
    void fileImportDir();
    void fileExportAO();
    void fileExportASCII();
    void fileExportASCIITasks();
    void fileExportCSV();
    void fileExportConfluence();
    void fileExportFirefoxBookmarks();
    void fileExportHTML();
    void fileExportImage();
    void fileExportImpress();
    void fileExportLaTeX();
    void fileExportMarkdown();
    void fileExportOrgMode();
    void fileExportPDF();
    void fileExportSVG();
    void fileExportTaskJuggler();
    void fileExportXML();
    void fileExportLast();
    bool fileCloseMap(int i = -1); // Optionally pass number of tab
    void filePrint();
    bool fileExitVYM();

  public slots:
    void editUndo();
    void editRedo();
    void gotoHistoryStep(int);
  private slots:
    void editCopy();
    void editPaste();
    void editCut();

  public slots:
    void updateQueries(VymModel *);
    bool openURL(const QString &url);
    void openTabs(QStringList);
    void editOpenURL();
    void editOpenURLTab();

  private slots:
    void editOpenMultipleVisURLTabs(bool ignoreScrolled = true);
    void editOpenMultipleURLTabs();
    void editNote2URLs();
    void editURL();
    void editLocalURL();
    void editHeading2URL();
    void setJiraQuery();
    void getJiraDataSubtree();
    void getConfluencePageDetails();
    void getConfluencePageDetailsRecursively();
    void getConfluenceUser();
    void openVymLinks(const QStringList &, bool background = false);
    void editVymLink();
    void editOpenMultipleVymLinks();
  public slots:
    void editHeading();
    void editHeadingFinished(VymModel *m);
    void editOpenVymLink(bool background = false);
    void editOpenVymLinkBackground();
  private slots:
    void editDeleteVymLink();
    void editToggleHideExport();
    void editToggleTask();
    void editCycleTaskStatus();
    void editTaskResetDeltaPrio();
    void editTaskSleepN();
    void editAddTimestamp();
    void editMapProperties();
    void editMoveUp();
    void editMoveDown();
    void editMoveUpDiagonally();
    void editMoveDownDiagonally();
    void editDetach();
    void editSortChildren();
    void editSortBackChildren();
    void editToggleScroll();
    void editExpandAll();
    void editExpandOneLevel();
    void editCollapseOneLevel();
    void editCollapseUnselected();
    void editUnscrollSubtree();
    void editGrowSelectionSize();
    void editShrinkSelectionSize();
    void editResetSelectionSize();
    void editAddMapCenter();
    void editAddBranch();
    void editAddBranchBefore();
    void editAddBranchAbove();
    void editAddBranchBelow();
    void editImportAdd();
    void editImportReplace();
    void editSaveBranch();
    void editDeleteKeepChildren();
    void editDeleteChildren();
    void editDeleteSelection();
    void editLoadImage();
    void editSaveImage();
    void popupFollowXLink();
    void editFollowXLink(QAction *);
    void editEditXLink(QAction *);

  private slots:
    bool initLinkedMapsMenu(VymModel *model, QMenu *menu);

  public slots:
    void editGoToLinkedMap();

  private slots:
    void editToggleTarget();
    bool initTargetsMenu(VymModel *model, QMenu *menu);
    void editGoToTarget();
    void editMoveToTarget();
    void editSelectPrevious();
    void editSelectNext();
    void editSelectNothing();
    void editOpenFindResultWidget();
    void editFindNext(QString s, bool searchNotesFlag);
    void editFindDuplicateURLs();

  public slots:
    void selectQuickColor(int n);
    void setQuickColor(QColor col);
    void quickColorPressed();
    void formatPickColor();
    QColor getCurrentColor();
    int getCurrentColorIndex();
    void setCurrentColor(QColor);

  private slots:
    void formatColorBranch();
    void formatColorSubtree();
    void formatLinkStyleLine();
    void formatLinkStyleParabel();
    void formatLinkStylePolyLine();
    void formatLinkStylePolyParabel();
    void formatBackground();
    void formatSelectLinkColor();
    void formatSelectSelectionColor();
    void formatSelectFont();
    void formatToggleLinkColorHint();
    void formatHideLinkUnselected();

  public slots:
    void viewZoomReset();
    void viewZoomIn();
    void viewZoomOut();
    void viewRotateCounterClockwise();
    void viewRotateClockwise();
    void viewCenter();
    void viewCenterScaled();
    void viewCenterRotated();

  public slots:
    void networkStartServer();
    void networkConnect();
    void downloadFinished();
    void settingsPDF();
    void settingsURL();
    void settingsActionLog();
    void settingsMacroPath();
    void settingsUndoLevels();
    void settingsDefaultMapPath();

  public:
    QString defaultMapPath();   // Default path, used with "auto" to define newMapPath
    QString newMapPath();       // Depends on settings and dark theme
    bool useAutosave();
    void setAutosave(bool b);

  public slots:
    void settingsAutosaveTime();
    void settingsDefaultMapAuthor();
    void settingsDarkTheme();
    void settingsShowParentsLevelTasks();
    void settingsShowParentsLevelFindResults();
    void settingsToggleAutoLayout();
    void settingsToggleWriteBackupFile();
    void settingsToggleAnimation();
    void settingsToggleDownloads();
    bool settingsConfluence();
    bool settingsJIRA();

    void windowToggleNoteEditor();
    void windowToggleTreeEditor();
    void windowToggleTaskEditor();
    void windowToggleSlideEditor();
    void windowToggleScriptEditor();
    void windowToggleScriptOutput();
    void windowToggleHistory();
    void windowToggleProperty();
    void windowShowHeadingEditor();
    void windowToggleHeadingEditor();
    void updateHistory(SimpleSettings &);
    void windowToggleAntiAlias();
    bool isAliased();
    bool hasSmoothPixmapTransform();
    void windowToggleSmoothPixmap();
    void clearScriptOutput();
    void updateHeading(const VymText &vt);
    void updateNoteText(const VymText &vt);
    void updateNoteEditor(TreeItem *ti);
    void updateHeadingEditor(TreeItem *ti = nullptr);
    void selectInNoteEditor(QString s, int i);
    void setFocusMapEditor();
    void changeSelection(VymModel *model, const QItemSelection &newSel,
                         const QItemSelection &delSel);
    void updateDockWidgetTitles(VymModel *model);

    void updateActions();
    ModMode getModMode();
    bool autoSelectNewBranch();

    void scriptPrint(const QString &);
    QVariant runScript(const QString &);
    void abortScript(const QJSValue::ErrorType &err, const QString &msg);
    void abortScript(const QString &msg);
    QVariant setScriptResult(const QVariant &r);

  private:
    QJSEngine *scriptEngine;
    QVariant scriptResult;

  public slots:
    QObject *getCurrentModelWrapper();
    bool gotoWindow(const int &n);

  private slots:
    void windowNextEditor();
    void windowPreviousEditor();
    void nextSlide();
    void previousSlide();

    void flagChanged();

    void testFunction1();
    void testFunction2();
    void toggleWinter();
    void toggleHideExport();
    void testCommand();

    void helpDoc();
    void helpDemo();
    void helpShortcuts();
    void helpMacros();
    void helpScriptingCommands();
    void helpDebugInfo();
    void helpAbout();
    void helpAboutQT();

    void callMacro();
    void downloadReleaseNotesFinished();

  private:
    QUrl serverUrl(const QString &scriptName);
    bool checkUpdatesAfterReleaseNotes;

  public:
    void checkReleaseNotesAndUpdates();

  public slots:
    void checkReleaseNotes();
    bool downloadsEnabled(bool userTriggered = false);
    void downloadUpdatesFinished(bool userTriggered = false);
    void downloadUpdatesFinishedInt();
    void downloadUpdates(bool userTriggered);
    void checkUpdates();
    void escapePressed();
    void togglePresentationMode();
    void toggleHideTmpMode();

  private:
    QString shortcutScope; //! For listing shortcuts
    QTabWidget *tabWidget;
    qint64 *browserPID;

    QStringList imageTypes;

    QUuid prevSelection;

    HistoryWindow *historyWindow;

    QDockWidget *headingEditorDW;
    QDockWidget *noteEditorDW;
    QDockWidget *scriptEditorDW;
    QDockWidget *branchPropertyEditorDW;

  public:
    QList<QAction *>
        mapEditorActions; //! allows mapEditor to clone actions and shortcuts
    QList<QAction *>
        taskEditorActions; //! allows taskEditor to clone actions and shortcuts
  private:
    QList<QAction *>
        restrictedMapActions; //! Actions reqire map and write access
    QList<QAction *>
        unrestrictedMapActions;       //! Actions require map, but work also in
                                      //! readonly, e.g. print, copy
    QList<QAction *> actionListFiles; //! File related actions, e.g. load, save,
                                      //! restore session
    QList<QAction *> actionListBranches;
    QList<QAction *> actionListImages;
    QList<QAction *> actionListItems;

    int xLinkMenuWidth;

    QMenu *recentFilesMenu;
    enum { MaxRecentFiles = 20 };
    QAction *recentFileActions[MaxRecentFiles];
    QAction *actionRecentFilesClear;

    QAction *macroActions[48];
    QStringList macro;

    QList <QColor> quickColors;

    QMenu *toolbarsMenu;
    QToolBar *fileToolbar;
    QToolBar *clipboardToolbar;
    QToolBar *editActionsToolbar;
    QToolBar *selectionToolbar;
    QToolBar *editorsToolbar;
    QToolBar *colorsToolbar;
    QToolBar *zoomToolbar;
    QToolBar *modModesToolbar;
    QToolBar *referencesToolbar;
    QToolBar *standardFlagsToolbar;
    QToolBar *userFlagsToolbar;

    bool presentationMode;
    QMap<QToolBar *, bool>
        toolbarStates; // Save visibilty of toolbars during presentation mode

    QAction *actionFileNew;
    QAction *actionFileNewCopy;
    QAction *actionFileOpen;
    QAction *actionFileClose;
    QAction *actionFileRestoreSession;
    QAction *actionFileSave;
    QAction *actionFilePrint;
    QAction *actionFileExitVym;
    QAction *actionClearRecent;
    QAction *actionMapProperties;
    QAction *actionFileExportLast;
    QAction *actionFileExportConfluence;
    QAction *actionUndo;
    QAction *actionRedo;
    QAction *actionCopy;
    QAction *actionCut;
    QAction *actionPaste;
    QAction *actionMoveUp;
    QAction *actionMoveDown;
    QAction *actionMoveDownDiagonally;
    QAction *actionMoveUpDiagonally;
    QAction *actionDetach;
    QAction *actionSortChildren;
    QAction *actionSortBackChildren;
    QAction *actionToggleScroll;
    QAction *actionExpandAll;
    QAction *actionExpandOneLevel;
    QAction *actionCollapseOneLevel;
    QAction *actionCollapseUnselected;
    QAction *actionOpenURL;
    QAction *actionOpenURLTab;
    QAction *actionOpenMultipleVisURLTabs;
    QAction *actionOpenMultipleURLTabs;
    QAction *actionGetURLsFromNote;
    QAction *actionURLNew;
    QAction *actionLocalURL;
    QAction *actionHeading2URL;
    QAction *actionGetJiraDataSubtree;
    QAction *actionGetConfluencePageDetails;
    QAction *actionGetConfluencePageDetailsRecursively;
    QAction *actionOpenVymLink;
    QAction *actionOpenVymLinkBackground;
    QAction *actionOpenMultipleVymLinks;
    QAction *actionEditVymLink;
    QAction *actionDeleteVymLink;
    QAction *actionAddTimestamp;
    QAction *actionToggleTask;
    QAction *actionTogglePresentationMode;
    QAction *actionToggleHideTmpMode;
    QAction *actionCycleTaskStatus;
    QAction *actionTaskResetDeltaPrio;
    QAction *actionTaskSleep0;
    QAction *actionTaskSleepN;
    QAction *actionTaskSleep1;
    QAction *actionTaskSleep2;
    QAction *actionTaskSleep3;
    QAction *actionTaskSleep4;
    QAction *actionTaskSleep5;
    QAction *actionTaskSleep7;
    QAction *actionTaskSleep14;
    QAction *actionTaskSleep28;
    QAction *actionToggleHideExport;
    QAction *actionMapInfo;
    QAction *actionHeading;
    QAction *actionDelete;
    QAction *actionDeleteAlt;

  public:
    QAction *actionAddMapCenter;

  private:
    QAction *actionAddBranch;
    QAction *actionAddBranchBefore;
    QAction *actionAddBranchAbove;
    QAction *actionAddBranchBelow;
    QAction *actionDeleteKeepChildren;
    QAction *actionDeleteChildren;
    QAction *actionImportAdd;
    QAction *actionImportReplace;
    QAction *actionSaveBranch;
    QAction *actionLoadImage;

    QAction *actionGrowSelectionSize;
    QAction *actionShrinkSelectionSize;
    QAction *actionResetSelectionSize;

    QAction *actionToggleTarget;
    QAction *actionGoToTargetLinkedMap;
    QAction *actionGoToTarget;
    QAction *actionMoveToTarget;
    QAction *actionSelectPrevious;
    QAction *actionSelectNext;
    QAction *actionSelectNothing;
    QAction *actionFind;

    QActionGroup *actionGroupQuickColors;
    QAction *actionFormatQuickColor;
    QAction *actionFormatPickColor;
    QAction *actionFormatColorBranch;
    QAction *actionFormatColorSubtree;
    QAction *actionFormatLinkColorHint;
    QAction *actionFormatBackground;
    QAction *actionFormatLinkColor;
    QAction *actionFormatSelectionColor;
    QAction *actionFormatFont;

    QAction *actionZoomIn;
    QAction *actionZoomOut;
    QAction *actionZoomReset;
    QAction *actionRotateCounterClockwise;
    QAction *actionRotateClockwise;
    QAction *actionCenterOn;
    QAction *actionCenterOnScaled;
    QAction *actionCenterOnRotated;

    QActionGroup *actionGroupModModes;
    QAction *actionModModePoint;
    QAction *actionModModeColor;
    QAction *actionModModeCopy;
    QAction *actionModModeXLink;
    QAction *actionModModeMoveObject;
    QAction *actionModModeMoveView;

    QAction *actionToggleHideMode;

    QAction *actionToggleWinter;

    QActionGroup *actionGroupFormatFrameTypes;

    QActionGroup *actionGroupFormatLinkStyles;
    QAction *actionFormatLinkStyleLine;
    QAction *actionFormatLinkStyleParabel;
    QAction *actionFormatLinkStylePolyLine;
    QAction *actionFormatLinkStylePolyParabel;
    QAction *actionFormatHideLinkUnselected;

    QAction *actionViewToggleNoteEditor;
    QAction *actionViewToggleHeadingEditor;
    QAction *actionViewToggleTreeEditor;
    QAction *actionViewToggleTaskEditor;
    QAction *actionViewToggleSlideEditor;
    QAction *actionViewToggleScriptEditor;
    QAction *actionViewToggleScriptOutput;
    QAction *actionViewToggleHistoryWindow;
    QAction *actionViewTogglePropertyEditor;
    QAction *actionViewToggleAntiAlias;
    QAction *actionViewToggleSmoothPixmapTransform;
    QAction *actionViewCenter;

    QAction *actionConnectGetConfluenceUser;
    QAction *actionSettingsAutoSelectNewBranch;
    QAction *actionSettingsAutoSelectText;
    QAction *actionSettingsUseFlagGroups;
    QAction *actionSettingsUseHideExport;
    QAction *actionSettingsToggleAutosave;
    QAction *actionSettingsAutosaveTime;
    QAction *actionSettingsDarkTheme;
    QAction *actionSettingsShowParentsLevelTasks;
    QAction *actionSettingsShowParentsLevelFindResults;
    QAction *actionSettingsToggleAutoLayout;
    QAction *actionSettingsWriteBackupFile;
    QAction *actionSettingsToggleDownloads;
    QAction *actionSettingsUseAnimation;
    QAction *actionSettingsJIRA;
    QAction *actionSettingsConfluence;
};

#endif
