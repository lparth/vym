#include "texteditor.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPushButton>
#include <QPrinter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

#include "file.h"
#include "mainwindow.h"
#include "settings.h"
#include "shortcuts.h"

extern Main *mainWindow;
extern Settings settings;
extern QString iconPrefix;
extern QString iconTheme;

extern QAction *actionViewToggleNoteEditor;

extern QString vymName;

extern Switchboard switchboard;

extern QPrinter *printer;
extern bool debug;

///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

TextEditor::TextEditor()    // FIXME-2 feature insert images with drag & drop
			    // https://stackoverflow.com/questions/3254652/several-ways-of-placing-an-image-in-a-qtextedit
{
    statusBar()->hide(); // Hide sizeGrip on default, which comes with statusBar

    editor = new QTextEdit(this);
    editor->setFocus();
    editor->setTabStopDistance(20); // unit is pixel, default would be 80
    editor->setAutoFillBackground(true);
    editor->installEventFilter(this);
    connect(editor, SIGNAL(textChanged()), this, SLOT(editorChanged()));
    setCentralWidget(editor);

    connect(editor, SIGNAL(currentCharFormatChanged(const QTextCharFormat &)), this,
            SLOT(formatChanged(const QTextCharFormat &)));

    // Don't show menubar per default
    menuBar()->hide();

    // Toolbars
    setupFileActions();
    setupEditActions();
    setupFormatActions();
    setupSettingsActions();

    // Various states
    blockChangedSignal = false;
    blockTextUpdate = false;
    setInactive();

    editorName = "Text editor";
    setEditorTitle("");
}

TextEditor::~TextEditor()
{
    // Save Settings
    QString n = QString("/satellite/%1/").arg(shortcutScope);
    settings.setValue(n + "geometry/size", size());
    settings.setValue(n + "geometry/pos", pos());
    settings.setValue(n + "state", saveState(0));

    QString s;
    if (actionSettingsFonthintDefault->isChecked())
        s = "fixed";
    else
        s = "variable";
    settings.setValue(n + "fonts/fonthintDefault", s);
    settings.setValue(n + "fonts/varFont", varFont.toString());
    settings.setValue(n + "fonts/fixedFont", fixedFont.toString());

    settings.setValue(n + "colors/richTextEditorBackground", colorRichTextEditorBackground.name());
    settings.setValue(n + "colors/richTextBackground", colorRichTextBackground.name());
    settings.setValue(n + "colors/richTextForeground", colorRichTextForeground.name());
}

void TextEditor::init(const QString &scope)
{
    shortcutScope = scope;
    QString n = QString("/satellite/%1/").arg(shortcutScope);
    restoreState(settings.value(n + "state", 0).toByteArray());
    filenameHint = "";
    fixedFont.fromString(
        settings.value(n + "fonts/fixedFont", "Courier,12,-1,5,48,0,0,0,1,0")
            .toString());
    varFont.fromString(
        settings
            .value(n + "fonts/varFont", "DejaVu Sans Mono,12,-1,0,50,0,0,0,0,0")
            .toString());
    QString s =
        settings.value(n + "fonts/fonthintDefault", "variable").toString();
    if (s == "fixed") {
        actionSettingsFonthintDefault->setChecked(true);
        editor->setCurrentFont(fixedFont);
    }
    else {
        actionSettingsFonthintDefault->setChecked(false);
        editor->setCurrentFont(varFont);
    }

    // Default colors for RichText
    QPixmap pix(16, 16);
    colorRichTextEditorBackground.fromString(
        settings.value(n + "colors/richTextEditorBackground", "#ffffff").toString());
    pix.fill(colorRichTextEditorBackground);
    actionActiveEditorBGColor->setIcon(pix);


    colorRichTextForeground.fromString(
        settings.value(n + "colors/richTextForeground", "#000000").toString());
    pix.fill(colorRichTextForeground);
    actionRichTextFGColor->setIcon(pix);

    colorRichTextBackground.fromString(
        settings.value(n + "colors/richTextBackground", "#000000").toString());
    pix.fill(colorRichTextBackground);
    actionRichTextBGColor->setIcon(pix);

    // Default is PlainText
    actionFormatRichText->setChecked(false);
    clear();
}

bool TextEditor::isEmpty()
{
    if (editor->toPlainText().length() > 0)
        return false;
    else
        return true;
}

void TextEditor::setEditorTitle(const QString &s)
{
    editorTitle = (s.isEmpty()) ? editorName : editorName + ": " + s;

    // Set title of parent dockWidget
    if (parentWidget())
        parentWidget()->setWindowTitle(editorTitle);

    setWindowTitle(editorTitle);
}

QString TextEditor::getEditorTitle() { return editorTitle; }

void TextEditor::setEditorName(const QString &s) { editorName = s; }

