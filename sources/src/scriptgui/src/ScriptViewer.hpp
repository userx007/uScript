#pragma once
#include <QWidget>
#include <QSyntaxHighlighter>
#include <QPlainTextEdit>
#include <QFrame>
#include <QString>
#include <QColor>
#include <QKeyEvent>

class LineNumberArea;
class ScriptHighlighter;
class CommScriptHighlighter;

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
    void setCommHighlighting(bool on);

    // Gutter (called by LineNumberArea)
    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *ev);

signals:
    void commScriptLineClicked(const QString &scriptName);

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev)  override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void checkCurrentLineForCommScript();  // fires on every cursor move

private:
    LineNumberArea    *m_lineNumberArea;
    int                m_highlightedLine = 0;
    ScriptHighlighter *m_highlighter     = nullptr;
    QSyntaxHighlighter *m_commHighlighter = nullptr;
    int                m_lastCommScriptLine = -1;  // guard: only emit once per line
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
    void    setCurrentLine(int lineNo);
    QString lineText(int lineNo) const;   // 1-based; empty string if out of range
    int     lineCount() const;            // total number of lines in the document

    // ── Editor configuration ──────────────────────────────────────────────
    void setEditorFont(const QFont &font);
    void enableHighlighting(bool on);          // use ScriptHighlighter
    void enableCommHighlighting(bool on);      // use CommScriptHighlighter
    void setReadOnly(bool ro);

    // ── Persistence ───────────────────────────────────────────────────────
    bool save();               // save to currentFile(); returns false on error
    bool saveAs();             // open dialog, then save
    bool isModified() const;

    // ── Accessors ─────────────────────────────────────────────────────────
    QString currentFile() const { return m_currentFile; }

    // ── Highlight ─────────────────────────────────────────────────────────
    void clearHighlight();


signals:
    void modificationChanged(bool modified);   // forwarded from QTextDocument
    void commScriptRequested(const QString &scriptName);  // user clicked a .SCRIPT line
    void infoChanged(const QString &info);     // filename + current line text for external display

private slots:
    void onModificationChanged(bool modified);
    void onCommScriptLineClicked(const QString &scriptName);

private:
    void updateInfo();
    bool writeFile(const QString &path);

    CodeEditor  *m_editor;
    QString      m_currentFile;
    int          m_currentLine = 0;
};
