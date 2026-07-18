#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QString>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QSet>
#include <QDragEnterEvent>
#include <QDropEvent>

#include "ScriptViewer.hpp"
#include "LogViewer.hpp"
#include "StatusLed.hpp"
#include "ShellTerminal.hpp"
#include "CommDumpView.hpp"

/**
 * @brief Main application window.
 *
 * Layout:
 *
 *   ┌─ toolbar ──────────────────────────────────────────────────────────┐
 *   │  [script path …] [▶ RUN / ■ STOP]  ● led  state text               │
 *   └────────────────────────────────────────────────────────────────────┘
 *   ┌─ hSplitter ────────────────────────────────────────────────────────┐
 *   │  ┌─ vSplitter ──────────────────┐  ┌─ w3 log ─────────────────────┐│
 *   │  │  ┌─ QTabWidget ───────────┐  │  │                              |│
 *   │  │  │ tab0 | tab1 | tab2 | + │  │  │                              |│
 *   │  │  │  ScriptViewer          │  │  │                              |│
 *   │  │  └────────────────────────┘  │  │                              |│
 *   │  ├──────────────────────────────┤  ├──────────────────────────────┤│
 *   │  │  w2 comm script              │  │ w4 shell terminal            |│
 *   │  └──────────────────────────────┘  └──────────────────────────────┘│
 *   └────────────────────────────────────────────────────────────────────┘
 *   ┌─ status bar ───────────────────────────────────────────────────────┐
 *   │  exit code / timing / info                                         │
 *   └────────────────────────────────────────────────────────────────────┘
 *
 * Tab management:
 *   Ctrl+T          → new empty tab
 *   Ctrl+W          → close current tab  (kept if it's the last one)
 *   Ctrl+Tab        → next tab
 *   Ctrl+Shift+Tab  → previous tab
 *   Drag-and-drop   → opens file in a new tab (or reuses current if empty)
 *   Enter in path   → loads into the active tab
 *   RUN             → runs the script shown in the active tab
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;

private slots:
    void onBrowse();
    void onStartStop();
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);
    void onCommScriptRequested(const QString &scriptName);
    void onIncludeFileRequested(const QString &resolvedPath);  // INCLUDE "file" clicked in editor

    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessStarted();

private:
    // ── GUI construction ───────────────────────────────────────────────────
    QFrame   *buildToolbar();
    QWidget  *buildCentralWidget();
    QFrame   *buildStatusBar();

    // ── Tab helpers ────────────────────────────────────────────────────────
    ScriptViewer *addTab(const QString &filePath = {});   // empty path = blank tab
    ScriptViewer *currentViewer() const;
    ScriptViewer *runningViewer() const;
    void          loadIntoTab(int index, const QString &filePath);
    void          loadIntoCurrentTab(const QString &filePath);
    void          syncPathEdit(int tabIndex);
    void          saveCurrentTab();
    void          saveAllTabs();
    void          updateTabModifiedState(ScriptViewer *viewer);

    // ── Protocol dispatch ──────────────────────────────────────────────────
    void     dispatchLine(const QString &raw);
    void     dispatchCommDump(const QString &base64Payload);   // GUI:COMM_DUMP:<base64>
    bool     autoLoadCommScriptForLine(ScriptViewer *viewer, int lineNo); // returns true if comm script was (re)loaded
    QString  resolveCommScriptPath(const QString &rawPath) const;         // resolve interpreter-relative path to absolute
    QString  threadedCommScriptForLine(ScriptViewer *viewer, int lineNo) const; // canonical path of comm script on a '&' line, or empty
    bool     isThreadedCommFile(const QString &filePath) const;           // true when filePath is in m_threadedCommScripts

    // ── State helpers ──────────────────────────────────────────────────────
    void     setRunning(bool on);
    void     onResetErrorBars();          // clear all error markers without clearing content
    void     onReloadAll();               // re-read every open script/INI (tabs + comm window) from disk
    void     setStatus(const QString &msg);

    // ── Process lifetime ───────────────────────────────────────────────────
    // Gracefully stops the interpreter.  If the shell terminal is active,
    // sends "#q\n" twice (to exit any nested shell) and waits briefly for
    // a clean exit before falling back to SIGTERM / SIGKILL.
    void     terminateProcess();

    // ── Font scaling (Ctrl++ / Ctrl+- / Ctrl+0) ───────────────────────────
    void     adjustFontSize(int delta);
    void     applyFontSize();

    // ── UI elements ────────────────────────────────────────────────────────
    QLineEdit   *m_scriptPathEdit;
    QLineEdit   *m_iniPathEdit;     // toolbar ini-config field
    QPushButton *m_startStopBtn;
    QPushButton *m_reloadBtn = nullptr;   // reloads every open script/INI file from disk
    QPushButton *m_resetBtn  = nullptr;   // clears error bars without clearing content
    StatusLed   *m_led;
    QLabel      *m_ledLabel;

    QTabWidget    *m_tabWidget;   // holds N × ScriptViewer  (replaces m_w1)
    ScriptViewer  *m_w2;             // comm script (single, unchanged)
    LogViewer     *m_w3;             // log output
    CommDumpView  *m_wCommDump = nullptr;  // plugin Rx/Tx traffic dump (always visible)
    ShellTerminal *m_w4 = nullptr;   // shell terminal (always present, active on SHELL_RUN)
    QSplitter     *m_logShellSplit = nullptr;  // vertical splitter: m_w3 / m_wCommDump / m_w4
    QLabel        *m_commScriptNameLabel = nullptr;  // filename shown next to "COMM SCRIPT" title
    QTimer        *m_splitterSaveTimer   = nullptr;  // debounce QSettings writes on splitter drag

    QLabel      *m_statusText;
    QLabel      *m_statusRight;

    // ── Process ────────────────────────────────────────────────────────────
    QProcess    *m_process;
    bool         m_running       = false;
    int          m_runningTab    = -1;   // tab index that is currently executing
    QString      m_interpreterPath;
    QString      m_iniPath;         // -c argument for the interpreter

    QByteArray   m_lineBuf;
    QByteArray   m_errBuf;          // stderr accumulation buffer (mirrors m_lineBuf)
    bool         m_terminalMode      = false;  // true while GUI:SHELL_RUN is active
    bool         m_pendingCommHighlight = false; // loadScript() called this batch; defer EXEC_COMM setCurrentLine()
    bool         m_stoppingByUser    = false;  // set in terminateProcess(), cleared in onProcessFinished
    int          m_pendingDrainCounter = 0;    // readyRead ticks since last gui_notify_flush_pending() drain
    QSet<QString> m_threadedCommScripts;  // canonical paths of comm scripts running in a '&' thread;
                                          // EXEC_COMM/LOAD_COMM/ERROR_COMM are suppressed while the
                                          // loaded comm file is in this set

    // ── Font size ──────────────────────────────────────────────────────────
    static constexpr int k_fontDefault = 12;
    static constexpr int k_fontMin     = 7;
    static constexpr int k_fontMax     = 32;
    int          m_fontSize = k_fontDefault;
};
