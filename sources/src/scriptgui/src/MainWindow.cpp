#include "MainWindow.hpp"
#include "AppStyle.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QStyle>
#include <QShortcut>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QRegularExpression>
#include <QTabBar>
#include <QSaveFile>
#include <QHash>
#include <QVector>
#include <cstring>
#include "ICommDumpProtocol.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Canonical monospace font builder — single source of truth used everywhere.
//
//  QFontDatabase::systemFont(FixedFont) gives the OS-default monospace font
//  (e.g. "DejaVu Sans Mono" on most Linux distros, "Courier New" on Windows).
//  We try a preferred list first; if none is installed we fall back to the
//  system fixed font so we always get a real monospace — never a proportional
//  fallback that would make spaces look narrow.
// ─────────────────────────────────────────────────────────────────────────────
static QFont buildEditorFont(int pointSize)
{
    // Cache per point-size — the preferred-family scan is identical every call.
    static QHash<int, QFont> cache;
    if (cache.contains(pointSize))
        return cache.value(pointSize);

    static const QStringList preferred = {
        "JetBrains Mono", "Cascadia Code", "Cascadia Mono",
        "Fira Code", "Hack", "Consolas",
        "DejaVu Sans Mono", "Liberation Mono", "Courier New"
    };

    QFont f;
    for (const QString &fam : preferred) {
        if (QFontDatabase::hasFamily(fam)) {
            f = QFont(fam, pointSize);
            f.setFixedPitch(true);
            f.setStyleHint(QFont::Monospace);
            cache.insert(pointSize, f);
            return f;
        }
    }
    f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(pointSize);
    cache.insert(pointSize, f);
    return f;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_process(new QProcess(this))
{
    setWindowTitle("µScript Front-End");
    setMinimumSize(1100, 680);
    setAcceptDrops(true);

    QSettings cfg;
    restoreGeometry(cfg.value("window/geometry").toByteArray());

    // ── Build UI ──────────────────────────────────────────────────────────
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildToolbar());
    rootLayout->addWidget(buildCentralWidget(), 1);
    rootLayout->addWidget(buildStatusBar());
    setCentralWidget(root);

    // ── Wire QProcess ─────────────────────────────────────────────────────
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,  this, &MainWindow::onProcessError);
    connect(m_process, &QProcess::started,                 this, &MainWindow::onProcessStarted);
    connect(m_process, &QProcess::finished,                this, &MainWindow::onProcessFinished);

    // ── Font-size shortcuts ───────────────────────────────────────────────
    auto *scPlus  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus),  this);
    auto *scEqual = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    auto *scMinus = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    auto *scReset = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0),     this);
    connect(scPlus,  &QShortcut::activated, this, [this]{ adjustFontSize(+1); });
    connect(scEqual, &QShortcut::activated, this, [this]{ adjustFontSize(+1); });
    connect(scMinus, &QShortcut::activated, this, [this]{ adjustFontSize(-1); });
    connect(scReset, &QShortcut::activated, this, [this]{ adjustFontSize(0);  });

    // ── Save shortcuts ────────────────────────────────────────────────────
    auto *scSave    = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    auto *scSaveAll = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this);
    connect(scSave,    &QShortcut::activated, this, [this]{ saveCurrentTab(); });
    connect(scSaveAll, &QShortcut::activated, this, [this]{ saveAllTabs(); });

    // ── Tab shortcuts ─────────────────────────────────────────────────────
    auto *scNewTab   = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    auto *scCloseTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    auto *scNextTab  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    auto *scPrevTab  = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(scNewTab,   &QShortcut::activated, this, [this]{ addTab(); });
    connect(scCloseTab, &QShortcut::activated, this, [this]{
        onTabCloseRequested(m_tabWidget->currentIndex());
    });
    connect(scNextTab, &QShortcut::activated, this, [this]{
        const int n = m_tabWidget->count();
        m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() + 1) % n);
    });
    connect(scPrevTab, &QShortcut::activated, this, [this]{
        const int n = m_tabWidget->count();
        m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() + n - 1) % n);
    });

    // ── Restore session ───────────────────────────────────────────────────
    m_fontSize = cfg.value("session/fontSize", k_fontDefault).toInt();
    // Note: applyFontSize() is called AFTER tabs are restored below

    // Restore all previously open script tabs
    const QStringList tabPaths = cfg.value("session/tabPaths").toStringList();
    const int activeTab = cfg.value("session/activeTab", 0).toInt();
    for (const QString &p : tabPaths) {
        if (QFileInfo::exists(p))
            addTab(p);
    }
    if (m_tabWidget->count() == 0)
        addTab();   // always have at least one tab

    const int clampedTab = qBound(0, activeTab, m_tabWidget->count() - 1);
    m_tabWidget->setCurrentIndex(clampedTab);
    syncPathEdit(clampedTab);

    applyFontSize();   // called here so all restored tabs get the right font
    setStatus("Ready");
}

MainWindow::~MainWindow() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  UI builders
// ─────────────────────────────────────────────────────────────────────────────
QFrame *MainWindow::buildToolbar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName("toolbar");
    bar->setFrameShape(QFrame::NoFrame);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 0, 12, 0);
    lay->setSpacing(8);

    auto *appLabel = new QLabel("µSCRIPT", bar);
    appLabel->setObjectName("toolbarLabel");

    // Interpreter binary
    auto *interpLabel = new QLabel("INTERPRETER", bar);
    interpLabel->setObjectName("toolbarLabel");

    auto *interpEdit = new QLineEdit(bar);
    m_interpEdit = interpEdit;
    interpEdit->setObjectName("interpPathEdit");
    interpEdit->setPlaceholderText("path/to/interpreter binary…");
    interpEdit->setToolTip("Path to the ScriptInterpreter executable");

    QSettings cfg;
    {
        QString savedInterp = cfg.value("session/interpreterPath").toString().trimmed();
        // Resolve a relative saved path against the application directory.
        // This handles the case where the deployment folder has been moved or
        // the app is launched from a different CWD than where it was first configured.
        if (!savedInterp.isEmpty()) {
            QFileInfo fi(savedInterp);
            if (fi.isRelative()) {
                const QString resolved =
                    QDir(QCoreApplication::applicationDirPath()).filePath(savedInterp);
                if (QFileInfo::exists(resolved))
                    savedInterp = QFileInfo(resolved).absoluteFilePath();
            } else if (!fi.exists()) {
                // Stale absolute path (e.g. deploy folder was moved) — clear it
                // so the auto-detection below falls back to applicationDirPath().
                savedInterp.clear();
            }
        }
        interpEdit->setText(savedInterp);
        m_interpreterPath = savedInterp;
    }
    connect(interpEdit, &QLineEdit::textEdited, this, [this, interpEdit](const QString &t) {
        m_interpreterPath = t;
        // Always persist as an absolute path so the entry survives CWD changes.
        QString toSave = t.trimmed();
        if (!toSave.isEmpty() && QFileInfo(toSave).isRelative() && QFileInfo(toSave).exists())
            toSave = QFileInfo(toSave).absoluteFilePath();
        QSettings s; s.setValue("session/interpreterPath", toSave);
    });

    auto *interpBrowse = new QPushButton("…", bar);
    interpBrowse->setObjectName("browseBtn");
    interpBrowse->setToolTip("Browse for interpreter binary");
    connect(interpBrowse, &QPushButton::clicked, this, [this, interpEdit] {
        const QString f = QFileDialog::getOpenFileName(
            this, "Select Interpreter Binary",
            interpEdit->text().isEmpty()
                ? QDir::homePath()
                : QFileInfo(interpEdit->text()).absolutePath());
        if (!f.isEmpty()) interpEdit->setText(f);
    });

    // Active-tab script path
    auto *scriptLabel = new QLabel("SCRIPT", bar);
    scriptLabel->setObjectName("toolbarLabel");

    m_scriptPathEdit = new QLineEdit(bar);
    m_scriptPathEdit->setPlaceholderText("path/to/script.txt  — Enter to load, drag-and-drop accepted…");
    m_scriptPathEdit->setToolTip("Active tab's script path — press Enter to load\n"
                                 "Click to switch to this file's tab");
    m_scriptPathEdit->installEventFilter(this);

    connect(m_scriptPathEdit, &QLineEdit::returnPressed, this, [this] {
        const QString path = m_scriptPathEdit->text().trimmed();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            loadIntoCurrentTab(path);
        } else if (!path.isEmpty()) {
            m_w3->appendStatus(QString("File not found: %1").arg(path));
        }
    });

    auto *browseBtn = new QPushButton("…", bar);
    browseBtn->setObjectName("browseBtn");
    browseBtn->setToolTip("Browse for script file");
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowse);

    // INI config file
    auto *iniLabel = new QLabel("CONFIG", bar);
    iniLabel->setObjectName("toolbarLabel");

    m_iniPathEdit = new QLineEdit(bar);
    m_iniPathEdit->setObjectName("interpPathEdit");   // reuse same QSS
    m_iniPathEdit->setPlaceholderText("path/to/uscript.ini…");
    m_iniPathEdit->setToolTip("INI configuration file passed as -c to the interpreter\n"
                              "Click to open/switch to this file in a tab");

    // Click on a valid path → open in tab (or switch if already open)
    m_iniPathEdit->installEventFilter(this);

    m_iniPath = cfg.value("session/iniPath").toString();
    m_iniPathEdit->setText(m_iniPath);
    connect(m_iniPathEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_iniPath = t;
        QSettings s; s.setValue("session/iniPath", t);
    });

    auto *iniBrowseBtn = new QPushButton("…", bar);
    iniBrowseBtn->setObjectName("browseBtn");
    iniBrowseBtn->setToolTip("Browse for INI config file");
    connect(iniBrowseBtn, &QPushButton::clicked, this, [this] {
        const QString start = m_iniPath.isEmpty()
            ? (m_scriptPathEdit->text().isEmpty()
                   ? QDir::homePath()
                   : QFileInfo(m_scriptPathEdit->text()).absolutePath())
            : QFileInfo(m_iniPath).absolutePath();
        const QString f = QFileDialog::getOpenFileName(
            this, "Select INI Config File", start,
            "INI files (*.ini);;All files (*)");
        if (!f.isEmpty()) m_iniPathEdit->setText(f);
    });

    // Reload every open script/INI file from disk
    m_reloadBtn = new QPushButton("⟳  RELOAD", bar);
    m_reloadBtn->setObjectName("reloadBtn");
    m_reloadBtn->setToolTip("Reload every open script and INI file from disk\n"
                            "(all tabs, plus the comm-script window)");
    connect(m_reloadBtn, &QPushButton::clicked, this, &MainWindow::onReloadAll);

    // Reset error bars
    m_resetBtn = new QPushButton("✕  RESET", bar);
    m_resetBtn->setObjectName("resetErrorBtn");
    m_resetBtn->setToolTip("Clear all validation/execution error markers\n"
                           "from the script windows without changing their content");
    m_resetBtn->setEnabled(false);   // enabled only when there are errors to clear
    connect(m_resetBtn, &QPushButton::clicked, this, &MainWindow::onResetErrorBars);

    // Run / Stop
    m_startStopBtn = new QPushButton("▶  RUN", bar);
    m_startStopBtn->setObjectName("startBtn");
    m_startStopBtn->setToolTip("Run active tab's script");
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStop);

    // LED
    m_led      = new StatusLed(bar);
    m_ledLabel = new QLabel("IDLE", bar);
    m_ledLabel->setObjectName("toolbarLabel");

    lay->addWidget(appLabel);
    lay->addSpacing(12);
    lay->addWidget(interpLabel);
    lay->addWidget(interpEdit, 1);
    lay->addWidget(interpBrowse);
    lay->addSpacing(8);
    lay->addWidget(scriptLabel);
    lay->addWidget(m_scriptPathEdit, 1);
    lay->addWidget(browseBtn);
    lay->addSpacing(8);
    lay->addWidget(iniLabel);
    lay->addWidget(m_iniPathEdit, 1);
    lay->addWidget(iniBrowseBtn);
    lay->addSpacing(8);
    lay->addWidget(m_reloadBtn);
    lay->addSpacing(4);
    lay->addWidget(m_resetBtn);
    lay->addSpacing(4);
    lay->addWidget(m_startStopBtn);
    lay->addSpacing(6);
    lay->addWidget(m_led);
    lay->addWidget(m_ledLabel);

    return bar;
}

