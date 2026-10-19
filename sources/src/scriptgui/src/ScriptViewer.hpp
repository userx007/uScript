#pragma once
#include <QColor>
#include <QFrame>
#include <QKeyEvent>
#include <QList>
#include <QPlainTextEdit>
#include <QSet>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextEdit>
#include <QWidget>
#include <Qt>

class LineNumberArea;
class QCheckBox;
class QLabel;
class QLineEdit;
class QEvent;
class QFont;
class QObject;
class QRect;
class QSyntaxHighlighter;
class QWidget;
class ScriptHighlighter;

class CommScriptHighlighter;
class QMouseEvent;

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

        explicit CodeEditor(QWidget *pParent = nullptr);

        void highlightLine(int iLineNo); // 1-based; 0 = clear
        void clearHighlight();
        /** Switches the script highlighter on or off. */
        void setHighlighting(bool bOn);
        /** Switches the COMM-script highlighter on or off. */
        void setCommHighlighting(bool bOn);
        void setIniHighlighting(bool bOn); // switch to INI highlighter (clears others)

        // Error markers (validation phase) — red bar(s), independent of exec bar
        void setErrorLine(int iLineNo); // 1-based; accumulates (call once per error line)
        void clearErrorLines();         // clear all error markers

        /** Returns whether any error marker is set. */
        bool hasErrorLines() const
        {
            return !m_errorLines.isEmpty();
        }

        // Thread-active markers — bright-green rectangle outline while a & thread runs
        void addThreadLine(int iLineNo);    // 1-based; draw rectangle until removed
        void removeThreadLine(int iLineNo); // remove rectangle when thread joins
        void clearThreadLines();            // clear all (called on script finish)

        /** Returns whether the script highlighter is active. */
        bool hasScriptHighlighter() const
        {
            return m_highlighter != nullptr;
        }

        /** Returns whether the INI highlighter is active. */
        bool hasIniHighlighter() const
        {
            return m_iniHighlighter != nullptr;
        }

        // Reset the "already emitted for this line" guard so the next cursor move
        // onto a COMM-script or INCLUDE line re-emits the click signal even when
        // the line number hasn't changed (e.g. after loading a new file).
        void resetCommScriptLineCache()
        {
            m_lastCommScriptLine = -1;
        }

        // Gutter (called by LineNumberArea)
        int lineNumberAreaWidth() const;
        /** Paints the line-number gutter. */
        void lineNumberAreaPaintEvent(QPaintEvent *pEv);

        // Call after a font change to recalculate gutter width and repaint.
        void refreshGutter();

        // Word-occurrence highlighting — Ctrl+double-click a word to highlight
        // every occurrence of it in the document (like Qt Creator / VS Code).
        // Cleared automatically on plain click, Escape, or any text edit.
        void clearWordHighlights();

    signals:
        /** Emitted when a line naming COMM script @p scriptName is clicked. */
        void commScriptLineClicked(const QString &scriptName);

        // Emitted when the cursor lands on an  INCLUDE "path"  line.
        // The raw (unresolved) path string extracted from the directive is passed;
        // ScriptViewer::onIncludeFileRequested() resolves it to an absolute path
        // before re-emitting includeFileRequested to MainWindow.
        void includeFileClicked(const QString &rawPath);

    protected:
        /** Keeps the gutter geometry in sync with the editor. */
        void resizeEvent(QResizeEvent *pEv) override;
        void keyPressEvent(QKeyEvent *pEv) override;
        void mousePressEvent(QMouseEvent *pEv) override;
        /** Selects the word and highlights its other occurrences. */
        void mouseDoubleClickEvent(QMouseEvent *pEv) override;
        bool eventFilter(QObject *pObj, QEvent *pEv) override;

    private slots:
        /** Reserves the viewport margin for the line-number gutter. */
        void updateLineNumberAreaWidth(int iNewBlockCount);
        /** Scrolls or repaints the gutter after the editor changed. */
        void updateLineNumberArea(const QRect &rect, int iDy);
        void checkCurrentLineForCommScript(); // fires on every cursor move

    private:
        void highlightOccurrences(const QString &word); // populate m_wordHighlights

        LineNumberArea *m_lineNumberArea;
        int m_highlightedLine = 0;
        QSet<int> m_errorLines;  // validation-error lines (red bar)
        QSet<int> m_threadLines; // active & thread lines (green rectangle)
        ScriptHighlighter *m_highlighter      = nullptr;
        QSyntaxHighlighter *m_commHighlighter = nullptr;
        QSyntaxHighlighter *m_iniHighlighter  = nullptr;
        int m_lastCommScriptLine              = -1; // guard: only emit once per line

        // Ctrl+double-click word-occurrence highlighting (rendered via Qt's
        // built-in ExtraSelection mechanism, so it composes for free with normal
        // text selection and doesn't need any custom painting).
        QList<QTextEdit::ExtraSelection> m_wordHighlights;
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
        explicit ScriptViewer(QWidget *pParent = nullptr);

        // ── Loading ──────────────────────────────────────────────────────────
        void loadScript(const QString &filePath);
        /** Replaces the editor content and resets modification and highlights. */
        void loadText(const QString &text);
        /** Clears the editor and the current file association. */
        void clear();

        // ── Execution marker ─────────────────────────────────────────────────
        void setCurrentLine(int iLineNo);
        QString lineText(int iLineNo) const; // 1-based; empty string if out of range
        int lineCount() const;               // total number of lines in the document

        // ── Editor configuration ──────────────────────────────────────────────
        void setEditorFont(const QFont &font);
        void enableHighlighting(bool bOn);     // use ScriptHighlighter
        void enableCommHighlighting(bool bOn); // use CommScriptHighlighter
        /** Makes the editor read-only or editable. */
        void setReadOnly(bool bRo);

        // ── Persistence ───────────────────────────────────────────────────────
        bool save();   // save to currentFile(); returns false on error
        bool saveAs(); // open dialog, then save
        bool isModified() const;

        // ── Accessors ─────────────────────────────────────────────────────────
        QString currentFile() const
        {
            return m_currentFile;
        }

        /** Returns whether the current file has an .ini extension. */
        bool isIniFile() const
        {
            return m_currentFile.endsWith(".ini", Qt::CaseInsensitive);
        }

        // ── Highlight ─────────────────────────────────────────────────────────
        void clearHighlight();

        // Error markers (validation phase) — red bar(s), independent of exec bar
        void setErrorLine(int iLineNo); // 1-based; accumulates
        void clearErrorLines();         // clear all error markers
        bool hasErrorLines() const;     // true if any error markers are set

        // Thread markers — outline rectangle shown while a '&' thread is running
        void addThreadLine(int iLineNo);    // start showing rectangle on lineNo
        void removeThreadLine(int iLineNo); // remove it (thread joined)
        void clearThreadLines();            // remove all (script reset / new load)

    signals:
        void modificationChanged(bool bModified);            // forwarded from QTextDocument
        void saved(const QString &filePath);                 // emitted after a successful write to disk
        void commScriptRequested(const QString &scriptName); // user clicked a .SCRIPT line

        // Emitted when the cursor lands on an INCLUDE "path" line.
        // resolvedPath is already an absolute filesystem path (resolved by
        // onIncludeFileClicked relative to the current script's directory).
        // MainWindow connects this to onIncludeFileRequested to open the file in
        // a new core-script tab (or switch to an already-open one).
        void includeFileRequested(const QString &resolvedPath);

        void infoChanged(const QString &info); // filename + current line text for external display

    private slots:
        /** Refreshes the info text and forwards the modification state. */
        void onModificationChanged(bool bModified);
        void onCommScriptLineClicked(const QString &scriptName);

        // Resolves rawPath relative to m_currentFile's directory and re-emits
        // includeFileRequested with the resulting absolute path.
        void onIncludeFileClicked(const QString &rawPath);

    private:
        void updateInfo();
        /** Writes the content to @p path atomically; shows a message box and returns false on failure. */
        bool writeFile(const QString &path);

        // ── Minimal Find / Replace (Ctrl+F / Ctrl+H) ──────────────────────────
        /** Shows the find bar, pre-filled with the selection, and focuses the right field. */
        void showFindBar(bool bFocusReplace);
        void hideFindBar();
        /** Selects the next/previous match (wrapping around); false if there is none. */
        bool findNext(bool bBackward);
        /** Replaces the current match (if the selection is one) and moves to the next. */
        void replaceOne();
        /** Replaces every match as one undo step. */
        void replaceAll();

        QWidget *m_findBar       = nullptr;
        QLineEdit *m_findEdit    = nullptr;
        QLineEdit *m_replaceEdit = nullptr;
        QCheckBox *m_caseChk     = nullptr;
        QLabel *m_findStatus     = nullptr;
        QWidget *m_replaceWidgets = nullptr; // replace field + buttons (disabled when read-only)

        CodeEditor *m_editor;
        QString m_currentFile;
        int m_currentLine = 0;
};
