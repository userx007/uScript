#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QString>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QSplitter>
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
 *   │  ┌─ vSplitter ──────────┐  ┌─ w3 log ───────────────────────────   │
 *   │  │  w1 main script      │  │                                       │
 *   │  ├──────────────────────┤  │                                       │
 *   │  │  w2 comm script      │  │                                       │
 *   │  └──────────────────────┘  └─────────────────────────────────────  │
 *   └────────────────────────────────────────────────────────────────────┘
 *   ┌─ status bar ───────────────────────────────────────────────────────┐
 *   │  exit code / timing / info                                         │
 *   └────────────────────────────────────────────────────────────────────┘
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;   // drag-and-drop support
    void dropEvent(QDropEvent *ev) override;

private slots:
    // Toolbar actions
    void onBrowse();
    void onStartStop();

    // QProcess signals
    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessStarted();

private:
    // ── GUI construction ──────────────────────────────────────────────────
    QFrame   *buildToolbar();
    QWidget  *buildCentralWidget();   // returns the root splitter
    QFrame   *buildStatusBar();

    // ── Protocol dispatch ─────────────────────────────────────────────────
    void     dispatchLine(const QString &raw);

    // ── State helpers ─────────────────────────────────────────────────────
    void     setRunning(bool on);
    void     setStatus(const QString &msg);

    // ── Font scaling (Ctrl++ / Ctrl+- / Ctrl+0) ──────────────────────────
    void     adjustFontSize(int delta);   // delta in pt; 0 = reset to default
    void     applyFontSize();

    // ── UI elements ───────────────────────────────────────────────────────
    QLineEdit   *m_scriptPathEdit;
    QPushButton *m_startStopBtn;
    StatusLed   *m_led;
    QLabel      *m_ledLabel;

    ScriptViewer *m_w1;      // main script
    ScriptViewer *m_w2;      // comm script
    LogViewer    *m_w3;      // log output

    QLabel      *m_statusText;
    QLabel      *m_statusRight;

    // ── Process ───────────────────────────────────────────────────────────
    QProcess    *m_process;
    bool         m_running = false;
    QString      m_interpreterPath;  // resolved on first run

    // Overflow buffer for partial lines from QProcess
    QByteArray   m_lineBuf;

    // ── Font size (pt) — shared across all code panels ────────────────────
    static constexpr int k_fontDefault = 12;
    static constexpr int k_fontMin     = 7;
    static constexpr int k_fontMax     = 32;
    int          m_fontSize = k_fontDefault;
};