QWidget *MainWindow::buildCentralWidget()
{
    // ── Tab widget for main scripts ───────────────────────────────────────
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);          // tabs can be reordered by drag
    m_tabWidget->setDocumentMode(true);     // cleaner look, no box around tabs
    m_tabWidget->setElideMode(Qt::ElideMiddle);

    // Style the tab bar to match the dark theme.
    // IMPORTANT: do NOT set color: in QTabBar::tab rules — it would override
    // setTabTextColor() which we use to show modified/clean/running state.
    // All tab text colours are set exclusively via setTabTextColor().
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane { border: none; background: #12141a; }

        QTabBar::tab {
            background:    #0e1016;
            border:        1px solid #252a35;
            border-bottom: none;
            padding:       5px 20px 5px 12px;
            font-size:     13px;
            min-width:     90px;
        }
        QTabBar::tab:selected {
            background:  #1c1f27;
            border-top:  2px solid #4a9eff;
        }
        QTabBar::tab:hover:!selected {
            background: #161920;
        }

        /* Close button — visible × on a dark pill */
        QTabBar::close-button {
            subcontrol-position: right;
            subcontrol-origin:   padding;
            width:   16px;
            height:  16px;
            margin:  0 2px 0 0;
            border-radius: 3px;
            background: #252a35;
        }
        QTabBar::close-button:hover  { background: #ff5555; }
        QTabBar::close-button:pressed{ background: #cc2222; }

        QTabBar::tear  { border: none; }
        QTabBar::scroller { width: 20px; }
    )");

    // Corner widget: [+]  [SAVE]  [SAVE ALL]  for the main script tab bar
    {
        auto *cornerBar = new QWidget(m_tabWidget);
        auto *cLay = new QHBoxLayout(cornerBar);
        cLay->setContentsMargins(0, 0, 4, 0);
        cLay->setSpacing(3);

        auto *addTabBtn = new QPushButton("+", cornerBar);
        addTabBtn->setObjectName("clearBtn");
        addTabBtn->setToolTip("New script tab  (Ctrl+T)");
        addTabBtn->setFixedSize(24, 24);
        connect(addTabBtn, &QPushButton::clicked, this, [this]{ addTab(); });

        auto *saveBtn = new QPushButton("SAVE", cornerBar);
        saveBtn->setObjectName("clearBtn");
        saveBtn->setToolTip("Save active tab  (Ctrl+S)");
        saveBtn->setFixedHeight(24);
        connect(saveBtn, &QPushButton::clicked, this, [this]{ saveCurrentTab(); });

        auto *saveAllBtn = new QPushButton("SAVE ALL", cornerBar);
        saveAllBtn->setObjectName("clearBtn");
        saveAllBtn->setToolTip("Save all modified tabs  (Ctrl+Shift+S)");
        saveAllBtn->setFixedHeight(24);
        connect(saveAllBtn, &QPushButton::clicked, this, [this]{ saveAllTabs(); });

        cLay->addWidget(addTabBtn);
        cLay->addWidget(saveBtn);
        cLay->addWidget(saveAllBtn);
        m_tabWidget->setCornerWidget(cornerBar, Qt::TopRightCorner);
    }

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this,        &MainWindow::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this,        &MainWindow::onCurrentTabChanged);

    // ── Comm script viewer + log ──────────────────────────────────────────
    // Comm script panel — wrapper with its own save button bar
    auto *commWrapper = new QWidget(this);
    {
        auto *wLay = new QVBoxLayout(commWrapper);
        wLay->setContentsMargins(0, 0, 0, 0);
        wLay->setSpacing(0);

        // Thin save bar above the comm viewer
        auto *commBar = new QFrame(commWrapper);
        commBar->setObjectName("panelHeader");
        commBar->setFrameShape(QFrame::NoFrame);
        auto *cbLay = new QHBoxLayout(commBar);
        cbLay->setContentsMargins(8, 0, 4, 0);
        cbLay->setSpacing(4);
        auto *commLabel = new QLabel("COMM SCRIPT", commBar);
        commLabel->setObjectName("panelTitle");
        m_commScriptNameLabel = new QLabel("", commBar);
        m_commScriptNameLabel->setObjectName("panelInfo");
        m_commScriptNameLabel->setStyleSheet("font-size: 13px; color: #c8d0e0;");
        auto *commSaveBtn = new QPushButton("SAVE", commBar);
        commSaveBtn->setObjectName("clearBtn");
        commSaveBtn->setToolTip("Save comm script");
        commSaveBtn->setFixedHeight(22);
        connect(commSaveBtn, &QPushButton::clicked, this, [this]{ 
            if (m_w2->save())
                setStatus(QString("Saved: %1").arg(
                    QFileInfo(m_w2->currentFile()).fileName()));
        });

        auto *commClearBtn = new QPushButton("CLEAR", commBar);
        commClearBtn->setObjectName("clearBtn");
        commClearBtn->setToolTip("Unload comm script");
        commClearBtn->setFixedHeight(22);
        connect(commClearBtn, &QPushButton::clicked, this, [this] {
            if (m_w2->isModified()) {
                QMessageBox dlg(this);
                dlg.setWindowTitle("Unsaved changes");
                dlg.setText(QString("Comm script \"%1\" has unsaved changes.\nSave before closing?")
                    .arg(QFileInfo(m_w2->currentFile()).fileName()));
                dlg.setIcon(QMessageBox::Question);
                auto *saveBtn    = dlg.addButton("Save",    QMessageBox::AcceptRole);
                auto *discardBtn = dlg.addButton("Discard", QMessageBox::DestructiveRole);
                dlg.addButton("Cancel", QMessageBox::RejectRole);
                dlg.setDefaultButton(saveBtn);
                dlg.exec();
                const auto *clicked = dlg.clickedButton();
                if (clicked == saveBtn    && !m_w2->save()) return; // save failed / cancelled
                if (clicked != saveBtn && clicked != discardBtn)    return; // Cancel or ×
            }
            m_w2->clear();
            setStatus("Comm script cleared");
        });

        cbLay->addWidget(commLabel);
        cbLay->addWidget(m_commScriptNameLabel);
        cbLay->addStretch();
        cbLay->addWidget(commSaveBtn);
        cbLay->addWidget(commClearBtn);

        m_w2 = new ScriptViewer(commWrapper);
        m_w2->enableCommHighlighting(true);
        connect(m_w2, &ScriptViewer::infoChanged,
                m_commScriptNameLabel, &QLabel::setText);
        connect(m_w2, &ScriptViewer::modificationChanged,
                this, [this](bool modified) {
            m_commScriptNameLabel->setStyleSheet(
                modified ? "font-size: 13px; color: #ff5555;"
                         : "font-size: 13px; color: #c8d0e0;");
        });
        wLay->addWidget(commBar);
        wLay->addWidget(m_w2, 1);
    }

    // ── Comm-script tab bar ─────────────────────────────────────────────────
    // Tab 0 ("MAIN") hosts commWrapper/m_w2 unchanged — sequential (non-'&')
    // comm-script execution, exactly as before this feature existed.
    // GUI:LOAD_COMM_T:<tid>:<path> opens one additional, closable tab per
    // running comm-script thread (tid > 0), so parallel '&' comm scripts
    // each get their own execution view instead of fighting over a single
    // viewer. Finished threads keep their tab (per user preference) until
    // manually closed via its × button — GUI:CLEAR_COMM_T only drops the
    // "●" live marker.
    m_commTabs = new QTabWidget(this);
    m_commTabs->setTabsClosable(true);
    m_commTabs->setDocumentMode(true);
    m_commTabs->setElideMode(Qt::ElideMiddle);
    // Same dark tab-bar styling as m_tabWidget, scoped to this instance.
    m_commTabs->setStyleSheet(R"(
        QTabWidget::pane { border: none; background: #12141a; }

        QTabBar::tab {
            background:    #0e1016;
            border:        1px solid #252a35;
            border-bottom: none;
            padding:       5px 20px 5px 12px;
            font-size:     13px;
            min-width:     90px;
        }
        QTabBar::tab:selected {
            background:  #1c1f27;
            border-top:  2px solid #4a9eff;
        }
        QTabBar::tab:hover:!selected {
            background: #161920;
        }

        QTabBar::close-button {
            subcontrol-position: right;
            subcontrol-origin:   padding;
            width:   16px;
            height:  16px;
            margin:  0 2px 0 0;
            border-radius: 3px;
            background: #252a35;
        }
        QTabBar::close-button:hover  { background: #ff5555; }
        QTabBar::close-button:pressed{ background: #cc2222; }

        QTabBar::tear  { border: none; }
        QTabBar::scroller { width: 20px; }
    )");
    m_commTabs->addTab(commWrapper, "MAIN");
    // "MAIN" is permanent — remove its close button specifically (index 0);
    // dynamic per-thread tabs added later keep theirs.
    m_commTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    connect(m_commTabs, &QTabWidget::tabCloseRequested,
            this,       &MainWindow::onCommTabCloseRequested);

    // Corner widget: [CLOSE ALL] — closes every per-thread tab, keeps MAIN.
    {
        auto *cornerBar = new QWidget(m_commTabs);
        auto *cLay = new QHBoxLayout(cornerBar);
        cLay->setContentsMargins(0, 0, 4, 0);
        cLay->setSpacing(3);

        auto *closeAllBtn = new QPushButton("CLOSE ALL", cornerBar);
        closeAllBtn->setObjectName("clearBtn");
        closeAllBtn->setToolTip("Close all per-thread comm-script tabs (keeps MAIN)");
        closeAllBtn->setFixedHeight(22);
        connect(closeAllBtn, &QPushButton::clicked, this, &MainWindow::closeAllCommThreadTabs);

        cLay->addWidget(closeAllBtn);
        m_commTabs->setCornerWidget(cornerBar, Qt::TopRightCorner);
    }
    m_w3 = new LogViewer(this);

    // ── Comm-dump panel (always visible, between OUTPUT LOG and SHELL) ────
    m_wCommDump = new CommDumpView(this);

    // ── Shell terminal (always present, collapsed until GUI:SHELL_RUN) ────
    m_w4 = new ShellTerminal(this);

    // Connect m_w4 key presses directly to the interpreter's stdin
    connect(m_w4, &ShellTerminal::keyBytesReady,
            this, [this](const QByteArray &bytes) {
        if (m_process->state() == QProcess::Running)
            m_process->write(bytes);
    });

    // Vertical splitter: OUTPUT LOG / COMM DUMP / SHELL TERMINAL
    m_logShellSplit = new QSplitter(Qt::Vertical, this);
    m_logShellSplit->addWidget(m_w3);
    m_logShellSplit->addWidget(m_wCommDump);
    m_logShellSplit->addWidget(m_w4);
    m_logShellSplit->setStretchFactor(0, 2);
    m_logShellSplit->setStretchFactor(1, 1);
    m_logShellSplit->setStretchFactor(2, 0);
    m_logShellSplit->setHandleWidth(3);
    // Start with m_w4 fully collapsed; log/comm-dump share the rest 2:1
    m_logShellSplit->setSizes({2, 1, 0});

    auto *vSplit = new QSplitter(Qt::Vertical, this);
    vSplit->addWidget(m_tabWidget);
    vSplit->addWidget(m_commTabs);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 2);
    vSplit->setHandleWidth(3);

    auto *hSplit = new QSplitter(Qt::Horizontal, this);
    hSplit->addWidget(vSplit);
    hSplit->addWidget(m_logShellSplit);
    hSplit->setStretchFactor(0, 1);
    hSplit->setStretchFactor(1, 1);
    hSplit->setHandleWidth(3);

    QSettings cfg;
    hSplit->restoreState(cfg.value("window/hSplit").toByteArray());
    vSplit->restoreState(cfg.value("window/vSplit").toByteArray());
    // m_w4 always starts collapsed — do NOT restore logShellSplit from settings
    // here; it would reopen the terminal on a cold start.  The state is saved
    // only when the user manually resizes during an active shell session.
    m_logShellSplit->setSizes({2, 1, 0});

    {
        m_splitterSaveTimer = new QTimer(this);
        m_splitterSaveTimer->setSingleShot(true);
        connect(m_splitterSaveTimer, &QTimer::timeout, this, [hSplit, vSplit, this] {
            QSettings s;
            s.setValue("window/hSplit", hSplit->saveState());
            s.setValue("window/vSplit", vSplit->saveState());
            if (m_terminalMode)
                s.setValue("window/logShellSplit", m_logShellSplit->saveState());
        });
        connect(hSplit, &QSplitter::splitterMoved, this, [this] {
            m_splitterSaveTimer->start(300);
        });
    }

    return hSplit;
}

QFrame *MainWindow::buildStatusBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName("statusBar");
    bar->setFrameShape(QFrame::NoFrame);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_statusText  = new QLabel("", bar);
    m_statusText->setObjectName("statusText");

    m_statusRight = new QLabel("", bar);
    m_statusRight->setObjectName("statusRight");
    m_statusRight->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    lay->addWidget(m_statusText,  1);
    lay->addWidget(m_statusRight, 0);
    return bar;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tab helpers
// ─────────────────────────────────────────────────────────────────────────────
ScriptViewer *MainWindow::addTab(const QString &filePath)
{
    auto *viewer = new ScriptViewer(m_tabWidget);
    viewer->setEditorFont(buildEditorFont(m_fontSize));

    const QString tabLabel = filePath.isEmpty()
                             ? "untitled"
                             : QFileInfo(filePath).fileName();
    const int idx = m_tabWidget->addTab(viewer, tabLabel);
    m_tabWidget->setTabToolTip(idx, filePath.isEmpty() ? "(empty)" : filePath);

    // Update tab title dot whenever this viewer's modified state changes
    connect(viewer, &ScriptViewer::modificationChanged, this, [this, viewer](bool) {
        updateTabModifiedState(viewer);
    });

    // Load comm script when user clicks a PLUGIN.SCRIPT line
    connect(viewer, &ScriptViewer::commScriptRequested,
            this,   &MainWindow::onCommScriptRequested);

    // Open included file in a new main-script tab when user clicks INCLUDE "..."
    connect(viewer, &ScriptViewer::includeFileRequested,
            this,   &MainWindow::onIncludeFileRequested);

    if (!filePath.isEmpty())
        viewer->loadScript(filePath);

    m_tabWidget->setCurrentIndex(idx);
    // Set initial colour — light blue-gray = clean, will turn red if modified
    m_tabWidget->tabBar()->setTabTextColor(idx, QColor("#c8d0e0"));
    return viewer;
}

ScriptViewer *MainWindow::currentViewer() const
{
    return qobject_cast<ScriptViewer *>(m_tabWidget->currentWidget());
}

ScriptViewer *MainWindow::runningViewer() const
{
    if (m_runningTab < 0 || m_runningTab >= m_tabWidget->count())
        return nullptr;
    return qobject_cast<ScriptViewer *>(m_tabWidget->widget(m_runningTab));
}

void MainWindow::loadIntoTab(int index, const QString &filePath)
{
    if (index < 0 || index >= m_tabWidget->count()) return;
    auto *viewer = qobject_cast<ScriptViewer *>(m_tabWidget->widget(index));
    if (!viewer) return;

    viewer->loadScript(filePath);
    const QString name = QFileInfo(filePath).fileName();
    m_tabWidget->setTabText(index, name);
    m_tabWidget->setTabToolTip(index, filePath);

    QSettings cfg;
    // Persist all tab paths
    QStringList paths;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) paths << v->currentFile();
    }
    cfg.setValue("session/tabPaths",  paths);
    cfg.setValue("session/activeTab", m_tabWidget->currentIndex());

    m_w3->appendStatus(QString("Loaded: %1").arg(name));
    setStatus(QString("Script loaded: %1").arg(name));
}