void TextEditor::setFont(const QFont &font)
{
    blockChangedSignal = true;

    QTextCursor tc = editor->textCursor();
    QTextCharFormat format = tc.charFormat();

    tc.select(QTextCursor::Document);
    format.setFont(font);
    tc.setCharFormat(format);
    tc.clearSelection();
    fontChanged(fixedFont);

    blockChangedSignal = false;
}

void TextEditor::setFontHint(const QString &fh)
{
    if (fh == "fixed") {
        actionFormatUseFixedFont->setChecked(true);
        editor->setCurrentFont(fixedFont);
        setFont(fixedFont);
    }
    else {
        actionFormatUseFixedFont->setChecked(false);
        editor->setCurrentFont(varFont);
        setFont(varFont);
    }
}

QString TextEditor::getFontHint()
{
    if (actionFormatUseFixedFont->isChecked())
        return "fixed";
    else
        return "var";
}

QString TextEditor::getFontHintDefault()
{
    if (actionSettingsFonthintDefault->isChecked())
        return "fixed";
    else
        return "var";
}

void TextEditor::setFilename(const QString &fn)
{
    if (state == filledEditor) {
        if (fn.isEmpty()) {
            filename = "";
            mainWindow->statusMessage(
                tr("No filename available for this note.", "Statusbar message"));
        }
        else {
            filename = fn;
            mainWindow->statusMessage(
                tr(QString("Current filename is %1").arg(filename).toUtf8(),
                   "Statusbar message"));
        }
    }
}

QString TextEditor::getFilename() { return filename; }

void TextEditor::setFilenameHint(const QString &fnh) { filenameHint = fnh; }

QString TextEditor::getFilenameHint() { return filenameHint; }

QString TextEditor::getText()
{
    if (editor->toPlainText().isEmpty())
        return QString();

    if (actionFormatRichText->isChecked())
        return editor->toHtml();
    else
        return editor->toPlainText();
}

VymText TextEditor::getVymText()
{
    VymText vt;

    if (actionFormatRichText->isChecked()) {
        // Remove some QTextEdit specific tags and markers from RichText
        QString t = editor->toHtml();

        // Remove <!DOCTYPE settings in the beginning
        QRegularExpression re("(<!DOCTYPE.*)<html>");
        re.setPatternOptions(
                QRegularExpression::InvertedGreedinessOption |
                QRegularExpression::DotMatchesEverythingOption |
                QRegularExpression::MultilineOption);
        t.replace(re, "<html>");

        // Remove heading with characters that might cause problems in undo scripts
        re.setPattern("<head>.*head>");
        t.replace(re, "");
        vt.setRichText(t);
    } else
        vt.setPlainText(editor->toPlainText());

    if (actionFormatUseFixedFont->isChecked())
        vt.setFontHint(getFontHint());

    return vt;
}

bool TextEditor::findText(const QString &t,
                          const QTextDocument::FindFlags &flags)
{
    if (editor->find(t, flags))
        return true;
    else
        return false;
}

bool TextEditor::findText(const QString &t,
                          const QTextDocument::FindFlags &flags, int i)
{
    // Position at beginning
    QTextCursor c = editor->textCursor();
    c.setPosition(0, QTextCursor::MoveAnchor);
    editor->setTextCursor(c);

    // Search for t
    int j = 0;
    while (j <= i) {
        if (!editor->find(t, flags))
            return false;
        j++;
    }
    return true;
}

void TextEditor::setTextCursor(const QTextCursor &cursor)
{
    editor->setTextCursor(cursor);
}

QTextCursor TextEditor::getTextCursor() { return editor->textCursor(); }

void TextEditor::setFocus() { editor->setFocus(); }

void TextEditor::setupFileActions()
{
    QToolBar *tb = addToolBar(tr("Note Actions"));
    tb->setObjectName("noteEditorFileActions");
    QMenu *fileMenu = menuBar()->addMenu(tr("&Note", "Menubar"));

    QString tag = tr("Texteditor", "Shortcuts");
    QAction *a;
    a = new QAction(QPixmap(QString(":/document-open-%1").arg(iconTheme)), tr("&Import..."), this);
    a->setShortcut(Qt::CTRL | Qt::Key_O);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textLoad", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textLoad()));
    tb->addAction(a);
    fileMenu->addAction(a);
    actionFileLoad = a;

    fileMenu->addSeparator();
    a = new QAction(QPixmap(QString(":/document-export-%1").arg(iconTheme)), tr("&Export..."), this);
    a->setShortcut(Qt::CTRL | Qt::Key_S);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textSave", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textExport()));
    tb->addAction(a);
    fileMenu->addAction(a);
    addAction(a);
    filledEditorActions << a;
    actionFileExport = a;

    a = new QAction(tr("Export &As... (HTML)"), this);
    connect(a, SIGNAL(triggered()), this, SLOT(textExportHtml()));
    fileMenu->addAction(a);
    filledEditorActions << a;
    actionFileExportHtml = a;

    a = new QAction(tr("Export &As...(ASCII)"), this);
    switchboard.addSwitch("textExportAsASCII", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textExportText()));
    fileMenu->addAction(a);
    addAction(a);
    filledEditorActions << a;
    actionFileExportText = a;

    fileMenu->addSeparator();
    a = new QAction(QPixmap(QString(":/document-print-%1.svg").arg(iconTheme)), tr("&Print..."), this);
    a->setShortcut(Qt::CTRL | Qt::Key_P);
    switchboard.addSwitch("textPrint", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textPrint()));
    tb->addAction(a);
    fileMenu->addAction(a);
    filledEditorActions << a;
    actionFilePrint = a;

    a = new QAction(QPixmap(QString(":/edit-delete-%1.svg").arg(iconTheme)), tr("&Delete All"), this);
    connect(a, SIGNAL(triggered()), this, SLOT(deleteAll()));
    fileMenu->addAction(a);
    tb->addAction(a);
    filledEditorActions << a;
    actionFileDeleteAll = a;
}

