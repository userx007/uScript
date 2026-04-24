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
#include <QElapsedTimer>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QApplication>
#include <QProcess>
#include <QStyle>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_process(new QProcess(this))
{
    setWindowTitle("Script Interpreter Front-End");
    setMinimumSize(1100, 680);

    // ── Restore geometry from previous session ────────────────────────────
    QSettings cfg;
    restoreGeometry(cfg.value("window/geometry").toByteArray());

    // ── Build UI ──────────────────────────────────────────────────────────
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildToolbar());
    rootLayout->addWidget(buildCentralWidget(), 1);  // stretch=1 so it fills available space
    rootLayout->addWidget(buildStatusBar());

    setCentralWidget(root);

    // ── Wire QProcess ─────────────────────────────────────────────────────
    connect(m_process, &QProcess::readyReadStandardOutput,
            this,      &MainWindow::onProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this,      &MainWindow::onProcessError);
    connect(m_process, &QProcess::started,
            this,      &MainWindow::onProcessStarted);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    // ── Restore last script path ──────────────────────────────────────────
    const QString last = cfg.value("session/lastScript").toString();
    if (!last.isEmpty())
        m_scriptPathEdit->setText(last);

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

    // ── App title ─────────────────────────────────────────────────────────
    auto *appLabel = new QLabel("SCRIPT  RUNNER", bar);
    appLabel->setObjectName("toolbarLabel");

    // ── Interpreter binary path ───────────────────────────────────────────
    auto *interpLabel = new QLabel("INTERP", bar);
    interpLabel->setObjectName("toolbarLabel");

    auto *interpEdit = new QLineEdit(bar);
    interpEdit->setObjectName("interpPathEdit");
    interpEdit->setPlaceholderText("path/to/ScriptFrontend interpreter binary…");
    interpEdit->setToolTip("Path to the ScriptInterpreter executable");
    interpEdit->setFixedWidth(240);

    QSettings cfg;
    interpEdit->setText(cfg.value("session/interpreterPath").toString());
    connect(interpEdit, &QLineEdit::textChanged, this, [this, interpEdit](const QString &t) {
        m_interpreterPath = t;
        QSettings s; s.setValue("session/interpreterPath", t);
    });
    m_interpreterPath = interpEdit->text();

    auto *interpBrowse = new QPushButton("…", bar);
    interpBrowse->setObjectName("browseBtn");
    interpBrowse->setToolTip("Browse for interpreter binary");
    connect(interpBrowse, &QPushButton::clicked, this, [this, interpEdit] {
        const QString f = QFileDialog::getOpenFileName(
            this, "Select Interpreter Binary",
            interpEdit->text().isEmpty()
                ? QDir::homePath()
                : QFileInfo(interpEdit->text()).absolutePath()
        );
        if (!f.isEmpty()) interpEdit->setText(f);
    });

    // ── Script path ───────────────────────────────────────────────────────
    auto *scriptLabel = new QLabel("SCRIPT", bar);
    scriptLabel->setObjectName("toolbarLabel");

    m_scriptPathEdit = new QLineEdit(bar);
    m_scriptPathEdit->setPlaceholderText("path/to/main_script.txt…");
    m_scriptPathEdit->setToolTip("Main script file to execute");
    m_scriptPathEdit->setMinimumWidth(260);

    auto *browseBtn = new QPushButton("…", bar);
    browseBtn->setObjectName("browseBtn");
    browseBtn->setToolTip("Browse for script file");
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowse);

    // ── Run / Stop ────────────────────────────────────────────────────────
    m_startStopBtn = new QPushButton("▶  RUN", bar);
    m_startStopBtn->setObjectName("startBtn");
    m_startStopBtn->setToolTip("Launch the interpreter (--gui mode)");
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStop);

    // ── Status LED + label ────────────────────────────────────────────────
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
    // w1 / w2 stacked vertically on the left; w3 on the right
    m_w1 = new ScriptViewer("MAIN SCRIPT",  this);
    m_w2 = new ScriptViewer("COMM SCRIPT",  this);
    m_w3 = new LogViewer(this);

    auto *vSplit = new QSplitter(Qt::Vertical, this);
    vSplit->addWidget(m_w1);
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

    // Restore splitter states
    QSettings cfg;
    hSplit->restoreState(cfg.value("window/hSplit").toByteArray());
    vSplit->restoreState(cfg.value("window/vSplit").toByteArray());

    // Save splitter states on change
    connect(hSplit, &QSplitter::splitterMoved, this, [this, hSplit, vSplit] {
        QSettings s;
        s.setValue("window/hSplit", hSplit->saveState());
        s.setValue("window/vSplit", vSplit->saveState());
    });

    // Return the splitter so the constructor can add it to rootLayout
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
//  Toolbar actions
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onBrowse()
{
    const QString start = m_scriptPathEdit->text().isEmpty()
                          ? QDir::homePath()
                          : QFileInfo(m_scriptPathEdit->text()).absolutePath();

    const QString f = QFileDialog::getOpenFileName(
        this, "Select Main Script",
        start,
        "Script files (*.txt *.scr *.script);;All files (*)");

    if (!f.isEmpty()) {
        m_scriptPathEdit->setText(f);
        QSettings cfg;
        cfg.setValue("session/lastScript", f);
        // Pre-load the script so the user can read it before running
        m_w1->loadScript(f);
        m_w3->appendStatus(QString("Loaded: %1").arg(QFileInfo(f).fileName()));
        setStatus(QString("Script loaded: %1").arg(QFileInfo(f).fileName()));
    }
}

