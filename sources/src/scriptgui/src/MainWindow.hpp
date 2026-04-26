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
#include <QDragEnterEvent>
#include <QDropEvent>

#include "ScriptViewer.hpp"
#include "LogViewer.hpp"
#include "StatusLed.hpp"

/**
 * @brief Main application window.
 *
 * Layout:
 *
 *   ┌─ toolbar ──────────────────────────────────────────────────────────┐
 *   │  [script path …] [▶ RUN / ■ STOP]  ● led  state text               │
 *   └────────────────────────────────────────────────────────────────────┘
 *   ┌─ hSplitter ────────────────────────────────────────────────────────┐
 *   │  ┌─ vSplitter ──────────────────┐  ┌─ w3 log ───────────────────   │
 *   │  │  ┌─ QTabWidget ───────────┐  │  │                               │
 *   │  │  │ tab0 | tab1 | tab2 | + │  │  │                               │
 *   │  │  │  ScriptViewer          │  │  │                               │
 *   │  │  └────────────────────────┘  │  │                               │
 *   │  ├──────────────────────────────┤  │                               │
 *   │  │  w2 comm script              │  │                               │
 *   │  └──────────────────────────────┘  └─────────────────────────────  │
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
    void dropEvent(QDropEvent *ev) override;

private slots:
    void onBrowse();
    void onStartStop();
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);
    void onCommScriptRequested(const QString &scriptName);

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

    // ── State helpers ──────────────────────────────────────────────────────
    void     setRunning(bool on);
    void     setStatus(const QString &msg);

    // ── Font scaling (Ctrl++ / Ctrl+- / Ctrl+0) ───────────────────────────
    void     adjustFontSize(int delta);
    void     applyFontSize();

    // ── UI elements ────────────────────────────────────────────────────────
    QLineEdit   *m_scriptPathEdit;
    QPushButton *m_startStopBtn;
    StatusLed   *m_led;
    QLabel      *m_ledLabel;

    QTabWidget   *m_tabWidget;   // holds N × ScriptViewer  (replaces m_w1)
    ScriptViewer *m_w2;          // comm script (single, unchanged)
    LogViewer    *m_w3;          // log output

    QLabel      *m_statusText;
    QLabel      *m_statusRight;

    // ── Process ────────────────────────────────────────────────────────────
    QProcess    *m_process;
    bool         m_running       = false;
    int          m_runningTab    = -1;   // tab index that is currently executing
    QString      m_interpreterPath;

    QByteArray   m_lineBuf;

    // ── Font size ──────────────────────────────────────────────────────────
    static constexpr int k_fontDefault = 12;
    static constexpr int k_fontMin     = 7;
    static constexpr int k_fontMax     = 32;
    int          m_fontSize = k_fontDefault;
};