void TextEditor::setupEditActions()
{
    QString tag = tr("Texteditor", "Shortcuts");
    QToolBar *editToolBar = addToolBar(tr("Edit Actions"));
    editToolBar->setObjectName("noteEditorEditActions");
    editToolBar->hide();
    QMenu *editMenu = menuBar()->addMenu(tr("Edi&t"));

    QAction *a;
    a = new QAction(QPixmap(":/undo.png"), tr("&Undo"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_Z);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textUndo", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), editor, SLOT(undo()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    // filledEditorActions << a;  // FIXME-2  Use undoAvailable instead?
    actionEditUndo = a;

    a = new QAction(QPixmap(":/redo.png"), tr("&Redo"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_Y);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textRedo", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), editor, SLOT(redo()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    filledEditorActions << a;
    actionEditRedo = a;

    editMenu->addSeparator();
    a = new QAction(QPixmap(), tr("Select and copy &all"), this);
    a->setShortcutContext(Qt::WidgetShortcut);
    a->setShortcut(Qt::CTRL | Qt::Key_A);
    switchboard.addSwitch("textCopyAll", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(editCopyAll()));
    editMenu->addAction(a);
    filledEditorActions << a;
    actionSelectAll = a;

    editMenu->addSeparator();
    a = new QAction(QPixmap(":/editcopy.svg"), tr("&Copy"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_C);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textCopy", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), editor, SLOT(copy()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    filledEditorActions << a;
    actionEditCopy = a;

    a = new QAction(QPixmap(":/editcut.png"), tr("Cu&t"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_X);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textCut", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), editor, SLOT(cut()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    filledEditorActions << a;
    actionEditCut = a;

    a = new QAction(QPixmap(":/editpaste.png"), tr("&Paste"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_V);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textPaste", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), editor, SLOT(paste()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    filledEditorActions << a;
    actionEditPaste = a;

    a = new QAction(QPixmap(":/" + iconPrefix + "insert-image.svg"), tr("Insert image", "TextEditor") + "...", this);
    editMenu->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(insertImage()));
    editMenu->addAction(a);
    editToolBar->addAction(a);
    filledEditorActions << a;
    actionInsertImage = a;
}

void TextEditor::setupFormatActions()
{
    QString tag = tr("Texteditor", "Shortcuts");
    fontHintsToolBar =
        addToolBar(tr("Font hints", "toolbar in texteditor"));
    fontHintsToolBar->setObjectName("noteEditorFontToolBar");
    QMenu *formatMenu = menuBar()->addMenu(tr("F&ormat"));

    QAction *a;

    a = new QAction(QPixmap(":/formatfixedfont.png"), tr("&Font hint"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_H);
    a->setCheckable(true);
    a->setChecked(
        settings.value("/noteeditor/fonts/useFixedByDefault", false).toBool());
    switchboard.addSwitch("textToggleFonthint", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(toggleFonthint()));
    formatMenu->addAction(a);
    fontHintsToolBar->addAction(a);
    filledEditorActions << a;
    actionFormatUseFixedFont = a;

    // Original icon: ./share/icons/oxygen/22x22/actions/format-text-color.png
    a = new QAction(QPixmap(":/formatrichtext.svg"), tr("&Richtext"), this);
    //  a->setShortcut(Qt::CTRL | Qt::Key_R);
    //  a->setShortcutContext (Qt::WidgetShortcut);
    a->setCheckable(true);
    switchboard.addSwitch("textToggleRichText", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(toggleRichText()));
    formatMenu->addAction(a);
    fontHintsToolBar->addAction(a);
    filledEditorActions << a;
    actionFormatRichText = a;

    fontToolBar = addToolBar(tr("Fonts", "toolbar in texteditor"));
    fontToolBar->setObjectName("noteEditorFontToolBar");

    comboFont = new QComboBox;
    fontToolBar->addWidget(comboFont);
    QFontDatabase fontDB;
    comboFont->insertItems(0, fontDB.families());
    connect(comboFont, SIGNAL(currentTextChanged(const QString &)), this,
            SLOT(textFamily(const QString &)));

    comboSize = new QComboBox;
    fontToolBar->addWidget(comboSize);
    QList<int> sizes = fontDB.standardSizes();
    QList<int>::iterator it = sizes.begin();
    int i = 0;
    while (it != sizes.end()) {
        i++;
        ++it; // increment i before using it
        comboSize->insertItem(i, QString::number(*it));
    }
    connect(comboSize, SIGNAL(currentTextChanged(const QString &)), this,
            SLOT(textSize(const QString &)));

    formatMenu->addSeparator();

    formatToolBar = addToolBar(tr("Format", "toolbar in texteditor"));
    formatToolBar->setObjectName("noteEditorFormatToolBar");

    QPixmap pix(16, 16);
    pix.fill(editor->textColor());
    a = new QAction(pix, tr("&Text Color..."), this);
    formatMenu->addAction(a);
    formatToolBar->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(selectTextFGColor()));
    filledEditorRichTextActions << a;
    actionTextFGColor = a;

    pix.fill(editor->textBackgroundColor());
    a = new QAction(pix, tr("&Text highlight color..."), this);
    formatMenu->addAction(a);
    formatToolBar->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(selectTextBGColor()));
    filledEditorRichTextActions << a;
    actionTextBGColor = a;

    a = new QAction(QPixmap(":/" + iconPrefix + "format-text-bold.svg"), tr("&Bold"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_B);
//    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textToggleBold", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textBold()));
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    a->setCheckable(true);
    filledEditorRichTextActions << a;
    actionTextBold = a;

    a = new QAction(QPixmap(":/" + iconPrefix + "format-text-italic.svg"), tr("&Italic"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_I);
//    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textToggleItalic", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textItalic()));
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    a->setCheckable(true);
    filledEditorRichTextActions << a;
    actionTextItalic = a;

    a = new QAction(QPixmap(":/" + iconPrefix + "text-format-underline.svg"), tr("&Underline"), this);
    a->setShortcut(Qt::CTRL | Qt::Key_U);
//    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    switchboard.addSwitch("textToggleUnderline", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textUnderline()));
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    filledEditorRichTextActions << a;
    a->setCheckable(true);
    // richTextWidgets.append((QWidget*)a);
    actionTextUnderline = a;
    formatMenu->addSeparator();

    QActionGroup *actGrp2 = new QActionGroup(this);
    actGrp2->setExclusive(true);
    a = new QAction(QPixmap(":/" + iconPrefix + "text-format-subscript.svg"), tr("Subs&cript"), actGrp2);
    a->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_B);
//    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    switchboard.addSwitch("textToggleSub", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textVAlign()));
    filledEditorRichTextActions << a;
    actionAlignSubScript = a;

    a = new QAction(QPixmap(":/" + iconPrefix + "text-format-superscript.svg"), tr("Su&perscript"), actGrp2);
    a->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_P);
//    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    switchboard.addSwitch("textToggleSuper", shortcutScope, a, tag);
    connect(a, SIGNAL(triggered()), this, SLOT(textVAlign()));
    filledEditorRichTextActions << a;
    actionAlignSuperScript = a;
    QActionGroup *grp = new QActionGroup(this);
    connect(grp, SIGNAL(triggered(QAction *)), this,
            SLOT(textAlign(QAction *)));

    formatMenu->addSeparator();

    a = new QAction(QPixmap(":/" + iconPrefix + "format-justify-left.svg"), tr("&Left"), grp);   // FIXME-2 "dark" is hardcoded here, needs to consider setting!
    // a->setShortcut( Qt::CTRL+Qt::Key_L );
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    filledEditorRichTextActions << a;
    actionAlignLeft = a;
    a = new QAction(QPixmap(":/" + iconPrefix + "format-justify-center.svg"), tr("C&enter"), grp);
    // a->setShortcut(  Qt::CTRL | Qt::Key_E);
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    filledEditorRichTextActions << a;
    actionAlignCenter = a;
    a = new QAction(QPixmap(":/" + iconPrefix + "format-justify-right.svg"), tr("&Right"), grp);
    // a->setShortcut(Qt::CTRL | Qt::Key_R );
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    filledEditorRichTextActions << a;
    actionAlignRight = a;
    a = new QAction(QPixmap(":/" + iconPrefix + "format-justify-fill.svg"), tr("&Justify"), grp);
    // a->setShortcut(Qt::CTRL | Qt::Key_J );
    a->setCheckable(true);
    formatToolBar->addAction(a);
    formatMenu->addAction(a);
    filledEditorRichTextActions << a;
    actionAlignJustify = a;
}

void TextEditor::setupSettingsActions()
{
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));

    QAction *a;
    a = new QAction(tr("Set &fixed font"), this);
    connect(a, SIGNAL(triggered()), this, SLOT(setFixedFont()));
    settingsMenu->addAction(a);
    actionSettingsFixedFont = a;

    a = new QAction(tr("Set &variable font"), this);
    connect(a, SIGNAL(triggered()), this, SLOT(setVarFont()));
    settingsMenu->addAction(a);
    actionSettingsVarFont = a;

    a = new QAction(tr("&fixed font is default"), this);
    a->setCheckable(true);
    // set state later in constructor...
    settingsMenu->addAction(a);
    actionSettingsFonthintDefault = a;

    settingsMenu->addSeparator();

    a = new QAction(
        tr("Set RichText mode editor background color", "TextEditor") + "...", this);
    settingsMenu->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(selectRichTextEditorBackgroundColor()));
    actionActiveEditorBGColor = a;

    a = new QAction(tr("Set RichText mode default text color", "TextEditor") + "...", this);
    settingsMenu->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(selectRichTextForegroundColor()));
    actionRichTextFGColor = a;

    a = new QAction(tr("Set RichText mode default text background color", "TextEditor") + "...", this);
    settingsMenu->addAction(a);
    connect(a, SIGNAL(triggered()), this, SLOT(selectRichTextBackgroundColor()));
    actionRichTextBGColor = a;
}