void MainWindow::onStartStop()
{
    if (m_running) {
        // ── Stop ─────────────────────────────────────────────────────────
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
        return;
    }

    // ── Start ─────────────────────────────────────────────────────────────
    const QString scriptPath = m_scriptPathEdit->text().trimmed();
    if (scriptPath.isEmpty()) {
        m_w3->appendStatus("No script selected. Use the '…' button to pick one.");
        return;
    }
    if (!QFileInfo::exists(scriptPath)) {
        m_w3->appendStatus(QString("Script not found: %1").arg(scriptPath));
        return;
    }

    // Resolve interpreter: look next to our own executable if not set.
    // Also tolerate the user pasting a full command like "./uscript --gui -s foo"
    // into the field — extract just the binary token.
    QString interp = m_interpreterPath.trimmed();
    if (!interp.isEmpty()) {
        // Strip any arguments the user may have typed after the binary name
        interp = interp.split(' ', Qt::SkipEmptyParts).first();
    }
    if (interp.isEmpty()) {
        interp = QDir(QCoreApplication::applicationDirPath())
                 .filePath("uscript");
#ifdef Q_OS_WIN
        interp += ".exe";
#endif
    }
    if (!QFileInfo::exists(interp)) {
        m_w3->appendStatus(
            QString("Interpreter not found: %1\n"
                    "Set the path in the INTERP field above.").arg(interp));
        return;
    }

    // Clear panels for fresh run
    m_w1->loadScript(scriptPath);
    m_w2->clear();
    m_w3->clear();
    m_lineBuf.clear();

    m_w3->appendStatus(QString("Starting: %1 -s %2")
                       .arg(QFileInfo(interp).fileName(),
                            QFileInfo(scriptPath).fileName()));

    // Activate GUI mode via environment variable — the interpreter checks
    // SCRIPT_GUI_MODE at startup.  This avoids any CLI parser flag issues.
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
    // Accumulate raw bytes to handle partial lines robustly.
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
    // stderr is shown as bare log lines with no GUI: prefix
    const QByteArray err = m_process->readAllStandardError();
    for (const QByteArray &raw : err.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty())
            m_w3->appendLine(line);
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // Flush any remaining partial line
    if (!m_lineBuf.isEmpty()) {
        dispatchLine(QString::fromUtf8(m_lineBuf).trimmed());
        m_lineBuf.clear();
    }

    setRunning(false);
    m_w2->clear();    // always clear comm panel when interpreter exits

    const QString reason = (status == QProcess::CrashExit)
                           ? "interpreter crashed"
                           : QString("exit code %1").arg(exitCode);
    m_w3->appendStatus(QString("Interpreter finished — %1").arg(reason));
    setStatus(QString("Finished (%1)").arg(reason));

    if (exitCode != 0) {
        m_led->setState(StatusLed::State::Error);
        m_ledLabel->setText("ERROR");
    } else {
        m_led->setState(StatusLed::State::Ready);
        m_ledLabel->setText("DONE");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Protocol dispatch
//
//  Expected prefixes (as defined in uGuiNotify.hpp):
//    GUI:EXEC_MAIN:<lineNo>
//    GUI:EXEC_COMM:<lineNo>
//    GUI:LOAD_COMM:<path>
//    GUI:CLEAR_COMM
//    GUI:LOG:<message>
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::dispatchLine(const QString &raw)
{
    if (!raw.startsWith("GUI:")) {
        // Any non-prefixed stdout line goes straight to the log
        m_w3->appendLine(raw);
        return;
    }

    const QStringView payload(raw.begin() + 4, raw.end());   // after "GUI:"

    if (payload.startsWith(QLatin1StringView("EXEC_MAIN:"))) {
        const int lineNo = payload.mid(10).toInt();
        m_w1->setCurrentLine(lineNo);
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
        // Unknown GUI: prefix — show raw in log for debugging
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
    // Force style re-evaluation for the dynamic property
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
    m_statusRight->setText(
        QDateTime::currentDateTime().toString("hh:mm:ss"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Close event — persist state, terminate interpreter if alive
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (m_running) {
        const auto ans = QMessageBox::question(
            this, "Interpreter running",
            "The interpreter is still running.\nTerminate it and quit?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) {
            ev->ignore();
            return;
        }
        m_process->terminate();
        m_process->waitForFinished(2000);
    }

    QSettings cfg;
    cfg.setValue("window/geometry", saveGeometry());

    ev->accept();
}
