#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFrame>
#include <QString>
#include <QColor>
#include <QKeyEvent>

class LineNumberArea;
class ScriptHighlighter;

// ─────────────────────────────────────────────────────────────────────────────
//  CodeEditor – editable QPlainTextEdit with line-number gutter.
//
//  Editing behaviour:
//    Tab key   → inserts TAB_WIDTH spaces (never a \t character)
//    Backspace → if cursor is at an indent boundary, deletes TAB_WIDTH spaces
//    Otherwise → standard QPlainTextEdit behaviour
// ─────────────────────────────────────────────────────────────────────────────
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    static constexpr int TAB_WIDTH = 4;

    explicit CodeEditor(QWidget *parent = nullptr);

    void highlightLine(int lineNo);      // 1-based; 0 = clear
    void clearHighlight();
    void setHighlighting(bool on);

    // Gutter (called by LineNumberArea)
    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *ev);

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev)  override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    LineNumberArea    *m_lineNumberArea;
    int                m_highlightedLine = 0;
    ScriptHighlighter *m_highlighter     = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptViewer – header bar + CodeEditor
//
//  Modification tracking:
//    isModified()          true if document has unsaved changes
//    save() / saveAs()     write to disk, clears modified flag
//    modificationChanged   emitted whenever the flag flips
// ─────────────────────────────────────────────────────────────────────────────
class ScriptViewer : public QFrame
{
    Q_OBJECT
public:
    explicit ScriptViewer(const QString &title, QWidget *parent = nullptr);

    // ── Loading ──────────────────────────────────────────────────────────
    void loadScript(const QString &filePath);
    void loadText(const QString &text);
    void clear();

    // ── Execution marker ─────────────────────────────────────────────────
    void setCurrentLine(int lineNo);

    // ── Editor configuration ──────────────────────────────────────────────
    void setEditorFont(const QFont &font);
    void enableHighlighting(bool on);
    void setReadOnly(bool ro);

    // ── Persistence ───────────────────────────────────────────────────────
    bool save();               // save to currentFile(); returns false on error
    bool saveAs();             // open dialog, then save
    bool isModified() const;

    // ── Accessors ─────────────────────────────────────────────────────────
    QString currentFile() const { return m_currentFile; }

signals:
    void modificationChanged(bool modified);   // forwarded from QTextDocument

private slots:
    void onModificationChanged(bool modified);

private:
    void updateInfo();
    bool writeFile(const QString &path);

    QLabel      *m_titleLabel;
    QLabel      *m_infoLabel;
    CodeEditor  *m_editor;
    QString      m_currentFile;
    int          m_currentLine = 0;
};