void MainWindow::loadIntoCurrentTab(const QString &filePath)
{
    // Reuse current tab if it's empty, otherwise open a new one
    const int cur = m_tabWidget->currentIndex();
    auto *viewer  = currentViewer();
    if (viewer && viewer->currentFile().isEmpty())
        loadIntoTab(cur, filePath);
    else
        addTab(filePath);

    syncPathEdit(m_tabWidget->currentIndex());
}

void MainWindow::syncPathEdit(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= m_tabWidget->count()) return;
    auto *viewer = qobject_cast<ScriptViewer *>(m_tabWidget->widget(tabIndex));
    if (!viewer) return;

    // Only update the script field when the active tab holds a script.
    // When an .ini tab is active the script field must keep the last script
    // path so clicking it still navigates back to the correct script tab.
    if (!viewer->isIniFile()) {
        m_scriptPathEdit->setText(viewer->currentFile());
        m_w3->setScriptPath(viewer->currentFile());
    }

    // Auto-fill the INI field from the script directory only for script tabs.
    if (!viewer->currentFile().isEmpty() && !viewer->isIniFile()) {
        const QString scriptDir  = QFileInfo(viewer->currentFile()).absolutePath();
        const QString defaultIni = scriptDir + "/uscript.ini";
        const bool isEmpty       = m_iniPathEdit->text().trimmed().isEmpty();
        const bool isDefault     = QFileInfo(m_iniPathEdit->text()).fileName()
                                       .compare("uscript.ini", Qt::CaseInsensitive) == 0;
        if (isEmpty || isDefault)
            m_iniPathEdit->setText(defaultIni);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tab slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onTabCloseRequested(int index)
{
    if (m_tabWidget->count() <= 1) return;   // always keep at least one tab

    // Check for unsaved changes
    auto *viewer = qobject_cast<ScriptViewer *>(m_tabWidget->widget(index));
    if (viewer && viewer->isModified()) {
        QString name = m_tabWidget->tabText(index);
        if (name.startsWith("● ")) name = name.mid(2); // strip "● " only if present

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Unsaved changes");
        auto msg = QString("'%1' has unsaved changes.\nSave before closing?").arg(name);
        msgBox.setText(msg);
        msgBox.setStandardButtons(QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.button(QMessageBox::Discard)->setText("Discard");  // override platform text
        const auto ans = msgBox.exec();

        if (ans == QMessageBox::Cancel) return;
        if (ans == QMessageBox::SaveAll && !viewer->save()) return;
    }

    if (index == m_runningTab) {
        const auto ans = QMessageBox::question(
            this, "Script running",
            "This tab's script is currently running.\nClose the tab anyway?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) return;
        m_stoppingByUser = true;   // so onProcessFinished reports "stopped by user"
        m_process->kill();
        // onProcessFinished will fire asynchronously and clean up m_runningTab
    }

    m_tabWidget->removeTab(index);

    // Adjust running tab index if needed
    if (m_runningTab > index) --m_runningTab;

    syncPathEdit(m_tabWidget->currentIndex());
}

void MainWindow::onCurrentTabChanged(int index)
{
    syncPathEdit(index);

    // Highlight the running tab label so the user can see which one is active
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        const bool mod = v && v->isModified();
        QColor c;
        if      (mod)             c = QColor("#ff5555");  // red   = modified
        else if (i==m_runningTab) c = QColor("#4a9eff");  // blue  = running
        else                      c = QColor("#c8d0e0");  // light blue-gray = clean
        m_tabWidget->tabBar()->setTabTextColor(i, c);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Toolbar actions
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onBrowse()
{
    auto *viewer = currentViewer();
    const QString start = viewer && !viewer->currentFile().isEmpty()
                          ? QFileInfo(viewer->currentFile()).absolutePath()
                          : QDir::homePath();

    const QString f = QFileDialog::getOpenFileName(
        this, "Select Script",
        start,
        "Script files (*.txt *.scr *.script);;All files (*)");

    if (!f.isEmpty())
        loadIntoCurrentTab(f);
}

void MainWindow::onStartStop()
{
    if (m_running) {
        terminateProcess();
        return;
    }

    auto *viewer = currentViewer();
    if (!viewer || viewer->currentFile().isEmpty()) {
        m_w3->appendStatus("No script loaded in the active tab.");
        return;
    }
    const QString scriptPath = viewer->currentFile();
    if (!QFileInfo::exists(scriptPath)) {
        m_w3->appendStatus(QString("Script not found: %1").arg(scriptPath));
        return;
    }
    if (viewer->isIniFile()) {
        m_w3->appendStatus(QString("'%1' is a configuration file — use the editor to view/edit it, not Run.")
                           .arg(QFileInfo(scriptPath).fileName()));
        return;
    }

    // ── Unsaved-changes guard ────────────────────────────────────────────────────────
    // Collect every modified editor: all core-script tabs + the comm editor.
    // (The ini path field is a plain QLineEdit and is always in sync with
    //  m_iniPath, so no separate check is needed for it. If the ini file is
    //  open in a tab, it will be caught by the tab loop below.)
    {
        bool anyModified = false;

        // Core script tabs
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
            if (v && v->isModified()) { anyModified = true; break; }
        }

        // Comm-script editor
        if (!anyModified && m_w2 && m_w2->isModified())
            anyModified = true;

        if (anyModified) {
            QMessageBox dlg(this);
            dlg.setWindowTitle("Unsaved changes");
            dlg.setText("One or more open files have unsaved changes.\n\n"
                        "Save all before running, discard the changes, or cancel the run?");
            dlg.setIcon(QMessageBox::Question);

            auto *saveBtn    = dlg.addButton("Save All",   QMessageBox::AcceptRole);
            auto *discardBtn = dlg.addButton("Discard",    QMessageBox::DestructiveRole);
            /*cancelBtn*/     dlg.addButton("Cancel Run",  QMessageBox::RejectRole);
            dlg.setDefaultButton(saveBtn);
            dlg.exec();

            const auto *clicked = dlg.clickedButton();

            // Any button other than Save or Discard (including window-close) → abort
            if (clicked != saveBtn && clicked != discardBtn)
                return;

            if (clicked == saveBtn) {
                // Save all modified tab scripts
                for (int i = 0; i < m_tabWidget->count(); ++i) {
                    auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
                    if (v && v->isModified()) {
                        if (!v->save()) {
                            m_w3->appendStatus(
                                QString("Save failed for tab %1 — run cancelled.").arg(i + 1));
                            return;   // save failed; do not run
                        }
                        updateTabModifiedState(v);
                    }
                }
                // Save comm script if modified
                if (m_w2 && m_w2->isModified()) {
                    if (!m_w2->save()) {
                        m_w3->appendStatus("Save failed for comm script — run cancelled.");
                        return;
                    }
                }
            }
            // "Discard": in-memory edits are kept but the interpreter reads
            // the on-disk version — no extra action required here.
        }
    }
    // ── End unsaved-changes guard ─────────────────────────────────────────────────────

    QString interp = m_interpreterPath.trimmed();
    if (!interp.isEmpty()) {
        const QStringList parts = QProcess::splitCommand(interp);
        if (!parts.isEmpty()) interp = parts.first();
    }

    // Nothing configured, or the configured path is stale (deploy folder
    // moved, etc.) — first try "uscript" sitting next to the GUI binary
    // (same directory as applicationDirPath()); if that's not there either,
    // fall back to a file-picker dialog rooted at that same directory so the
    // user can locate it manually instead of the run simply failing.
    if (interp.isEmpty() || !QFileInfo::exists(interp)) {
        QString appDirCandidate = QDir(QCoreApplication::applicationDirPath()).filePath("uscript");
#ifdef Q_OS_WIN
        appDirCandidate += ".exe";
#endif
        if (QFileInfo::exists(appDirCandidate)) {
            interp = appDirCandidate;
        } else {
            const QString picked = QFileDialog::getOpenFileName(
                this, "Locate uscript Interpreter Binary",
                QCoreApplication::applicationDirPath());
            if (!picked.isEmpty()) interp = picked;
        }

        if (!QFileInfo::exists(interp)) {
            m_w3->appendStatus(QString("Interpreter not found: %1").arg(interp));
            return;
        }

        // Remember the resolved/picked path so future runs don't need to
        // search again, and reflect it in the toolbar field.
        m_interpreterPath = interp;
        if (m_interpEdit) m_interpEdit->setText(interp);
        QSettings().setValue("session/interpreterPath", interp);
    }

    m_runningTab = m_tabWidget->currentIndex();

    // Refresh all tab colours
    onCurrentTabChanged(m_tabWidget->currentIndex());

    m_w2->clear();
    m_w3->clear();
    if (m_wCommDump) m_wCommDump->clear();
    m_lineBuf.clear();
    m_errBuf.clear();   // flush stale stderr from any previous run

    // Clear any validation-error markers from the previous run
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) v->clearErrorLines();
    }
    m_w2->clearErrorLines();
    m_resetBtn->setEnabled(false);

    m_w3->appendStatus(QString("Starting: %1 -s %2")
                       .arg(QFileInfo(interp).fileName(),
                            QFileInfo(scriptPath).fileName()));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SCRIPT_GUI_MODE", "1");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString libDir = QDir(appDir).filePath("lib");
    {
        const QString existing = env.value("LD_LIBRARY_PATH");
        env.insert("LD_LIBRARY_PATH",
                   existing.isEmpty() ? libDir : libDir + ":" + existing);
    }
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());

    // Build argument list: always -s <script>, optionally -c <ini>
    QStringList args;
    const QString iniPath = m_iniPath.trimmed();
    if (!iniPath.isEmpty())
        args << "-c" << iniPath;
    args << "-s" << scriptPath;

    // Silently append -l <N> when the user selected a specific log severity in
    // the Output Log header (DEFAULT → omit the flag entirely).
    const int logLevelArg = m_w3->logLevelArg();
    if (logLevelArg >= 0)
        args << "-l" << QString::number(logLevelArg);

    m_process->start(interp, args);
}

