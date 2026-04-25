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
#include <QTabBar>

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

    // Style the tab bar to match the dark theme
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane      { border: none; background: #12141a; }
        QTabBar::tab {
            background:    #0e1016;
            color:         #60697a;
            border:        1px solid #252a35;
            border-bottom: none;
            padding:       5px 14px;
            font-family:   "JetBrains Mono", "Consolas", monospace;
            font-size:     10px;
            min-width:     80px;
        }
        QTabBar::tab:selected {
            background: #1c1f27;
            color:      #c8d0dc;
            border-top: 2px solid #4a9eff;
        }
        QTabBar::tab:hover:!selected { background: #161920; color: #abb2bf; }
        QTabBar::close-button {
            image: none;
            subcontrol-position: right;
        }
        QTabBar::tear         { border: none; }
    )");

    // "+" button to add a new blank tab
    auto *addTabBtn = new QPushButton("+", m_tabWidget);
    addTabBtn->setObjectName("clearBtn");
    addTabBtn->setToolTip("New script tab  (Ctrl+T)");
    addTabBtn->setFixedSize(24, 24);
    m_tabWidget->setCornerWidget(addTabBtn, Qt::TopRightCorner);
    connect(addTabBtn, &QPushButton::clicked, this, [this]{ addTab(); });

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this,        &MainWindow::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this,        &MainWindow::onCurrentTabChanged);

    // ── Comm script viewer + log ──────────────────────────────────────────
    m_w2 = new ScriptViewer("COMM SCRIPT", this);
    m_w3 = new LogViewer(this);

    auto *vSplit = new QSplitter(Qt::Vertical, this);
    vSplit->addWidget(m_tabWidget);
    vSplit->addWidget(m_w2);
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
    viewer->setEditorFont([this]{
        QFont f; f.setFamily("JetBrains Mono"); f.setFixedPitch(true);
        f.setStyleHint(QFont::Monospace); f.setPointSize(m_fontSize);
        return f;
    }());

    const QString tabLabel = filePath.isEmpty()
                             ? "untitled"
                             : QFileInfo(filePath).fileName();
    const int idx = m_tabWidget->addTab(viewer, tabLabel);
    m_tabWidget->setTabToolTip(idx, filePath.isEmpty() ? "(empty)" : filePath);

    if (!filePath.isEmpty())
        viewer->loadScript(filePath);

    m_tabWidget->setCurrentIndex(idx);
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
        m_tabWidget->tabBar()->setTabTextColor(
            i, i == m_runningTab ? QColor("#50fa7b") : QColor("#60697a"));
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

    const int  savedRunningTab = m_runningTab;
    m_runningTab = -1;
    onCurrentTabChanged(m_tabWidget->currentIndex());   // reset tab colour

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
            m_tabWidget->tabBar()->setTabTextColor(savedRunningTab, QColor("#ff5555"));
    } else {
        m_led->setState(StatusLed::State::Ready);
        m_ledLabel->setText("DONE");
        if (savedRunningTab >= 0 && savedRunningTab < m_tabWidget->count())
            m_tabWidget->tabBar()->setTabTextColor(savedRunningTab, QColor("#50fa7b"));
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
        if (auto *v = runningViewer()) v->setCurrentLine(lineNo);
        setStatus(QString("Main script — line %1").arg(lineNo));
    }
    else if (payload.startsWith(QLatin1StringView("EXEC_COMM:"))) {
        const int lineNo = payload.mid(10).toInt();
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
    }
    else if (payload.startsWith(QLatin1StringView("LOG:"))) {
        m_w3->appendLine(payload.mid(4).toString());
    }
    else {
        m_w3->appendLine(raw);
    }
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
    QFont monoFont("JetBrains Mono");
    if (!monoFont.exactMatch()) monoFont.setFamily("Cascadia Code");
    if (!monoFont.exactMatch()) monoFont.setFamily("Consolas");
    if (!monoFont.exactMatch()) monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(m_fontSize);
    monoFont.setFixedPitch(true);

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