void TextEditor::textLoad()
{
    if (state != inactiveEditor) {
        if (!isEmpty()) {
            QMessageBox mb(
                   QMessageBox::Warning,
                   vymName + " - " + tr("Note Editor"),
                   "Loading will overwrite the existing note");
            QPushButton *overwriteButton = mb.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
            mb.addButton(tr("Cancel"), QMessageBox::RejectRole);
            mb.exec();
            if (mb.clickedButton() != overwriteButton) return;
        }
        // Load note
        QFileDialog *fd = new QFileDialog(this);
        QStringList types;
        types << "Text (*.txt *.html)"
              << "VYM notes and HTML (*.html)"
              << "ASCII texts (*.txt)"
              << "All files (*)";
        fd->setNameFilters(types);
        fd->setDirectory(QDir().current());
        fd->show();
        QString fn;
        if (fd->exec() == QDialog::Accepted && !fd->selectedFiles().isEmpty())
            fn = fd->selectedFiles().first();

        if (!fn.isEmpty()) {
            QFile f(fn);
            if (!f.open(QIODevice::ReadOnly))
                return;

            QTextStream ts(&f);
            setTextAuto(ts.readAll());
            editorChanged();
        }
    }
}

void TextEditor::closeEvent(QCloseEvent *ce)
{
    ce->accept(); // TextEditor can be reopened with show()
    hide();
    emit windowClosed();
    return;
}

