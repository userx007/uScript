#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFrame>
#include <QString>
#include <QColor>

class LineNumberArea;

// ─────────────────────────────────────────────────────────────────────────────
//  CodeEditor – QPlainTextEdit subclass with a line-number gutter.
// ─────────────────────────────────────────────────────────────────────────────
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    // Called by ScriptViewer to highlight the active execution line.
    void highlightLine(int lineNo);   // 1-based; 0 clears
    void clearHighlight();

    // Gutter support (called by LineNumberArea)
    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *ev);

protected:
    void resizeEvent(QResizeEvent *ev) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    LineNumberArea *m_lineNumberArea;
    int             m_highlightedLine = 0;   // 0 = none
};

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptViewer – full panel: header bar + CodeEditor
// ─────────────────────────────────────────────────────────────────────────────
class ScriptViewer : public QFrame
{
    Q_OBJECT
public:
    explicit ScriptViewer(const QString &title, QWidget *parent = nullptr);

    // Load script text (replaces content; clears highlight).
    void loadScript(const QString &filePath);
    void loadText(const QString &text);

    // Move the execution band to this 1-based line (0 clears).
    void setCurrentLine(int lineNo);

    // Remove all content and clear highlight.
    void clear();

    // Set the font used in the code editor (called by MainWindow for Ctrl+/-).
    void setEditorFont(const QFont &font);

    QString currentFile() const { return m_currentFile; }

private:
    void   updateInfo();

    QLabel      *m_titleLabel;
    QLabel      *m_infoLabel;
    CodeEditor  *m_editor;
    QString      m_currentFile;
    int          m_currentLine = 0;
};