// ─────────────────────────────────────────────────────────────────────────────
//  QProcess slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onProcessStarted()
{
    setRunning(true);
    setStatus("Running…");
    m_w3->setRunning(true);
    m_w3->appendStatus("Interpreter started");

    // Lock all editors read-only for the duration of the run
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) v->setReadOnly(true);
    }
    m_w2->setReadOnly(true);
}

void MainWindow::onProcessOutput()
{
    const QByteArray newBytes = m_process->readAllStandardOutput();

    if (m_terminalMode) {
        // ── terminal mode ─────────────────────────────────────────────────
        // Buffer all incoming bytes on \n boundaries.  For each complete line:
        //   • lines starting with "GUI:" are dispatched as protocol events
        //     and are NOT forwarded to the terminal widget — they must never
        //     appear in the shell display.
        //   • all other lines (raw shell output, prompts, ANSI sequences) are
        //     collected and forwarded to ShellTerminal in one batch so that
        //     \r-based prompt rewrites and character echo remain correct.
        m_lineBuf += newBytes;
        QByteArray terminalBytes;   // non-GUI bytes to forward to w4
        int nlPos;
        while ((nlPos = m_lineBuf.indexOf('\n')) != -1) {
            const QByteArray rawLine = m_lineBuf.left(nlPos + 1);  // keep \n
            m_lineBuf.remove(0, nlPos + 1);
            const QString line = QString::fromUtf8(rawLine).trimmed();
            if (line.isEmpty()) {
                // Blank line produced by the leading '\n' in gui_notify_*
                // calls — discard rather than forwarding to the terminal.
                continue;
            }
            if (line.startsWith(QLatin1StringView("GUI:"))) {
                // Flush any buffered terminal bytes before dispatching so
                // the terminal display stays in sync with protocol events.
                if (!terminalBytes.isEmpty()) {
                    m_w4->processRawBytes(terminalBytes);
                    terminalBytes.clear();
                }
                dispatchLine(line);
            } else {
                terminalBytes += rawLine;
            }
        }
        // Forward remaining non-GUI bytes (incomplete last line / prompts).
        if (!terminalBytes.isEmpty())
            m_w4->processRawBytes(terminalBytes);
        // Also forward any partial (no-\n) tail so prompt characters appear
        // in real time without waiting for the next newline.
        if (!m_lineBuf.isEmpty()) {
            m_w4->processRawBytes(m_lineBuf);
            m_lineBuf.clear();
        }
    } else {
        // ── normal mode ───────────────────────────────────────────────────
        // Process line-by-line. If a line causes a mode switch to terminal
        // (GUI:SHELL_RUN), any bytes that arrived in the same chunk after
        // that \n must be forwarded to the terminal rather than parsed as
        // protocol lines — otherwise the first prompt is silently swallowed.
        m_lineBuf += newBytes;
        int nlPos;
        while ((nlPos = m_lineBuf.indexOf('\n')) != -1) {
            const QString line = QString::fromUtf8(m_lineBuf.left(nlPos)).trimmed();
            m_lineBuf.remove(0, nlPos + 1);
            if (!line.isEmpty())
                dispatchLine(line);   // may set m_terminalMode = true

            if (m_terminalMode) {
                // Mode just switched — flush remaining buffered bytes directly
                // to the terminal and switch to terminal-mode loop.
                if (!m_lineBuf.isEmpty()) {
                    m_w4->processRawBytes(m_lineBuf);
                    m_lineBuf.clear();
                }
                break;
            }
        }
    }
}

