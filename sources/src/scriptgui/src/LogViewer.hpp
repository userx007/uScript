#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>

class LogLineNumberArea;
class QCheckBox;
class QFont;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QRect;
class QResizeEvent;
class QWidget;

// ─────────────────────────────────────────────────────────────────────────────
//  LogEdit – read-only QPlainTextEdit with a dedicated line-number gutter.
//
//  The gutter is a separate QWidget (LogLineNumberArea) positioned to the left
//  of the viewport via setViewportMargins(), exactly like CodeEditor in
//  ScriptViewer.  Line numbers are therefore never part of the document text,
//  so mouse selection never picks them up.
// ─────────────────────────────────────────────────────────────────────────────
class LogEdit : public QPlainTextEdit
{
        Q_OBJECT
    public:
        explicit LogEdit(QWidget *pParent = nullptr);

        // Recalculate gutter width and repaint (call after font changes).
        void refreshGutter();

        // Gutter geometry/paint – called by LogLineNumberArea
        int lineNumberAreaWidth() const;
        /** Paints the line-number gutter. */
        void lineNumberAreaPaintEvent(QPaintEvent *pEv);

    protected:
        /** Keeps the gutter geometry in sync with the editor. */
        void resizeEvent(QResizeEvent *pEv) override;
        /** Selects the word and highlights its other occurrences. */
        void mouseDoubleClickEvent(QMouseEvent *pEv) override;
        void mousePressEvent(QMouseEvent *pEv) override;

    private slots:
        /** Reserves the viewport margin for the line-number gutter. */
        void updateLineNumberAreaWidth(int iNewBlockCount);
        /** Scrolls or repaints the gutter after the editor changed. */
        void updateLineNumberArea(const QRect &rect, int iDy);

    private:
        void applyWordHighlights(const QString &word);
        /** Removes the word-occurrence highlights. */
        void clearWordHighlights();

        LogLineNumberArea *m_lineNumberArea;
        QString m_highlightedWord; // currently highlighted word (empty = none)
};

// ─────────────────────────────────────────────────────────────────────────────
//  LogViewer – header bar + LogEdit  (output log panel, w3)
//
//  Parses the interpreter's formatted log lines and colourises them by level:
//    DEBUG  → dim grey
//    INFO   → white
//    WARN   → amber
//    ERROR  → red
//    bare   → light-grey (LOG_EMPTY lines from the interpreter)
// ─────────────────────────────────────────────────────────────────────────────
class LogViewer : public QFrame
{
        Q_OBJECT
    public:
        explicit LogViewer(QWidget *pParent = nullptr);

        // Append a raw GUI:LOG:<message> payload (the "GUI:LOG:" prefix stripped).
        void appendLine(const QString &line);

        // Append a plain status message (rendered in dim italic, not from interpreter).
        void appendStatus(const QString &msg);

        /** Clears the log. */
        void clear();
        /** Saves the log to a file; does nothing if there is nothing new. */
        void saveLog();
        void setScriptPath(const QString &scriptPath); // called on tab switch / load
        // Set the font used in the log text area (called by MainWindow for Ctrl+/-).
        void setLogFont(const QFont &font);

        // Batches a run of appendLine()/appendStatus() calls: while a batch is
        // open, the per-call line-count label refresh and auto-scroll are
        // skipped and applied once, in endBatch(), instead of once per line.
        // A single QProcess::readyRead chunk can carry many lines at once —
        // without batching, each one paid for its own QScrollBar::setValue()
        // (with the associated geometry/repaint work) and label text update.
        // Nestable; only the outermost begin/end pair takes effect. Safe to
        // call endBatch() with nothing appended in between (no-op).
        void beginBatch();
        /** Ends a batch started with beginBatch(); unbalanced calls are ignored. */
        void endBatch();

        // Returns the numeric enum value of the selected log level (0=WERBOSE …
        // 7=FIXED), or -1 when DEFAULT is selected (meaning: don't pass -l at all).
        int logLevelArg() const;

    public slots:

        /** Enables or disables scrolling to the newest line. */
        void setAutoScroll(bool bOn)
        {
            m_autoScroll = bOn;
        }

        // Disable the log-level combo while the interpreter is running so the
        // selection cannot be changed mid-run (it only takes effect at launch).
        void setRunning(bool bRunning)
        {
            m_logLevelCb->setEnabled(!bRunning);
        }

    private:
        /** Appends one line of HTML to the log. */
        void appendFormattedLine(const QString &html);
        void markDirty(); // enable save button + clear "saved" label on first new content

        QLabel *m_titleLabel;
        QLabel *m_countLabel;
        QLabel *m_savedLabel; // shows "Saved: <path>" after a save
        LogEdit *m_logEdit;
        QPushButton *m_clearBtn;
        QPushButton *m_saveBtn;
        bool m_savedClean = true;
        QString m_scriptDir; // directory of the currently active script
        QCheckBox *m_autoScrollCb;
        QComboBox *m_logLevelCb;
        bool m_autoScroll            = true;

        // ── batching (see beginBatch()/endBatch()) ──────────────────────────
        int m_batchDepth             = 0;
        bool m_batchNeedsLabelUpdate = false;
        bool m_batchNeedsScroll      = false;

        void refreshCountAndScroll(); // the per-line work batching defers
};