bool TextEditor::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == editor) {
        if (ev->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
            if (keyEvent == QKeySequence::Paste) {
                // switch editor mode to match clipboard content before pasting
                const QClipboard *clipboard = QApplication::clipboard();
                const QMimeData *mimeData = clipboard->mimeData();

                if (mimeData->hasHtml() && !actionFormatRichText->isChecked())
                    setRichTextMode(true);
            }
        }
    }
    // pass the event on to the parent class
    return QMainWindow::eventFilter(obj, ev);
}

void TextEditor::editorChanged()
{
    //qDebug() << "TE::editorChanged" << editorName << "blockChanged: " << blockChangedSignal;
    EditorState oldState = state;
    if (isEmpty())
        state = emptyEditor;
    else
        state = filledEditor;

    if (!blockChangedSignal) {
        blockTextUpdate = true;
        emit textHasChanged(getVymText());
        blockTextUpdate = false;
    }

    if (state == oldState)
        return;

    updateState();
}

void TextEditor::setRichText(const QString &t)
{
    blockChangedSignal = true;
    editor->setReadOnly(false);
    editor->setHtml(t);
    actionFormatRichText->setChecked(true);
    actionInsertImage->setEnabled(true);

    // Update state including colors
    updateState();

    updateActions();
    blockChangedSignal = false;
}

void TextEditor::setPlainText(const QString &t)
{
    blockChangedSignal = true;
    editor->setReadOnly(false);

    editor->setPlainText(t);
    actionFormatRichText->setChecked(false);
    actionInsertImage->setEnabled(false);

    // Reset also text format
    QTextCharFormat textformat;
    textformat.setForeground(qApp->palette().color(QPalette::WindowText));
    textformat.setFont(varFont);
    editor->setCurrentCharFormat(textformat);

    // Update state including colors
    updateState();

    updateActions();
    blockChangedSignal = false;
}

