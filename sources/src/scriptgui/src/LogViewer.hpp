#pragma once
#include <QFrame>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QWidget>

class LogLineNumberArea;

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
    explicit LogEdit(QWidget *parent = nullptr);

    // Recalculate gutter width and repaint (call after font changes).
    void refreshGutter();

    // Gutter geometry/paint – called by LogLineNumberArea
    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *ev);

protected:
    void resizeEvent(QResizeEvent *ev) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    LogLineNumberArea *m_lineNumberArea;
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
    explicit LogViewer(QWidget *parent = nullptr);

    // Append a raw GUI:LOG:<message> payload (the "GUI:LOG:" prefix stripped).
    void appendLine(const QString &line);

    // Append a plain status message (rendered in dim italic, not from interpreter).
    void appendStatus(const QString &msg);

    void clear();
    void saveLog();
    void setScriptPath(const QString &scriptPath);  // called on tab switch / load
    // Set the font used in the log text area (called by MainWindow for Ctrl+/-).
    void setLogFont(const QFont &font);

public slots:
    void setAutoScroll(bool on) { m_autoScroll = on; }

private:
    void appendFormattedLine(const QString &html);
    void markDirty();   // enable save button + clear "saved" label on first new content

    QLabel      *m_titleLabel;
    QLabel      *m_countLabel;
    QLabel      *m_savedLabel;    // shows "Saved: <path>" after a save
    LogEdit     *m_logEdit;
    QPushButton *m_clearBtn;
    QPushButton *m_saveBtn;
    bool         m_savedClean = true;
    QString      m_scriptDir;     // directory of the currently active script
    QCheckBox   *m_autoScrollCb;
    bool         m_autoScroll = true;
    int          m_lineCount  = 0;
};
