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
#include <QProcess>
#include <QStyle>
#include <QShortcut>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QRegularExpression>
#include <QTabBar>
#include <QSaveFile>
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
    static const QStringList preferred = {
        "JetBrains Mono", "Cascadia Code", "Cascadia Mono",
        "Fira Code", "Hack", "Consolas",
        "DejaVu Sans Mono", "Liberation Mono", "Courier New"
    };

    const QStringList installed = QFontDatabase::families();
    for (const QString &fam : preferred) {
        // Case-insensitive search — font family names vary by platform
        for (const QString &inst : installed) {
            if (inst.compare(fam, Qt::CaseInsensitive) == 0) {
                QFont f(inst, pointSize);
                f.setFixedPitch(true);
                f.setStyleHint(QFont::Monospace);
                return f;
            }
        }
    }
    // Nothing from the preferred list — use OS fixed font
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(pointSize);
    return f;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_process(new QProcess(this))
{
    setWindowTitle("Script Interpreter Front-End");
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
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

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

    auto *appLabel = new QLabel("SCRIPT  RUNNER", bar);
    appLabel->setObjectName("toolbarLabel");

    // Interpreter binary
    auto *interpLabel = new QLabel("INTERP", bar);
    interpLabel->setObjectName("toolbarLabel");

    auto *interpEdit = new QLineEdit(bar);
    interpEdit->setObjectName("interpPathEdit");
    interpEdit->setPlaceholderText("path/to/interpreter binary…");
    interpEdit->setToolTip("Path to the ScriptInterpreter executable");
    interpEdit->setFixedWidth(240);

    QSettings cfg;
    interpEdit->setText(cfg.value("session/interpreterPath").toString());
    m_interpreterPath = interpEdit->text();
    connect(interpEdit, &QLineEdit::textChanged, this, [this, interpEdit](const QString &t) {
        m_interpreterPath = t;
        QSettings s; s.setValue("session/interpreterPath", t);
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
    m_scriptPathEdit->setToolTip("Active tab's script path — press Enter to load");
    m_scriptPathEdit->setMinimumWidth(260);

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
    lay->addWidget(interpEdit);
    lay->addWidget(interpBrowse);
    lay->addSpacing(8);
    lay->addWidget(scriptLabel);
    lay->addWidget(m_scriptPathEdit, 1);
    lay->addWidget(browseBtn);
    lay->addSpacing(8);
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

        m_w2 = new ScriptViewer("", commWrapper);
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
    m_w3 = new LogViewer(this);

    auto *vSplit = new QSplitter(Qt::Vertical, this);
    vSplit->addWidget(m_tabWidget);
    vSplit->addWidget(commWrapper);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 2);
    vSplit->setHandleWidth(3);

    auto *hSplit = new QSplitter(Qt::Horizontal, this);
    hSplit->addWidget(vSplit);
    hSplit->addWidget(m_w3);
    hSplit->setStretchFactor(0, 1);
    hSplit->setStretchFactor(1, 1);
    hSplit->setHandleWidth(3);

    QSettings cfg;
    hSplit->restoreState(cfg.value("window/hSplit").toByteArray());
    vSplit->restoreState(cfg.value("window/vSplit").toByteArray());

    connect(hSplit, &QSplitter::splitterMoved, this, [hSplit, vSplit] {
        QSettings s;
        s.setValue("window/hSplit", hSplit->saveState());
        s.setValue("window/vSplit", vSplit->saveState());
    });

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
    auto *viewer = new ScriptViewer("", m_tabWidget);
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
    if (viewer)
        m_scriptPathEdit->setText(viewer->currentFile());
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
        const QString name = m_tabWidget->tabText(index).remove(0, 2); // strip "● "
        const auto ans = QMessageBox::question(
            this, "Unsaved changes",
            QString("'%1' has unsaved changes.\nSave before closing?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ans == QMessageBox::Cancel) return;
        if (ans == QMessageBox::Save && !viewer->save()) return;
    }

    if (index == m_runningTab) {
        const auto ans = QMessageBox::question(
            this, "Script running",
            "This tab's script is currently running.\nClose the tab anyway?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) return;
        m_process->terminate();
        m_process->waitForFinished(2000);
        m_runningTab = -1;
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
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
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

    QString interp = m_interpreterPath.trimmed();
    if (!interp.isEmpty())
        interp = interp.split(' ', Qt::SkipEmptyParts).first();
    if (interp.isEmpty()) {
        interp = QDir(QCoreApplication::applicationDirPath()).filePath("uscript");
#ifdef Q_OS_WIN
        interp += ".exe";
#endif
    }
    if (!QFileInfo::exists(interp)) {
        m_w3->appendStatus(QString("Interpreter not found: %1").arg(interp));
        return;
    }

    m_runningTab = m_tabWidget->currentIndex();

    // Refresh all tab colours
    onCurrentTabChanged(m_tabWidget->currentIndex());

    m_w2->clear();
    m_w3->clear();
    m_lineBuf.clear();
    m_execContext   = ExecContext::Main;
    m_commLineCount = 0;

    m_w3->appendStatus(QString("Starting: %1 -s %2")
                       .arg(QFileInfo(interp).fileName(),
                            QFileInfo(scriptPath).fileName()));

    QStringList env = QProcess::systemEnvironment();
    env << "SCRIPT_GUI_MODE=1";
    m_process->setEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    m_process->start(interp, {"-s", scriptPath});
}

// ─────────────────────────────────────────────────────────────────────────────
//  QProcess slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onProcessStarted()
{
    setRunning(true);
    setStatus("Running…");
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
    m_lineBuf += m_process->readAllStandardOutput();
    int nlPos;
    while ((nlPos = m_lineBuf.indexOf('\n')) != -1) {
        const QString line = QString::fromUtf8(m_lineBuf.left(nlPos)).trimmed();
        m_lineBuf.remove(0, nlPos + 1);
        if (!line.isEmpty())
            dispatchLine(line);
    }
}

void MainWindow::onProcessError()
{
    const QByteArray err = m_process->readAllStandardError();
    for (const QByteArray &raw : err.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty())
            m_w3->appendLine(line);
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_lineBuf.isEmpty()) {
        dispatchLine(QString::fromUtf8(m_lineBuf).trimmed());
        m_lineBuf.clear();
    }
    setRunning(false);
    m_w2->clear();
    m_execContext   = ExecContext::Main;
    m_commLineCount = 0;

    const int  savedRunningTab = m_runningTab;
    m_runningTab = -1;
    onCurrentTabChanged(m_tabWidget->currentIndex());   // reset tab colour

    // Restore editors to read-write and clear execution highlights
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v) { v->setReadOnly(false); v->setCurrentLine(0); }
    }
    m_w2->setReadOnly(false);
    m_w2->setCurrentLine(0);

    const QString reason = (status == QProcess::CrashExit)
                           ? "interpreter crashed"
                           : QString("exit code %1").arg(exitCode);
    m_w3->appendStatus(QString("Interpreter finished — %1").arg(reason));
    setStatus(QString("Finished (%1)").arg(reason));

    if (exitCode != 0) {
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

        if (m_execContext == ExecContext::Comm) {
            // Still inside a comm sub-script as long as lineNo fits within it.
            if (lineNo <= m_commLineCount) {
                m_w2->setCurrentLine(lineNo);
                setStatus(QString("Comm script — line %1").arg(lineNo));
                return;
            }
            // lineNo has gone past the end of the comm script — we're back in main.
            m_execContext = ExecContext::Main;
            m_w2->clearHighlight();
        }

        // ── Main-script execution ──────────────────────────────────────────
        v->setCurrentLine(lineNo);
        if (autoLoadCommScriptForLine(v, lineNo)) {
            // This line called a sub-script: enter comm context.
            m_execContext   = ExecContext::Comm;
            m_commLineCount = m_w2->lineCount();
        }
        setStatus(QString("Main script — line %1").arg(lineNo));
    }
    else if (payload.startsWith(QLatin1StringView("EXEC_COMM:"))) {
        // Explicit comm-line message (interpreter variant that does send it).
        const int lineNo = payload.mid(10).toInt();
        m_execContext = ExecContext::Comm;
        m_w2->setCurrentLine(lineNo);
        setStatus(QString("Comm script — line %1").arg(lineNo));
    }
    else if (payload.startsWith(QLatin1StringView("LOAD_COMM:"))) {
        const QString path = payload.mid(10).toString();
        m_w2->loadScript(path);
        m_w3->appendStatus(QString("Comm script: %1").arg(QFileInfo(path).fileName()));
    }
    else if (payload.startsWith(QLatin1StringView("CLEAR_COMM"))) {
        m_w2->clear();
        m_execContext   = ExecContext::Main;
        m_commLineCount = 0;
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
        R"(\b[A-Z][A-Z0-9_]*\.SCRIPT\s+(\S+))"        // PLUGIN.SCRIPT <file>
    );
    static const QRegularExpression scriptArg(
        R"(\b[A-Z][A-Z0-9_]*\.[A-Z][A-Z0-9_]*\s+script\s+(\S+))"  // PLUGIN.CMD script <file>
    );

    QRegularExpressionMatch m = scriptCmd.match(line);
    if (!m.hasMatch()) m = scriptArg.match(line);
    if (!m.hasMatch()) return false;

    const QString scriptName = m.captured(1);
    const QString baseDir = !viewer->currentFile().isEmpty()
                            ? QFileInfo(viewer->currentFile()).absolutePath()
                            : QDir::currentPath();
    const QString resolved = QDir(baseDir).filePath(scriptName);

    if (!QFileInfo::exists(resolved)) return false;

    if (m_w2->currentFile() != resolved) {
        m_w2->loadScript(resolved);
        m_w3->appendStatus(QString("Comm script: %1").arg(QFileInfo(resolved).fileName()));
    }
    return true;   // this line calls a comm sub-script (already loaded or just loaded)
}

// ─────────────────────────────────────────────────────────────────────────────
//  State helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setRunning(bool on)
{
    m_running = on;
    m_startStopBtn->setText(on ? "■  STOP" : "▶  RUN");
    m_startStopBtn->setProperty("running", on);
    m_startStopBtn->style()->unpolish(m_startStopBtn);
    m_startStopBtn->style()->polish(m_startStopBtn);

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
        m_process->terminate();
        m_process->waitForFinished(2000);
    }

    // Check for any unsaved tabs
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *v = qobject_cast<ScriptViewer *>(m_tabWidget->widget(i));
        if (v && v->isModified()) {
            const auto ans = QMessageBox::question(
                this, "Unsaved changes",
                "Some tabs have unsaved changes.\nSave all before quitting?",
                QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
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
    if (m_w2->currentFile() == resolved) return;

    m_w2->loadScript(resolved);
    m_w3->appendStatus(
        QString("Preview: %1").arg(QFileInfo(resolved).fileName()));
}