void TextEditor::setTextAuto(const QString &t)
{
    if (Qt::mightBeRichText(t))
        setRichText(t);
    else
        setPlainText(t);
}

void TextEditor::setVymText(const VymText &vt)
{
    // While a note is being edited, we are sending textHasChanged
    // Then we don't want to update the text additionally from outside,
    // as this would position cursor at beginning of text
    if (blockTextUpdate) return;

    if (vt.getText() == getText()) return;

    if (vt.isRichText())
        setRichText(vt.getText());
    else
        setPlainText(vt.getText());
}

void TextEditor::setInactive()
{
    setState(inactiveEditor);
}

void TextEditor::editCopyAll()
{
    editor->selectAll();
    editor->copy();
}

void TextEditor::clear()
{
    //qDebug() << "TE::clear" << editorName;
    bool blockChangedOrg = blockChangedSignal;

    blockChangedSignal = true;
    editor->clear();
    setState(emptyEditor);

    blockChangedSignal = blockChangedOrg;
}

void TextEditor::deleteAll()
{
    editor->clear();
}

void TextEditor::textExportAs()
{
    QTextCharFormat f = editor->currentCharFormat();

    QString caption = tr("Export Note to single file");
    QString fn = QFileDialog::getSaveFileName(
        this, caption, QString(), "VYM Note (HTML) (*.html);;All files (*)",
        0, QFileDialog::DontConfirmOverwrite);

    if (!fn.isEmpty()) {
        QFile file(fn);
        if (file.exists()) {
            QMessageBox mb(
                QMessageBox::Warning,
                vymName,
                tr("The file %1\nexists already.\nDo you want to overwrite it?",
                   "dialog 'save note as'").arg(fn));
            QPushButton *overwriteButton = mb.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
            mb.addButton(tr("Cancel"), QMessageBox::RejectRole);
            mb.exec();
            if (mb.clickedButton() != overwriteButton) return;

            // save
            filename = fn;
            textExport();
            return;
        }
        else {
            filename = fn;
            textExport();
            return;
        }
    }
    mainWindow->statusMessage(
        tr("Couldn't export note ", "dialog 'save note as'") + fn);
}

void TextEditor::textExport()
{
    if (filename.isEmpty()) {
        textExportAs();
        return;
    }

    QString text;
    if (actionFormatRichText->isChecked())
        text = editor->toHtml();
    else
        text = editor->toPlainText();

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) {
        mainWindow->statusMessage(QString("Could not write to %1").arg(filename));
        return;
    }

    QTextStream t(&f);
    t << text;
    f.close();

    editor->document()->setModified(false);

    mainWindow->statusMessage(QString("Note exported as %1").arg(filename));
}

void TextEditor::textExportText()
{
    QString fn, s;
    if (!filenameHint.isEmpty()) {
        if (!filenameHint.contains(".txt"))
            s = filenameHint + ".txt";
        else
            s = filenameHint;
    }
    else
        s = QString();
    QString caption = tr("Export Note to single file (ASCII)");
    fn = QFileDialog::getSaveFileName(
        this, caption, s, "VYM Note (ASCII) (*.txt);;All files (*)");

    if (!fn.isEmpty()) {
        QFile file(fn);

        // Already tested in QFileDialog, if we may overwrite in case file exists already

        if (!file.open(QIODevice::WriteOnly))
            mainWindow->statusMessage(
                QString("Could not write to %1").arg(filename));
        else {
            QTextStream t(&file);
            t << getVymText().getTextASCII();
            file.close();

            mainWindow->statusMessage(QString("Note exported as %1").arg(fn));
        }
    }
}

void TextEditor::textPrint()
{
    QTextDocument *document = editor->document();

    if (!printer)
        mainWindow->setupPrinter();

    QPrintDialog dialog(printer, this);
    dialog.setWindowTitle(tr("Print", "TextEditor"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    document->print(printer);
}

void TextEditor::textEditUndo() {}

void TextEditor::toggleFonthint()
{
    if (!actionFormatUseFixedFont->isChecked()) {
        editor->setCurrentFont(varFont);
        setFont(varFont);
    }
    else {
        editor->setCurrentFont(fixedFont);
        setFont(fixedFont);
    }
    emit textHasChanged(getVymText());
}

void TextEditor::setRichTextMode(bool b)
{
    //qDebug() << "TE::setRichTextMode b=" << b;
    actionFormatUseFixedFont->setEnabled(false);
    if (b) {
        setRichText(editor->toHtml());

        // Use default foreground color for all text when switching to RichText
        QTextCursor cursor = editor->textCursor();
        editor->selectAll();
        editor->setTextColor(colorRichTextForeground);
        editor->setTextBackgroundColor(colorRichTextBackground);
        editor->setTextCursor(cursor);
        
    } else {
        setPlainText(editor->toPlainText());
    }
    emit textHasChanged(getVymText());
}

void TextEditor::toggleRichText()
{
    if (actionFormatRichText->isChecked())
        setRichTextMode(true);
    else
        setRichTextMode(false);
}

void TextEditor::setFixedFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, fixedFont, this);
    if (ok)
        fixedFont = font;
}