void MainWindow::onProcessError()
{
    m_errBuf += m_process->readAllStandardError();
    int nlPos;
    while ((nlPos = m_errBuf.indexOf('\n')) != -1) {
        const QString line = QString::fromUtf8(m_errBuf.left(nlPos)).trimmed();
        m_errBuf.remove(0, nlPos + 1);
        if (!line.isEmpty())
            m_w3->appendLine(line);
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // Flush any remaining buffered stdout (last line without trailing '\n')
    if (!m_lineBuf.isEmpty()) {
        dispatchLine(QString::fromUtf8(m_lineBuf).trimmed());
        m_lineBuf.clear();
    }
    // Flush any remaining buffered stderr (last partial line would otherwise be lost)
    if (!m_errBuf.isEmpty()) {
        const QString lastErr = QString::fromUtf8(m_errBuf).trimmed();
        m_errBuf.clear();
        if (!lastErr.isEmpty())
            m_w3->appendLine(lastErr);
    }
    // If the process was killed/crashed while the shell was active, the
    // GUI:SHELL_EXIT message was never sent.  Reset terminal mode here so the
    // next run starts clean and its stdout is parsed as protocol lines.
    if (m_terminalMode) {
        m_terminalMode = false;
        m_w4->setActive(false);
        // Give the shell's space back to the log panel; leave the comm-dump
        // panel's current size untouched.
        QList<int> sizes = m_logShellSplit->sizes();
        if (sizes.size() == 3) {
            sizes[0] += sizes[2];
            sizes[2] = 0;
            m_logShellSplit->setSizes(sizes);
        }
    }
    m_threadedCommScripts.clear();   // reset: any threads still alive at crash/stop are gone
    setRunning(false);
    // Keep w2 loaded when it carries error markers so the red bar on the
    // failing comm-script line stays visible after the run ends.
    // In all other cases (success, or error in main script only) clear normally.
    // Cancel any pending deferred highlight (QTimer::singleShot issued during
    // the last EXEC_COMM batch) so it cannot re-apply a stale bar after the run.
    m_pendingCommHighlight = false;
    if (!m_w2->hasErrorLines()) {
        m_w2->clear();
    } else {
        // Document is kept for the error markers, but the execution-progress
        // highlight bar must be removed so only the red error bar remains.
        m_w2->clearHighlight();
    }

    const int  savedRunningTab = m_runningTab;
    m_runningTab = -1;
    onCurrentTabChanged(m_tabWidget->currentIndex());   // reset tab colour

    // Restore editors to read-write and clear execution highlights
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) { v->setReadOnly(false); v->setCurrentLine(0); v->clearThreadLines(); }
    }
    m_w2->setReadOnly(false);
    m_w2->setCurrentLine(0);
    m_w3->setRunning(false);    // re-enable the log-level combo

    const bool userStopped = m_stoppingByUser;
    m_stoppingByUser = false;

    const QString reason = userStopped
                           ? "stopped by user"
                           : (status == QProcess::CrashExit)
                             ? "interpreter crashed"
                             : QString("exit code %1").arg(exitCode);
    m_w3->appendStatus(QString("Interpreter finished — %1").arg(reason));
    setStatus(QString("Finished (%1)").arg(reason));

    if (userStopped) {
        m_led->setState(StatusLed::State::Idle);
        m_ledLabel->setText("IDLE");
        if (savedRunningTab >= 0 && savedRunningTab < m_tabWidget->count())
            m_tabWidget->tabBar()->setTabTextColor(savedRunningTab, QColor("#c8d0e0"));
    } else if (exitCode != 0 || status == QProcess::CrashExit) {
        m_led->setState(StatusLed::State::Error);
        m_ledLabel->setText("ERROR");
        // Tint the finished tab red briefly
        if (savedRunningTab >= 0 && savedRunningTab < m_tabWidget->count())
            m_tabWidget->tabBar()->setTabTextColor(savedRunningTab, QColor("#ff5555"));  // error — red
    } else {
        m_led->setState(StatusLed::State::Ready);
        m_ledLabel->setText("DONE");
        if (savedRunningTab >= 0 && savedRunningTab < m_tabWidget->count())
            m_tabWidget->tabBar()->setTabTextColor(savedRunningTab, QColor("#c8d0e0"));  // done — light blue-gray
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Protocol dispatch
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::dispatchLine(const QString &raw)
{
    if (!raw.startsWith("GUI:")) {
        m_w3->appendLine(raw);
        return;
    }

    const QStringView payload(raw.begin() + 4, raw.end());

    if (payload.startsWith(QLatin1StringView("EXEC_MAIN:"))) {
        const int lineNo = payload.mid(10).toInt();
        auto *v = runningViewer();
        if (!v) return;

        // Always advance the main-script bar unconditionally.
        // The w1 and w2 bars are independent: w1 stays pinned on the
        // SCRIPT command being executed while w2 tracks the individual
        // comm-script lines via EXEC_COMM messages.  The old ExecContext
        // filtering was wrong: it compared main-script line numbers against
        // the comm-script line count and mis-routed w1 updates into w2.
        v->setCurrentLine(lineNo);
        // Only auto-load the comm script when this main-script line is NOT
        // a threaded (&) invocation — threaded comm scripts are suppressed.
        if (threadedCommScriptForLine(v, lineNo).isEmpty())
            autoLoadCommScriptForLine(v, lineNo);
        setStatus(QString("Main script — line %1").arg(lineNo));
    }
    else if (payload.startsWith(QLatin1StringView("EXEC_COMM:"))) {
        // Comm-script line notification from the interpreter.
        // Suppressed when the currently-loaded comm file belongs to a threaded
        // (&) invocation — the viewer is reserved for non-threaded execution.
        // Guard: the document must be loaded and have real content.
        // autoLoadCommScriptForLine() pre-loads the file on EXEC_MAIN so
        // the document is ready before the first EXEC_COMM arrives.
        const int lineNo = payload.mid(10).toInt();
        if (m_w2->currentFile().isEmpty() || m_w2->lineCount() == 0) return;
        if (isThreadedCommFile(m_w2->currentFile())) return;
        if (m_pendingCommHighlight) {
            // loadScript() was called in this same onProcessOutput() batch.
            // QSyntaxHighlighter defers its rehighlight via a queued connection,
            // so it hasn't run yet.  If we call setCurrentLine() now, the queued
            // rehighlight will fire later and wipe the execution band.
            // Defer setCurrentLine() via a zero-delay timer: it fires after the
            // current event returns, by which time the queued rehighlight has
            // already executed and the document is fully highlighted.
            m_pendingCommHighlight = false;
            auto *w2 = m_w2;
            QTimer::singleShot(0, this, [this, w2, lineNo]() {
                // Only apply the deferred highlight if the script is still running;
                // execution may have ended while the timer was pending.
                if (m_running)
                    w2->setCurrentLine(lineNo);
            });
        } else {
            m_w2->setCurrentLine(lineNo);
        }
        setStatus(QString("Comm script — line %1").arg(lineNo));
    }
    else if (payload.startsWith(QLatin1StringView("ERROR_MAIN:"))) {
        // Validation-phase error: highlight the failing line in w1 (red bar).
        const int lineNo = payload.mid(11).toInt();
        auto *v = runningViewer();
        if (!v) return;
        v->setErrorLine(lineNo);
    }
    else if (payload.startsWith(QLatin1StringView("ERROR_COMM:"))) {
        // Validation-phase error: highlight the failing line in w2 (red bar).
        // Suppressed when the currently-loaded comm file is threaded.
        const int lineNo = payload.mid(11).toInt();
        if (m_w2->currentFile().isEmpty()) return;
        if (isThreadedCommFile(m_w2->currentFile())) return;
        m_w2->setErrorLine(lineNo);
    }
    else if (payload.startsWith(QLatin1StringView("LOAD_COMM_T:"))) {
        // GUI:LOAD_COMM_T:<tid>:<path> — open/target the comm tab for thread <tid>.
        const QStringView rest = payload.mid(12);
        const int sep = rest.indexOf(QChar(':'));
        if (sep < 0) return;
        const int tid = rest.left(sep).toInt();
        const QString rawPath = rest.mid(sep + 1).toString();
        loadCommTabForThread(tid, rawPath);
    }
    else if (payload.startsWith(QLatin1StringView("EXEC_COMM_T:"))) {
        // GUI:EXEC_COMM_T:<tid>:<lineNo> — highlight <lineNo> in thread <tid>'s tab.
        const QStringView rest = payload.mid(12);
        const int sep = rest.indexOf(QChar(':'));
        if (sep < 0) return;
        const int tid    = rest.left(sep).toInt();
        const int lineNo = rest.mid(sep + 1).toInt();
        const auto it = m_commThreadTabs.constFind(tid);
        if (it != m_commThreadTabs.constEnd())
            it->viewer->setCurrentLine(lineNo);
    }
    else if (payload.startsWith(QLatin1StringView("CLEAR_COMM_T:"))) {
        // GUI:CLEAR_COMM_T:<tid> — thread finished: drop the "●" live marker.
        // The tab itself stays open (closed only via its × button) so the
        // final execution state remains available for inspection.
        const int tid = payload.mid(13).toInt();
        markCommTabFinished(tid);
    }
    else if (payload.startsWith(QLatin1StringView("LOAD_COMM:"))) {
        // Resolve the interpreter-relative path to an absolute path using
        // the running script's directory as base, so the GUI can open it
        // regardless of the GUI process's own working directory.
        const QString rawPath = payload.mid(10).toString();
        const QString resolved = resolveCommScriptPath(rawPath);
        const QString loadPath = (!resolved.isEmpty() && QFileInfo::exists(resolved))
                                 ? resolved : rawPath;

        // Skip reload when the file is already showing in w2 — reloading
        // calls clearHighlight() which would wipe the bar set by
        // autoLoadCommScriptForLine() for the in-flight EXEC_COMM sequence.
        // Use QFileInfo::canonicalFilePath() on both sides so that paths
        // resolved by autoLoadCommScriptForLine() (absolute) and here
        // (also absolute, but potentially via a different symlink chain)
        // always compare equal when they point to the same file.  A simple
        // string comparison can fail when one path contains a trailing slash,
        // a "." component, or was resolved through a different symlink, causing
        // a spurious reload that calls clearHighlight() and wipes the tracing bar
        // before the first EXEC_COMM line is processed.
        const QString currentCanon = QFileInfo(m_w2->currentFile()).canonicalFilePath();
        const QString loadCanon    = QFileInfo(loadPath).canonicalFilePath();
        if (currentCanon != loadCanon || currentCanon.isEmpty()) {
            // Only display the comm script when it is not running in a thread.
            if (!isThreadedCommFile(loadPath)) {
                m_w2->loadScript(loadPath);
                // Do NOT call processEvents() here — it re-enters onProcessOutput()
                // via the still-pending readyReadStandardOutput signal, corrupting
                // m_lineBuf mid-loop (the root cause of GUI: tokens being printed
                // verbatim when the comm script completes in < ~10 ms).
                // The highlighter race is handled in the EXEC_COMM branch instead.
                m_pendingCommHighlight = true;
                m_w3->appendStatus(QString("Comm script: %1").arg(QFileInfo(loadPath).fileName()));
            }
        }
    }
    else if (payload.startsWith(QLatin1StringView("CLEAR_COMM"))) {
        // Only clear the viewer when the currently-displayed file is not threaded.
        if (!isThreadedCommFile(m_w2->currentFile()))
            m_w2->clear();
    }
    else if (payload.startsWith(QLatin1StringView("THREAD_START:"))) {
        // A & command launched a background thread: record the comm script
        // that this threaded line invokes so we can suppress viewer updates
        // for exactly that file while letting non-threaded comm scripts through.
        const int lineNo = payload.mid(13).toInt();
        auto *v = runningViewer();
        if (v) {
            v->addThreadLine(lineNo);
            // Resolve the comm-script path for this threaded line (if any)
            // and add it to the suppression set.
            const QString canon = threadedCommScriptForLine(v, lineNo);
            if (!canon.isEmpty())
                m_threadedCommScripts.insert(canon);
        }
    }
    else if (payload.startsWith(QLatin1StringView("THREAD_DONE:"))) {
        // The background thread for that line has finished: remove its comm
        // script from the suppression set and clear the viewer if it was
        // showing that file (leave it empty — the running script is threaded).
        const int lineNo = payload.mid(12).toInt();
        auto *v = runningViewer();
        if (v) {
            const QString canon = threadedCommScriptForLine(v, lineNo);
            if (!canon.isEmpty()) {
                m_threadedCommScripts.remove(canon);
                // If the viewer was showing this threaded file, clear it now
                // so no stale content remains after the thread exits.
                if (QFileInfo(m_w2->currentFile()).canonicalFilePath() == canon)
                    m_w2->clear();
            }
            v->removeThreadLine(lineNo);
        }
    }
    else if (payload.startsWith(QLatin1StringView("SHELL_RUN"))) {
        // ── Enter terminal mode ────────────────────────────────────────────
        // Clear first, THEN activate. Any bytes that arrived in the same
        // readyRead chunk as GUI:SHELL_RUN are forwarded by onProcessOutput
        // AFTER this dispatch returns, so they land on a clean terminal.
        m_w4->clear();
        m_terminalMode = true;
        m_w4->setActive(true);
        const int total  = m_logShellSplit->height();
        const int shellH = qMax(total * 40 / 100, 120);
        // Keep the comm-dump panel's current size, shrink the log panel to
        // make room for the terminal.
        const int dumpH = m_logShellSplit->sizes().value(1, 0);
        const int logH  = qMax(total - shellH - dumpH, 60);
        m_logShellSplit->setSizes({ logH, dumpH, shellH });
        m_w3->appendStatus("─── Shell started ───────────────────────────────");
    }
    else if (payload.startsWith(QLatin1StringView("SHELL_EXIT"))) {
        // ── Leave terminal mode ────────────────────────────────────────────
        // Triggered by the plugin's exit sequence (e.g. "exit" command inside
        // the uShell). The plugin writes GUI:SHELL_EXIT then blocks waiting for
        // SHELL_DONE on its stdin before it continues the main script.
        m_terminalMode = false;
        m_w4->setActive(false);

        // Collapse the terminal panel — user can still scroll its output.
        // Give its space back to the log panel; leave comm-dump untouched.
        QList<int> sizes = m_logShellSplit->sizes();
        if (sizes.size() == 3) {
            sizes[0] += sizes[2];
            sizes[2] = 0;
            m_logShellSplit->setSizes(sizes);
        }

        // Unblock the interpreter so the main script can continue.
        if (m_process->state() == QProcess::Running)
            m_process->write("SHELL_DONE\n");

        m_w3->appendStatus("─── Shell exited — main script resumed ──────────");
    }
    else if (payload.startsWith(QLatin1StringView("COMM_DUMP:"))) {
        dispatchCommDump(payload.mid(10).toString());
    }
    else if (payload.startsWith(QLatin1StringView("LOG:"))) {
        // A GUI:LOG: line may contain an embedded GUI:EXEC_MAIN: or
        // GUI:EXEC_COMM: token at the end when the interpreter's stdout
        // pipe delivers two adjacent printf calls in a single read() chunk
        // without the separating newline being visible to the splitter.
        // Detect and re-dispatch any trailing embedded token.
        QString logText = payload.mid(4).toString();
        static const QRegularExpression embeddedRe(
            R"((GUI:EXEC_(?:MAIN|COMM):\d+|GUI:LOAD_COMM:\S+|GUI:CLEAR_COMM)$)"
        );
        const QRegularExpressionMatch em = embeddedRe.match(logText);
        if (em.hasMatch()) {
            // Strip the embedded token from the log text
            logText = logText.left(em.capturedStart()).trimmed();
            // Re-dispatch the embedded token as if it were a top-level line
            dispatchLine(em.captured(1));
        }
        if (!logText.isEmpty())
            m_w3->appendLine(logText);
    }
    else {
        m_w3->appendLine(raw);
    }
}

// ── helpers ──────────────────────────────────────────────────────────────────
// Reads a fixed-size, NUL-padded char buffer (as written by commdump_details()
// in ICommDumpProtocol.hpp) into a QString, stopping at the first NUL rather
// than assuming it's fully populated.
static QString fixedCStr(const char *buf, size_t maxLen)
{
    const void *nul = std::memchr(buf, '\0', maxLen);
    const size_t len = nul ? (static_cast<const char *>(nul) - buf) : maxLen;
    return QString::fromUtf8(buf, static_cast<int>(len));
}

// ─────────────────────────────────────────────────────────────────────────────
//  dispatchCommDump — decode one GUI:COMM_DUMP:<base64> payload and append it
//  as a row in the comm-dump panel. Wire format is documented at the top of
//  ICommDumpProtocol.hpp:
//
//    [1] nameLen  [nameLen] pluginName  [1] family  [k_labelSize] label
//    [1] dir      [4] dataLen (LE)      [dataLen] data
//
//  The label is already the exact display text the driver rendered via
//  describeConnection() — no per-family formatting needed here, unlike the
//  earlier union-based wire format. `family` is decoded but currently only
//  informational (a hook for a future per-family icon/colour); the Details
//  column shows the label verbatim.
//
//  Malformed/truncated payloads (should not happen — the interpreter is the
//  only writer — but stdout parsing is never fully trustworthy) are dropped
//  silently rather than crashing the GUI.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::dispatchCommDump(const QString &base64Payload)
{
    const QByteArray raw = QByteArray::fromBase64(base64Payload.toLatin1());

    int pos = 0;
    if (raw.size() < pos + 1) return;
    const int nameLen = static_cast<unsigned char>(raw[pos]); ++pos;

    if (raw.size() < pos + nameLen) return;
    const QString plugin = QString::fromUtf8(raw.constData() + pos, nameLen);
    pos += nameLen;

    if (raw.size() < pos + 1) return;
    ++pos;   // family byte — decoded but not yet used for display (see comment above)

    if (raw.size() < pos + k_labelSize) return;
    const QString details = fixedCStr(raw.constData() + pos, k_labelSize);
    pos += k_labelSize;

    if (raw.size() < pos + 1) return;
    const bool isTx = static_cast<CommDir>(static_cast<unsigned char>(raw[pos])) == CommDir::Tx;
    ++pos;

    if (raw.size() < pos + 4) return;
    uint32_t dataLen = 0;
    for (int i = 0; i < 4; ++i)
        dataLen |= static_cast<uint32_t>(static_cast<unsigned char>(raw[pos + i])) << (8 * i);
    pos += 4;

    if (dataLen > static_cast<uint32_t>(raw.size() - pos)) return;
    const QByteArray data(raw.constData() + pos, static_cast<int>(dataLen));

    if (m_wCommDump)
        m_wCommDump->addRecord(plugin, details, isTx, data);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Auto-load comm script during execution
//
//  Called every time EXEC_MAIN:N arrives.  If line N in the running main script
//  is a PLUGIN.SCRIPT or PLUGIN.COMMAND script invocation, the referenced comm
//  script is resolved relative to the main script's directory and loaded into
//  m_w2 — provided it isn't already the file currently displayed there.
//
//  After this call the main-script bar stays fixed on line N while the
//  interpreter sends EXEC_COMM:M messages that advance the bar inside m_w2.
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::autoLoadCommScriptForLine(ScriptViewer *viewer, int lineNo)
{
    const QString line = viewer->lineText(lineNo);
    if (line.isEmpty()) return false;

    // Same patterns as CodeEditor::checkCurrentLineForCommScript()
    static const QRegularExpression scriptCmd(
        R"(\b[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.SCRIPT\s+(\S+))"        // PLUGIN[:N].SCRIPT <file>
    );
    static const QRegularExpression scriptArg(
        R"(\b[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.([A-Z][A-Z0-9_]*)\s+script\s+(\S+))"  // PLUGIN[:N].CMD script <file>
    );

    QRegularExpressionMatch m = scriptCmd.match(line);
    if (!m.hasMatch()) m = scriptArg.match(line);
    if (!m.hasMatch()) return false;

    // scriptCmd: group 1 = filename
    // scriptArg: group 1 = command name, group 2 = filename
    const QString scriptName = m.captured(m.regularExpression() == scriptCmd ? 1 : 2);
    const QString baseDir = !viewer->currentFile().isEmpty()
                            ? QFileInfo(viewer->currentFile()).absolutePath()
                            : QDir::currentPath();
    const QString resolved = QDir(baseDir).filePath(scriptName);

    if (!QFileInfo::exists(resolved)) return false;

    if (QFileInfo(m_w2->currentFile()).canonicalFilePath() !=
        QFileInfo(resolved).canonicalFilePath()) {
        m_w2->loadScript(resolved);
        // Do NOT call processEvents() here — it re-enters onProcessOutput() via
        // the still-pending readyReadStandardOutput signal, corrupting m_lineBuf
        // mid-loop.  This is the root cause of GUI: tokens being printed verbatim
        // when the comm script has no delay and all output arrives in one chunk.
        // Set the flag so the EXEC_COMM handler knows to defer setCurrentLine().
        m_pendingCommHighlight = true;
        m_w3->appendStatus(QString("Comm script: %1").arg(QFileInfo(resolved).fileName()));
    }
    return true;   // this line calls a comm sub-script (already loaded or just loaded)
}

// ─────────────────────────────────────────────────────────────────────────────
//  Resolve a comm-script path received in GUI:LOAD_COMM:<path> to an
//  absolute path usable by the GUI process.
//
//  The interpreter emits the path exactly as it was given in the main script
//  (e.g. "comm/my_script.txt").  That path is relative to the interpreter's
//  working directory, which is the directory containing the main script file.
//  The GUI resolves it the same way autoLoadCommScriptForLine() does, using
//  the running tab's script directory as the base.
//
//  Returns the resolved absolute path, or an empty string if it cannot be
//  determined (no running tab, no script loaded in that tab).
// ─────────────────────────────────────────────────────────────────────────────
QString MainWindow::resolveCommScriptPath(const QString &rawPath) const
{
    // If the path is already absolute, return it unchanged.
    if (QFileInfo(rawPath).isAbsolute())
        return rawPath;

    // Use the running tab's script directory as the base.
    auto *viewer = runningViewer();
    if (!viewer || viewer->currentFile().isEmpty())
        return {};

    const QString baseDir = QFileInfo(viewer->currentFile()).absolutePath();
    return QDir(baseDir).filePath(rawPath);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-thread comm-script tabs (GUI:LOAD_COMM_T / EXEC_COMM_T / CLEAR_COMM_T)
// ─────────────────────────────────────────────────────────────────────────────

// Finds (or creates) the closable tab for comm-script thread <tid> and loads
// rawPath into it. Called on GUI:LOAD_COMM_T:<tid>:<path>.
//
// Design note: per-thread viewers are read-only. They exist to show what a
// background '&' comm script is currently executing, not to be edited —
// editing the file that's still running underneath a live thread would be
// confusing, and there's already a perfectly good "MAIN" tab / file browser
// for editing comm scripts at rest. This also means no save/modified/close-
// confirmation plumbing is needed per tab, unlike m_w2.
void MainWindow::loadCommTabForThread(int tid, const QString &rawPath)
{
    if (!m_commTabs) return;

    const QString resolved = resolveCommScriptPath(rawPath);
    const QString loadPath = (!resolved.isEmpty() && QFileInfo::exists(resolved))
                             ? resolved : rawPath;

    auto it = m_commThreadTabs.find(tid);
    if (it == m_commThreadTabs.end()) {
        auto *v = new ScriptViewer(m_commTabs);
        v->enableCommHighlighting(true);
        v->setReadOnly(true);

        CommThreadTab tab;
        tab.viewer = v;
        m_commThreadTabs.insert(tid, tab);
        it = m_commThreadTabs.find(tid);

        const int idx = m_commTabs->addTab(v, QString());
        m_commTabs->setTabToolTip(idx, loadPath);
    }

    ScriptViewer *v = it->viewer;
    const QString currentCanon = QFileInfo(v->currentFile()).canonicalFilePath();
    const QString loadCanon    = QFileInfo(loadPath).canonicalFilePath();
    if (currentCanon != loadCanon || currentCanon.isEmpty()) {
        v->loadScript(loadPath);
        it->baseLabel = QString("%1 #%2").arg(QFileInfo(loadPath).fileName()).arg(tid);
        const int idx = m_commTabs->indexOf(v);
        if (idx >= 0) m_commTabs->setTabToolTip(idx, loadPath);
    }
    updateCommTabLabel(tid, /*live=*/true);
}

// GUI:CLEAR_COMM_T:<tid> — thread finished. Drops the "●" live marker; the
// tab itself is left open (per user preference) until closed via its ×.
void MainWindow::markCommTabFinished(int tid)
{
    updateCommTabLabel(tid, /*live=*/false);
}

// Redraws tab <tid>'s label/colour to reflect whether its thread is
// currently running — same "● " prefix + colour convention already used
// for the modified-state marker on m_tabWidget's script tabs.
void MainWindow::updateCommTabLabel(int tid, bool live)
{
    if (!m_commTabs) return;
    const auto it = m_commThreadTabs.constFind(tid);
    if (it == m_commThreadTabs.constEnd()) return;

    const int idx = m_commTabs->indexOf(it->viewer);
    if (idx < 0) return;

    m_commTabs->setTabText(idx, (live ? QStringLiteral("\u25CF ") : QString()) + it->baseLabel);
    m_commTabs->tabBar()->setTabTextColor(idx, live ? QColor("#50fa7b") : QColor("#c8d0e0"));
}

// User clicked a per-thread tab's × button. The "MAIN" tab (index 0) has no
// close button (removed at construction), so index is always a dynamic
// per-thread tab here — but guard defensively anyway.
void MainWindow::onCommTabCloseRequested(int index)
{
    if (!m_commTabs || index <= 0) return;

    QWidget *w = m_commTabs->widget(index);
    for (auto it = m_commThreadTabs.begin(); it != m_commThreadTabs.end(); ++it) {
        if (it->viewer == w) {
            m_commThreadTabs.erase(it);
            break;
        }
    }
    m_commTabs->removeTab(index);
    w->deleteLater();
}

// Closes every per-thread comm-script tab (keeps "MAIN"). Used by the
// panel's own CLOSE ALL button and by the toolbar RESET button.
void MainWindow::closeAllCommThreadTabs()
{
    if (!m_commTabs || m_commThreadTabs.isEmpty()) return;

    for (auto it = m_commThreadTabs.constBegin(); it != m_commThreadTabs.constEnd(); ++it) {
        const int idx = m_commTabs->indexOf(it->viewer);
        if (idx >= 0) m_commTabs->removeTab(idx);
        it->viewer->deleteLater();
    }
    m_commThreadTabs.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Threading helpers
// ─────────────────────────────────────────────────────────────────────────────

// Returns the canonical path of the comm script invoked on the given main-script
// line when that line is a threaded (&) invocation, or an empty string otherwise.
// Used by THREAD_START/DONE to populate m_threadedCommScripts.
QString MainWindow::threadedCommScriptForLine(ScriptViewer *viewer, int lineNo) const
{
    const QString line = viewer->lineText(lineNo);
    if (line.isEmpty()) return {};

    // Must end with & (possibly followed by whitespace) to be a threaded call.
    if (!line.trimmed().endsWith(QLatin1Char('&'))) return {};

    // Reuse the same regex patterns as autoLoadCommScriptForLine.
    static const QRegularExpression scriptCmd(
        R"(\b[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.SCRIPT\s+(\S+))"
    );
    static const QRegularExpression scriptArg(
        R"(\b[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.([A-Z][A-Z0-9_]*)\s+script\s+(\S+))"
    );

    QRegularExpressionMatch m = scriptCmd.match(line);
    if (!m.hasMatch()) m = scriptArg.match(line);
    if (!m.hasMatch()) return {};

    const QString scriptName = m.captured(m.regularExpression() == scriptCmd ? 1 : 2);
    const QString baseDir = !viewer->currentFile().isEmpty()
                            ? QFileInfo(viewer->currentFile()).absolutePath()
                            : QDir::currentPath();
    const QString resolved = QDir(baseDir).filePath(scriptName);
    return QFileInfo(resolved).canonicalFilePath();  // empty if file does not exist
}

// Returns true when filePath (resolved to a canonical path) is in the set of
// comm scripts currently executing inside a '&' thread.
bool MainWindow::isThreadedCommFile(const QString &filePath) const
{
    if (filePath.isEmpty() || m_threadedCommScripts.isEmpty()) return false;
    return m_threadedCommScripts.contains(QFileInfo(filePath).canonicalFilePath());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Process lifetime
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::terminateProcess()
{
    if (m_process->state() == QProcess::NotRunning)
        return;

    if (m_terminalMode) {
        // The shell is active — ask it to exit cleanly before we SIGTERM.
        // Send "#q\n" up to twice: once to exit any nested sub-shell, and once
        // more to exit the outer uShell session.  A short wait after each
        // gives the shell time to process the command and emit GUI:SHELL_EXIT
        // (which onProcessOutput will dispatch normally, clearing m_terminalMode
        // and writing SHELL_DONE back so the interpreter can resume).
        m_w3->appendStatus("Stopping shell — sending exit sequence…");
        for (int pass = 0; pass < 2; ++pass) {
            if (m_process->state() != QProcess::Running) break;
            if (!m_terminalMode) break;   // GUI:SHELL_EXIT already received — SHELL_DONE
                                          // was sent; the interpreter resumed the main
                                          // script — do NOT send #q or terminate here
            // '#q' + line ending — same sequence as the STOP button.
            // Use \r\n on Windows (some shells require CR), bare \n on Unix.
#ifdef Q_OS_WIN
            m_process->write("#q\r\n");
#else
            m_process->write("#q\x0A");
#endif
            // Process pending output for up to 800 ms so dispatchLine() can
            // handle GUI:SHELL_EXIT and clear m_terminalMode.
            m_process->waitForReadyRead(800);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        // If the shell exited cleanly (m_terminalMode cleared), the interpreter
        // has resumed its main script and SHELL_DONE has already been written.
        // Leave the process running — onProcessFinished will fire in due course.
        // Only fall through to terminate() if the shell stubbornly refused to exit.
        if (!m_terminalMode)
            return;
    }

    // Give the interpreter a moment to finish its own cleanup
    if (m_process->state() == QProcess::Running) {
        m_stoppingByUser = true;
        // Use kill() directly — on Linux, QProcess::terminate() sends SIGTERM
        // which Qt reports as CrashExit even on a clean signal delivery,
        // indistinguishable from a real crash.  SIGKILL is unambiguous and
        // avoids any risk from a buggy SIGTERM handler in the interpreter.
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  State helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onResetErrorBars()
{
    // Clear error markers from every core-script tab viewer
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) v->clearErrorLines();
    }
    // Clear comm-script viewer — restore normal cleared state if no file is
    // "permanently" loaded (i.e. it was only kept visible due to the error)
    // Clear w2 content only if it was being kept solely due to the error markers.
    // Check before clearErrorLines() while the lines are still present.
    const bool hadErrors = m_w2->hasErrorLines();
    m_w2->clearErrorLines();
    if (hadErrors)
        m_w2->clear();

    m_w3->clear();
    closeAllCommThreadTabs();
    m_led->setState(StatusLed::State::Idle);
    m_ledLabel->setText("IDLE");

    m_resetBtn->setEnabled(false);
}

void MainWindow::onReloadAll()
{
    // Every ScriptViewer that currently has a real file on disk: all the
    // core-script/INI tabs, plus the comm-script window (m_w2) if it has
    // something loaded.
    QVector<ScriptViewer *> viewers;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && !v->currentFile().isEmpty())
            viewers << v;
    }
    if (m_w2 && !m_w2->currentFile().isEmpty())
        viewers << m_w2;

    if (viewers.isEmpty()) {
        setStatus("Nothing to reload");
        return;
    }

    // Warn before discarding unsaved edits
    bool anyModified = false;
    for (auto *v : viewers)
        if (v->isModified()) { anyModified = true; break; }

    if (anyModified) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Unsaved changes");
        msgBox.setText("Some open files have unsaved changes.\n"
                        "Reloading will discard those edits and reread every "
                        "file from disk.\nContinue?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        if (msgBox.exec() != QMessageBox::Yes) return;
    }

    for (auto *v : viewers) {
        v->loadScript(v->currentFile());
        updateTabModifiedState(v);   // no-op for m_w2 (it isn't a tab widget)
    }

    // Refresh tab label colours (modified/running/clean) after the reload
    onCurrentTabChanged(m_tabWidget->currentIndex());

    m_w3->appendStatus(QString("Reloaded %1 file(s) from disk").arg(viewers.size()));
    setStatus(QString("Reloaded %1 file(s)").arg(viewers.size()));
}

void MainWindow::setRunning(bool on)
{
    m_running = on;
    m_startStopBtn->setText(on ? "■  STOP" : "▶  RUN");
    m_startStopBtn->setProperty("running", on);
    m_startStopBtn->style()->unpolish(m_startStopBtn);
    m_startStopBtn->style()->polish(m_startStopBtn);

    // Disable reset button while running; re-enable after if errors are present
    if (on) {
        m_resetBtn->setEnabled(false);
    } else {
        bool bAnyErrors = m_w2->hasErrorLines();
        if (!bAnyErrors) {
            for (int i = 0; i < m_tabWidget->count() && !bAnyErrors; ++i) {
                auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
                if (v) bAnyErrors = v->hasErrorLines();
            }
        }
        m_resetBtn->setEnabled(bAnyErrors);
    }

    if (on) {
        m_led->setState(StatusLed::State::Running);
        m_ledLabel->setText("RUNNING");
    }
}

void MainWindow::setStatus(const QString &msg)
{
    m_statusText->setText(msg);
    m_statusRight->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Font scaling
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::adjustFontSize(int delta)
{
    m_fontSize = (delta == 0) ? k_fontDefault
                              : qBound(k_fontMin, m_fontSize + delta, k_fontMax);
    applyFontSize();
    setStatus(QString("Font size: %1 pt").arg(m_fontSize));
}

void MainWindow::applyFontSize()
{
    const QFont monoFont = buildEditorFont(m_fontSize);

    // Apply to every tab's viewer
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) v->setEditorFont(monoFont);
    }
    m_w2->setEditorFont(monoFont);
    m_w3->setLogFont(monoFont);
    if (m_wCommDump) m_wCommDump->setDumpFont(monoFont);
    m_w4->setTerminalFont(monoFont);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Event filter — INI path edit click-to-open
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonPress &&
        (obj == m_iniPathEdit || obj == m_scriptPathEdit))
    {
        const QString path = static_cast<QLineEdit *>(obj)->text().trimmed();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            // Scan all tabs for an already-loaded copy
            for (int i = 0; i < m_tabWidget->count(); ++i) {
                auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
                if (v && QFileInfo(v->currentFile()).canonicalFilePath()
                              == QFileInfo(path).canonicalFilePath()) {
                    m_tabWidget->setCurrentIndex(i);
                    return false;   // let click focus the edit too
                }
            }
            // Not yet open — load into a new tab
            addTab(path);
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drag and drop
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        for (const QUrl &url : ev->mimeData()->urls())
            if (url.isLocalFile()) { ev->acceptProposedAction(); return; }
    }
    ev->ignore();
}

void MainWindow::dropEvent(QDropEvent *ev)
{
    if (!ev->mimeData()->hasUrls()) { ev->ignore(); return; }

    bool accepted = false;
    for (const QUrl &url : ev->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (accepted)
            addTab(path);           // multiple files → each gets its own tab
        else
            loadIntoCurrentTab(path);
        accepted = true;
    }
    if (accepted) ev->acceptProposedAction(); else ev->ignore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Close event
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (m_running) {
        const auto ans = QMessageBox::question(
            this, "Interpreter running",
            "The interpreter is still running.\nTerminate it and quit?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) { ev->ignore(); return; }
        terminateProcess();
    }

    // Check comm script for unsaved changes first
    if (m_w2 && m_w2->isModified()) {
        QMessageBox dlg(this);
        dlg.setWindowTitle("Unsaved changes");
        dlg.setText(QString("Comm script \"%1\" has unsaved changes.\nSave before quitting?")
            .arg(QFileInfo(m_w2->currentFile()).fileName()));
        dlg.setIcon(QMessageBox::Question);
        auto *saveBtn    = dlg.addButton("Save",    QMessageBox::AcceptRole);
        auto *discardBtn = dlg.addButton("Discard", QMessageBox::DestructiveRole);
        dlg.addButton("Cancel", QMessageBox::RejectRole);
        dlg.setDefaultButton(saveBtn);
        dlg.exec();
        const auto *clicked = dlg.clickedButton();
        if (clicked == saveBtn && !m_w2->save()) { ev->ignore(); return; }
        if (clicked != saveBtn && clicked != discardBtn) { ev->ignore(); return; }
    }

    // Check for any unsaved tabs
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && v->isModified()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("Unsaved changes");
            msgBox.setText("Some tabs have unsaved changes.\nSave all before quitting?");
            msgBox.setStandardButtons(QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
            msgBox.button(QMessageBox::Discard)->setText("Discard");  // override platform text

            const auto ans = msgBox.exec();
            if (ans == QMessageBox::Cancel) { ev->ignore(); return; }
            if (ans == QMessageBox::SaveAll) saveAllTabs();
            break;
        }
    }

    // Persist all open tab paths and active tab index
    QStringList paths;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && !v->currentFile().isEmpty()) paths << v->currentFile();
    }

    QSettings cfg;
    cfg.setValue("window/geometry",   saveGeometry());
    cfg.setValue("session/tabPaths",  paths);
    cfg.setValue("session/activeTab", m_tabWidget->currentIndex());
    cfg.setValue("session/fontSize",  m_fontSize);

    ev->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Save helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::saveCurrentTab()
{
    // If the COMM editor (m_w2) contains the focused widget, save it instead
    // of the active tab.  hasFocus() only checks the ScriptViewer frame itself;
    // the actual editing happens inside its child CodeEditor / viewport, so we
    // check whether any descendant holds keyboard focus.
    if (m_w2 && m_w2->isModified()) {
        QWidget *fw = QApplication::focusWidget();
        if (fw && (fw == m_w2 || m_w2->isAncestorOf(fw))) {
            if (m_w2->save())
                setStatus(QString("Saved: %1").arg(QFileInfo(m_w2->currentFile()).fileName()));
            return;
        }
    }

    auto *viewer = currentViewer();
    if (!viewer) return;
    if (viewer->save()) {
        updateTabModifiedState(viewer);
        setStatus(QString("Saved: %1").arg(QFileInfo(viewer->currentFile()).fileName()));
    }
}

void MainWindow::saveAllTabs()
{
    int saved = 0;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && v->isModified()) {
            if (v->save()) { updateTabModifiedState(v); ++saved; }
        }
    }
    setStatus(saved > 0
              ? QString("Saved %1 file(s)").arg(saved)
              : "All files already saved");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tab modified-state indicator