void TextEditor::setVarFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, varFont, this);
    if (ok)
        varFont = font;
}

void TextEditor::textBold()
{
    if (actionTextBold->isChecked())
        editor->setFontWeight(QFont::Bold);
    else
        editor->setFontWeight(QFont::Normal);
}

void TextEditor::textUnderline()
{
    editor->setFontUnderline(actionTextUnderline->isChecked());
}

void TextEditor::textItalic()
{
    editor->setFontItalic(actionTextItalic->isChecked());
}

void TextEditor::textFamily(const QString &f) { editor->setFontFamily(f); }

void TextEditor::textSize(const QString &p) { editor->setFontPointSize(p.toInt()); }

void TextEditor::selectTextFGColor()
{
    QColor col = QColorDialog::getColor(editor->textColor(), this);
    if (!col.isValid())
        return;
    editor->setTextColor(col);
    /*
    QPixmap pix( 16, 16 );
    pix.fill( col );
    actionTextColor->setIcon( pix );
    */
}

void TextEditor::selectTextBGColor()
{
    QColor col = QColorDialog::getColor(editor->textBackgroundColor(), this);
    if (!col.isValid())
        return;
    editor->setTextBackgroundColor(col);
    /*
    QPixmap pix( 16, 16 );
    pix.fill( col );
    actionTextColor->setIcon( pix );
    */
}

void TextEditor::textAlign(QAction *a)
{
    QTextCursor c = editor->textCursor();

    if (a == actionAlignLeft)
        editor->setAlignment(Qt::AlignLeft);
    else if (a == actionAlignCenter)
        editor->setAlignment(Qt::AlignHCenter);
    else if (a == actionAlignRight)
        editor->setAlignment(Qt::AlignRight);
    else if (a == actionAlignJustify)
        editor->setAlignment(Qt::AlignJustify);
}

void TextEditor::textVAlign()
{
    QTextCharFormat format;

    if (sender() == actionAlignSuperScript &&
        actionAlignSuperScript->isChecked()) {
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    }
    else if (sender() == actionAlignSubScript &&
             actionAlignSubScript->isChecked()) {
        format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    }
    else {
        format.setVerticalAlignment(QTextCharFormat::AlignNormal);
    }
    editor->mergeCurrentCharFormat(format);
}

void TextEditor::fontChanged(const QFont &f)
{
    int i = comboFont->findText(f.family());
    if (i >= 0)
        comboFont->setCurrentIndex(i);
    i = comboSize->findText(QString::number(f.pointSize()));
    if (i >= 0)
        comboSize->setCurrentIndex(i);
    actionTextBold->setChecked(f.bold());
    actionTextItalic->setChecked(f.italic());
    actionTextUnderline->setChecked(f.underline());
}

void TextEditor::colorFGChanged(const QColor &c)
{
    QImage image("icons/color-text.svg");
    QPainter painter;
    painter.begin(&image);
    painter.setBrush(c);
    painter.drawRect(0,110,128,128);
    painter.end();

    actionTextFGColor->setIcon(QPixmap::fromImage(image));
}

void TextEditor::colorBGChanged(const QColor &c)
{
    QImage image("icons/draw-brush.svg");
    QPainter painter;
    painter.begin(&image);
    painter.setBrush(c);
    painter.drawRect(0,110,128,128);
    painter.end();

    actionTextBGColor->setIcon(QPixmap::fromImage(image));
}

void TextEditor::formatChanged(const QTextCharFormat &f)
{
    //qDebug() << "TE::formatChanged  fg=" << f.foreground().color() << " bg=" << f.background().color() << " valid=" << f.isValid();
    if (!actionFormatRichText->isChecked())
        return;
    fontChanged(f.font());
    colorFGChanged(f.foreground().color());
    colorBGChanged(f.background().color());
    alignmentChanged(editor->alignment());
    verticalAlignmentChanged(f.verticalAlignment());
}

void TextEditor::alignmentChanged(int a)
{
    if ((a == Qt::AlignLeft) || (a & Qt::AlignLeft))
        actionAlignLeft->setChecked(true);
    else if ((a & Qt::AlignHCenter))
        actionAlignCenter->setChecked(true);
    else if ((a & Qt::AlignRight))
        actionAlignRight->setChecked(true);
    else if ((a & Qt::AlignJustify))
        actionAlignJustify->setChecked(true);
}