//  Uses a coloured dot (●) prefixed to the tab label:
//    ● filename   → modified (amber)
//    filename     → clean    (normal colour)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::updateTabModifiedState(ScriptViewer *viewer)
{
    // Find which tab owns this viewer
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->widget(i) != viewer) continue;

        const bool mod = viewer->isModified();

        // Build clean label (strip any existing prefix)
        QString label = m_tabWidget->tabText(i);
        if (label.startsWith("● ")) label = label.mid(2);

        if (mod) {
            m_tabWidget->setTabText(i, "● " + label);
            m_tabWidget->tabBar()->setTabTextColor(i, QColor("#ff5555"));  // red  = modified
        } else {
            m_tabWidget->setTabText(i, label);
            // Running tab gets blue, clean tabs get green
            const QColor cleanColor = (i == m_runningTab)
                                      ? QColor("#4a9eff")   // blue  = running
                                      : QColor("#c8d0e0");  // light blue-gray = clean/saved
            m_tabWidget->tabBar()->setTabTextColor(i, cleanColor);
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Comm-script preview — called when user clicks a PLUGIN.SCRIPT line
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onCommScriptRequested(const QString &scriptName)
{
    // Don't interfere while the interpreter is running — it owns w2
    if (m_running) return;

    // Resolve the path relative to the active tab's script directory.
    // If the script name is already absolute, QDir resolves it unchanged.
    auto *viewer = currentViewer();
    const QString baseDir = viewer && !viewer->currentFile().isEmpty()
                            ? QFileInfo(viewer->currentFile()).absolutePath()
                            : QDir::currentPath();

    const QString resolved = QDir(baseDir).filePath(scriptName);

    if (!QFileInfo::exists(resolved)) {
        m_w3->appendStatus(
            QString("Comm script not found: %1").arg(resolved));
        return;
    }

    // Only reload if a different file is requested (avoids flicker on cursor
    // moving within the same PLUGIN.SCRIPT line)
    if (QFileInfo(m_w2->currentFile()).canonicalFilePath() ==
        QFileInfo(resolved).canonicalFilePath()) return;

    m_w2->loadScript(resolved);
    m_w3->appendStatus(
        QString("Preview: %1").arg(QFileInfo(resolved).fileName()));
}

void MainWindow::onIncludeFileRequested(const QString &resolvedPath)
{
    // Don't interfere while the interpreter is running.
    if (m_running) return;

    if (!QFileInfo::exists(resolvedPath)) {
        m_w3->appendStatus(
            QString("INCLUDE file not found: %1").arg(resolvedPath));
        return;
    }

    // If the file is already open in a tab, switch to it — same pattern used
    // when clicking a path edit field or dropping a file onto the window.
    const QString canonical = QFileInfo(resolvedPath).canonicalFilePath();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && QFileInfo(v->currentFile()).canonicalFilePath() == canonical) {
            m_tabWidget->setCurrentIndex(i);
            return;
        }
    }

    // Not yet open — open in a new tab so the original script stays visible.
    addTab(resolvedPath);
    m_w3->appendStatus(
        QString("Opened include: %1").arg(QFileInfo(resolvedPath).fileName()));
}