void TextEditor::verticalAlignmentChanged(QTextCharFormat::VerticalAlignment a)
{
    actionAlignSubScript->setChecked(false);
    actionAlignSuperScript->setChecked(false);
    switch (a) {
    case QTextCharFormat::AlignSuperScript:
        actionAlignSuperScript->setChecked(true);
        break;
    case QTextCharFormat::AlignSubScript:
        actionAlignSubScript->setChecked(true);
        break;
    default:;
    }
}

void TextEditor::updateActions()
{
    if (state == inactiveEditor) {
        actionFileLoad->setEnabled(false);
        foreach (QAction* a, filledEditorActions)
            a->setEnabled(false);
        foreach (QAction* a, filledEditorActions)
            a->setEnabled(false);
        foreach (QAction* a, filledEditorRichTextActions)
            a->setEnabled(false);
        return;
    }

    actionFileLoad->setEnabled(true);

    // editorState is filledEditor or emptyEditor
    foreach (QAction* a, emptyEditorActions)
        a->setEnabled(true);

    bool b = (state == filledEditor) ? true : false;
    foreach (QAction* a, filledEditorActions)
        a->setEnabled(b);

    b = (state == filledEditor && actionFormatRichText->isChecked()) ? true : false;
    foreach (QAction* a, filledEditorRichTextActions)
        a->setEnabled(b);
}

void TextEditor::setState(EditorState s)
{
    // qDebug() << "TE::setState" << s << editorName;
    QPalette p = qApp->palette();
    QColor baseColor;
    state = s;
    switch (state) {
        case emptyEditor:
            if (actionFormatRichText->isChecked()) {
                editor->setTextColor(colorRichTextForeground);
                editor->setTextBackgroundColor(colorRichTextBackground);
	    } else
                editor->setTextColor(p.color(QPalette::Text));

        case filledEditor:
            if (actionFormatRichText->isChecked()) {
                if (useColorMapBackground)
                    baseColor = colorMapBackground;
                else
                    baseColor = colorRichTextEditorBackground;
            } else {
                baseColor = p.color(QPalette::Base);
            }
            editor->setReadOnly(false);
            break;
        case inactiveEditor:
            baseColor = Qt::black;
            editor->setReadOnly(true);
    }
    p.setColor(QPalette::Base, baseColor);
    editor->setPalette(p);

    updateActions();
}

void TextEditor::updateState()
{
    //qDebug() << "TE::updateState" << editorName;
    if (isEmpty())
        setState(emptyEditor);
    else
        setState(filledEditor);
}

void TextEditor::selectRichTextEditorBackgroundColor()
{
    QColor col = QColorDialog::getColor(colorRichTextEditorBackground, nullptr);
    if (!col.isValid())
        return;
    colorRichTextEditorBackground = col;
    QPixmap pix(16, 16);
    pix.fill(colorRichTextEditorBackground);
    actionActiveEditorBGColor->setIcon(pix);
}

void TextEditor::selectRichTextForegroundColor()
{
    QColor col = QColorDialog::getColor(colorRichTextForeground, nullptr);
    setRichTextForegroundColor(col);
}

void TextEditor::selectRichTextBackgroundColor()
{
    QColor col = QColorDialog::getColor(colorRichTextBackground, nullptr);
    setRichTextBackgroundColor(col);
}

void TextEditor::insertImage()
{
    QStringList imagePaths = openImageDialog();

    foreach (QString path, imagePaths) {
	QUrl Uri ( QString ( "file://%1" ).arg (path));
	QImage image = QImageReader (path).read();

	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	QString encodedImage = buffer.data().toBase64();

	QTextDocument * textDocument = editor->document();
	textDocument->addResource( QTextDocument::ImageResource, Uri, QVariant (image));
	QTextCursor cursor = editor->textCursor();
	QTextImageFormat imageFormat;
	imageFormat.setWidth(image.width());
	imageFormat.setHeight(image.height());
	imageFormat.setName(Uri.toString());
	//cursor.insertImage(imageFormat);
	cursor.insertHtml("<img src=\"data:image;base64," + encodedImage + "\"/>");
    }
}

void TextEditor::setRichTextForegroundColor(const QColor &col)
{
    if (!col.isValid()) return;

    colorRichTextForeground = col;
    QPixmap pix(16, 16);
    pix.fill(colorRichTextForeground);
    actionRichTextFGColor->setIcon(pix);
}

void TextEditor::setRichTextBackgroundColor(const QColor &col)
{
    if (!col.isValid()) return;

    colorRichTextBackground = col;
    QPixmap pix(16, 16);
    pix.fill(colorRichTextBackground);
    actionRichTextBGColor->setIcon(pix);
}

void TextEditor::setMapBackgroundColor(const QColor &col)
{
    colorMapBackground = col;
}

void TextEditor::setUseMapBackgroundColor(bool b)
{
    useColorMapBackground = b;
}
