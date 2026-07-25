#include "ScriptViewer.hpp"
#include "ScriptHighlighter.hpp"
#include "CommScriptHighlighter.hpp"
#include "IniHighlighter.hpp"
#include "uSharedScriptRegex.hpp"

#include <QVBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QFileDialog>
#include <QTextStream>
#include <QTextBlock>
#include <QTextEdit>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QAbstractTextDocumentLayout>
#include <QMessageBox>

// ─────────────────────────────────────────────────────────────────────────────
//  LineNumberArea
// ─────────────────────────────────────────────────────────────────────────────
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }
protected:
    void paintEvent(QPaintEvent *ev) override { m_editor->lineNumberAreaPaintEvent(ev); }
private:
    CodeEditor *m_editor;
};

// ─────────────────────────────────────────────────────────────────────────────
//  CodeEditor
// ─────────────────────────────────────────────────────────────────────────────
CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName("scriptView");
    setReadOnly(false);           // editable by default for main script tabs
    setLineWrapMode(QPlainTextEdit::NoWrap);

    // Use spaces for indentation — never insert real tabs
    setTabStopDistance(TAB_WIDTH * fontMetrics().horizontalAdvance(' '));

    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &CodeEditor::updateLineNumberArea);

    // Detect clicks on PLUGIN.SCRIPT lines and INCLUDE "file" lines
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &CodeEditor::checkCurrentLineForCommScript);

    updateLineNumberAreaWidth(0);

    // Install event filter on the viewport so we can draw the execution band
    // on top of the text in eventFilter().  The viewport is the child widget
    // where QPlainTextEdit actually renders text — paintEvent on CodeEditor
    // itself paints on the frame and is overwritten by the viewport.
    viewport()->installEventFilter(this);

    m_highlighter = new ScriptHighlighter(document());
}

// ── Gutter ────────────────────────────────────────────────────────────────
int CodeEditor::lineNumberAreaWidth() const
{
    // Fixed 5-char field — matches LogViewer's 5-digit line-number gutter.
    return 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * 5 + 18;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::refreshGutter()
{
    // Recalculate gutter width with the new font metrics and force a repaint.
    // Must be called after setStyleSheet() changes the font size, because
    // QSS font changes don't trigger blockCountChanged (the signal that normally
    // drives updateLineNumberAreaWidth).
    updateLineNumberAreaWidth(0);
    m_lineNumberArea->update();
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) m_lineNumberArea->scroll(0, dy);
    else    m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *ev)
{
    QPlainTextEdit::resizeEvent(ev);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height());
}

bool CodeEditor::eventFilter(QObject *obj, QEvent *ev)
{
    // Intercept paint events on the viewport child widget so we can draw the
    // execution band on top of the text.  CodeEditor::paintEvent() would paint
    // on the frame widget, NOT on the viewport where the text lives — the
    // viewport is a separate child that repaints independently and would
    // overwrite anything drawn on the frame.
    if (obj == viewport() && ev->type() == QEvent::Paint) {
        // Let QPlainTextEdit paint the text first via the normal event path.
        QPlainTextEdit::paintEvent(static_cast<QPaintEvent *>(ev));

        if (m_highlightedLine > 0 || !m_errorLines.isEmpty() || !m_threadLines.isEmpty()) {
            auto *pev = static_cast<QPaintEvent *>(ev);
            QPainter p(viewport());
            if (m_highlightedLine > 0) {
                QTextBlock block = document()->findBlockByNumber(m_highlightedLine - 1);
                if (block.isValid() && block.isVisible()) {
                    const QRectF blockRect =
                        blockBoundingGeometry(block).translated(contentOffset());
                    if (blockRect.intersects(pev->rect())) {
                        p.fillRect(QRectF(0, blockRect.top(),
                                         viewport()->width(), blockRect.height()),
                                   QColor(0xff, 0x6e, 0xff, 80));
                    }
                }
            }
            // Red bars for validation-error lines (drawn on top of the exec bar
            // if they ever coincide, but in practice validation stops execution).
            for (int errLine : std::as_const(m_errorLines)) {
                QTextBlock block = document()->findBlockByNumber(errLine - 1);
                if (!block.isValid() || !block.isVisible()) continue;
                const QRectF blockRect =
                    blockBoundingGeometry(block).translated(contentOffset());
                if (blockRect.intersects(pev->rect()))
                    p.fillRect(QRectF(0, blockRect.top(),
                                     viewport()->width(), blockRect.height()),
                               QColor(0xff, 0x55, 0x55, 90));
            }
            // Bright-green outline rectangle for active '&' thread lines.
            // Drawn last so it sits on top of any fill underneath.
            // The rectangle persists until GUI:THREAD_DONE:<lineNo> arrives.
            if (!m_threadLines.isEmpty()) {
	            static const QColor C_THREAD_RECT { 0x50, 0xfa, 0x7b };  // #50fa7b bright-green
	            p.setPen(QPen(C_THREAD_RECT, 1));
	            p.setBrush(Qt::NoBrush);
	            for (int thrLine : std::as_const(m_threadLines)) {
	                QTextBlock block = document()->findBlockByNumber(thrLine - 1);
	                if (!block.isValid() || !block.isVisible()) continue;
	                const QRectF blockRect =
	                    blockBoundingGeometry(block).translated(contentOffset());
                    if (!blockRect.intersects(pev->rect())) continue;
                    // Inset by 1 px so the full border is visible without clipping.
                    p.drawRect(blockRect.adjusted(1, 1, -1, -1));
				}
            }
        }
        return true;   // event handled — do not call the default viewport handler again
    }
    return QPlainTextEdit::eventFilter(obj, ev);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *ev)
{
    // Colours and field width kept in sync with LogViewer's inline line-number style:
    //   field    → fixed 5-char wide, right-aligned
    //   numbers  → dim slate  #4b5263  (unobtrusive)
    //   separator→ #3b4048  (slightly lighter │ bar)
    //   active   → #ff6eff  (magenta, execution marker)
    static const QColor C_BG     	{ 0x0d, 0x0f, 0x14 };
    static const QColor C_NUM    	{ 0x4b, 0x52, 0x63 };   // dim slate
    static const QColor C_SEP    	{ 0x3b, 0x40, 0x48 };   // separator │
    static const QColor C_ACTIVE 	{ 0xff, 0x6e, 0xff };   // magenta execution line
    static const QColor C_ERROR  	{ 0xff, 0x55, 0x55 };   // red validation-error line
    static const QColor C_THREAD 	{ 0x50, 0xfa, 0x7b };   // bright-green active thread
    static const QColor C_THREAD_NUM{ 0x50, 0xfa, 0x7b };

    QPainter painter(m_lineNumberArea);
    // Use the editor's font (as resolved by QSS) so the gutter tracks Ctrl+/-
    // font-size changes.  Without this the painter defaults to LineNumberArea's
    // inherited font, which doesn't pick up stylesheet overrides from the parent.
    painter.setFont(font());
    painter.fillRect(ev->rect(), C_BG);

    QTextBlock block    = firstVisibleBlock();
    int        blockNum = block.blockNumber();
    int        top      = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int        bottom   = top + qRound(blockBoundingRect(block).height());
    const int  lineH    = fontMetrics().height();
    const int  gutterW  = m_lineNumberArea->width();
    // Reserve the rightmost ~10 px for the "│" separator character.
    const int  numRight = gutterW - 12;

    while (block.isValid() && top <= ev->rect().bottom()) {
        if (block.isVisible() && bottom >= ev->rect().top()) {
            const int  lineNo    = blockNum + 1;
            const bool isCurrent = (lineNo == m_highlightedLine);
            const bool isError   = m_errorLines.contains(lineNo);
            const bool isThread  = m_threadLines.contains(lineNo);

            // ▶ execution arrow (active line only)
            if (isCurrent) {
                painter.setPen(C_ACTIVE);
                painter.drawText(2, top, 14, lineH, Qt::AlignLeft | Qt::AlignVCenter, "▶");
            }
            // ✕ error marker (validation-error lines, not overridden by exec arrow)
            else if (isError) {
                painter.setPen(C_ERROR);
                painter.drawText(2, top, 14, lineH, Qt::AlignLeft | Qt::AlignVCenter, "✕");
            }
            // ⟳ thread-running marker (active '&' thread, not overridden by error/exec)
            else if (isThread) {
                painter.setPen(C_THREAD);
                painter.drawText(2, top, 14, lineH, Qt::AlignLeft | Qt::AlignVCenter, "⟳");
            }

            // Line number — magenta on active, red on error, green on thread, dim otherwise
            painter.setPen(isCurrent ? C_ACTIVE : (isError ? C_ERROR : (isThread ? C_THREAD_NUM : C_NUM)));
            painter.drawText(16, top, numRight - 16, lineH,
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(lineNo));

            // │ separator
            painter.setPen(C_SEP);
            painter.drawText(numRight, top, 10, lineH,
                             Qt::AlignCenter | Qt::AlignVCenter, "│");
        }
        block  = block.next();
        top    = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNum;
    }
}

// ── Execution highlight ────────────────────────────────────────────────────
void CodeEditor::highlightLine(int lineNo)
{
    m_highlightedLine = lineNo;

    if (lineNo <= 0) {
        viewport()->repaint();
        m_lineNumberArea->repaint();   // was update() — now consistent
        return;
    }

    QTextBlock block = document()->findBlockByLineNumber(lineNo - 1);
    if (!block.isValid()) {
        viewport()->repaint();
        m_lineNumberArea->repaint();   // was update() — now consistent
        return;
    }

    // Only scroll when the target block is outside the visible viewport.
    // Skipping setTextCursor/centerCursor for already-visible lines avoids:
    //   - moving the user's cursor while they are reading/navigating
    //   - the internal deferred update() calls those functions schedule,
    //     which interleave with our explicit repaint() and cause artifacts
    //   - constant scroll jitter when executing sequential lines in view
    const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
    if (!viewport()->rect().contains(blockRect.toRect())) {
        QTextCursor nav(block);
        nav.clearSelection();
        setTextCursor(nav);
        centerCursor();
    }

    viewport()->repaint();
    m_lineNumberArea->repaint();
}

void CodeEditor::clearHighlight()
{
    m_highlightedLine = 0;
    viewport()->repaint();
    m_lineNumberArea->repaint();
}

// ── Validation-error highlights (red) ────────────────────────────────────────
void CodeEditor::setErrorLine(int lineNo)
{
    if (lineNo <= 0) return;
    m_errorLines.insert(lineNo);
    viewport()->repaint();
    m_lineNumberArea->repaint();
}

void CodeEditor::clearErrorLines()
{
    if (m_errorLines.isEmpty()) return;
    m_errorLines.clear();
    viewport()->repaint();
    m_lineNumberArea->repaint();
}

// ── Thread-active markers (bright-green rectangle outline) ─────────────────
void CodeEditor::addThreadLine(int lineNo)
{
    if (lineNo <= 0) return;
    m_threadLines.insert(lineNo);
    viewport()->repaint();
    m_lineNumberArea->repaint();
}

void CodeEditor::removeThreadLine(int lineNo)
{
    if (!m_threadLines.remove(lineNo)) return;
    viewport()->repaint();
    m_lineNumberArea->repaint();
}

void CodeEditor::clearThreadLines()
{
    if (m_threadLines.isEmpty()) return;
    m_threadLines.clear();
    viewport()->repaint();
    m_lineNumberArea->repaint();
}


void CodeEditor::checkCurrentLineForCommScript()
{
    const int currentLine = textCursor().blockNumber();

    // Fire only once per line — suppress repeated signals from cursor
    // movement within the same block (click-drag, shift-arrows, etc.).
    if (currentLine == m_lastCommScriptLine) return;
    m_lastCommScriptLine = currentLine;

    const QString line = textCursor().block().text();

    // ── Pattern 1: INCLUDE "path"  ───────────────────────────────────────
    // Recognised by the reader as a pre-IR directive. The keyword comes from
    // SCRIPT_INCLUDE_KEYWORD (uSharedConfig.hpp) so the GUI stays in sync
    // with the reader and the highlighter if the keyword is ever renamed.
    // Clicking an INCLUDE line opens the named file in a new core-script tab
    // (or switches to it when already open), just like .SCRIPT does for comm
    // scripts — but routed through includeFileClicked → includeFileRequested
    // → MainWindow::onIncludeFileRequested so it lands in the main tab widget.
    {
        const QString kw = QString::fromLatin1("INCLUDE");
        const QRegularExpression includeRe(
            QString(R"re(^\s*%1\s+"([^"]+)")re").arg(kw)
        );
        const QRegularExpressionMatch im = includeRe.match(line);
        if (im.hasMatch()) {
            emit includeFileClicked(im.captured(1));
            return;   // don't fall through to comm-script patterns
        }
    }

    // ── Pattern 2: PLUGIN.SCRIPT <filename>   — "SCRIPT" must be uppercase
    //   e.g.  CP2112.SCRIPT cp2112_i2c.txt   or   UART:1.SCRIPT uart.txt
    static const QRegularExpression scriptCmd(
        QString("\\b" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX "\\.SCRIPT\\s+(\\S+)")  // case-sensitive (no flag)
    );

    // ── Pattern 3: PLUGIN.COMMAND script <filename>  — "script" must be lowercase
    //   e.g.  BUSPIRATE.I2C script ssd_1306bp.txt   or   UART:1.I2C script f.txt
    static const QRegularExpression scriptArg(
        QString("\\b" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX "\\.(" SCRIPT_RX_UPPER_IDENT ")\\s+script\\s+(\\S+)")  // case-sensitive
    );

    QRegularExpressionMatch m = scriptCmd.match(line);
    if (!m.hasMatch()) m = scriptArg.match(line);

    if (m.hasMatch())
        emit commScriptLineClicked(m.captured(m.regularExpression() == scriptCmd ? 1 : 2));
}

void CodeEditor::setHighlighting(bool on)
{
    // Tear down the other two highlighter types defensively, same as
    // setIniHighlighting() already does — only one highlighter may be
    // attached to the document at a time. Without this, calling
    // setHighlighting(true) while the comm or INI highlighter happened to
    // still be attached (i.e. outside the exact call sequence loadScript()
    // uses today) would leave two QSyntaxHighlighters wired to the same
    // QTextDocument simultaneously, double-applying (and fighting over)
    // formatting on every block.
    if (m_commHighlighter) { delete m_commHighlighter; m_commHighlighter = nullptr; }
    if (m_iniHighlighter)  { delete m_iniHighlighter;  m_iniHighlighter  = nullptr; }

    if (on && !m_highlighter) {
        m_highlighter = new ScriptHighlighter(document());
    } else if (!on && m_highlighter) {
        delete m_highlighter;
        m_highlighter = nullptr;
    }
}

void CodeEditor::setCommHighlighting(bool on)
{
    // Tear down the other two highlighter types defensively — see the
    // comment in setHighlighting() above; the same reasoning applies here.
    if (m_highlighter)    { delete m_highlighter;    m_highlighter    = nullptr; }
    if (m_iniHighlighter) { delete m_iniHighlighter; m_iniHighlighter = nullptr; }

    if (on && !m_commHighlighter) {
        m_commHighlighter = new CommScriptHighlighter(document());
    } else if (!on && m_commHighlighter) {
        delete m_commHighlighter;
        m_commHighlighter = nullptr;
    }
}

void CodeEditor::setIniHighlighting(bool on)
{
    // Remove all other highlighters — only one may be active at a time
    if (m_highlighter)    { delete m_highlighter;    m_highlighter    = nullptr; }
    if (m_commHighlighter){ delete m_commHighlighter; m_commHighlighter = nullptr; }
    if (on && !m_iniHighlighter) {
        m_iniHighlighter = new IniHighlighter(document());
    } else if (!on && m_iniHighlighter) {
        delete m_iniHighlighter;
        m_iniHighlighter = nullptr;
    }
}

// ── Keyboard handling ──────────────────────────────────────────────────────
void CodeEditor::keyPressEvent(QKeyEvent *ev)
{
    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(ev);
        return;
    }

    if (ev->key() == Qt::Key_Tab) {
        // Insert TAB_WIDTH spaces instead of a tab character
        QTextCursor cursor = textCursor();
        const int col      = cursor.positionInBlock();
        const int spaces   = TAB_WIDTH - (col % TAB_WIDTH);
        cursor.insertText(QString(spaces, QLatin1Char(' ')));
        return;
    }

    if (ev->key() == Qt::Key_Backtab) {
        // Shift+Tab: remove up to TAB_WIDTH leading spaces from selection / line
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        int start = cursor.selectionStart();
        int end   = cursor.selectionEnd();

        QTextBlock block = document()->findBlock(start);
        while (block.isValid() && block.position() <= end) {
            QString text = block.text();
            int remove = 0;
            for (int i = 0; i < TAB_WIDTH && i < text.length()
                            && text[i] == QLatin1Char(' '); ++i)
                ++remove;
            if (remove > 0) {
                QTextCursor bc(block);
                bc.movePosition(QTextCursor::StartOfBlock);
                bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
                bc.removeSelectedText();
            }
            block = block.next();
        }
        cursor.endEditBlock();
        return;
    }

    if (ev->key() == Qt::Key_Backspace && !ev->modifiers()) {
        // Smart backspace: if we're at a space-indent boundary, delete a full
        // indent level worth of spaces in one stroke.
        QTextCursor cursor = textCursor();
        if (!cursor.hasSelection()) {
            const QString lineText = cursor.block().text();
            const int col = cursor.positionInBlock();
            // Only act if everything to the left is spaces
            bool allSpaces = (col > 0);
            for (int i = 0; i < col && allSpaces; ++i)
                if (lineText[i] != QLatin1Char(' ')) allSpaces = false;

            if (allSpaces && col > 0) {
                const int del = ((col - 1) % TAB_WIDTH) + 1;
                cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, del);
                cursor.removeSelectedText();
                return;
            }
        }
    }

    QPlainTextEdit::keyPressEvent(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptViewer
// ─────────────────────────────────────────────────────────────────────────────
ScriptViewer::ScriptViewer(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("panelFrame");
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_editor = new CodeEditor(this);

    // Forward document modification signal
    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this,                 &ScriptViewer::onModificationChanged);

    // Forward comm-script click signal from CodeEditor
    connect(m_editor, &CodeEditor::commScriptLineClicked,
            this,     &ScriptViewer::onCommScriptLineClicked);

    // Forward INCLUDE-file click: resolve path then re-emit as includeFileRequested
    connect(m_editor, &CodeEditor::includeFileClicked,
            this,     &ScriptViewer::onIncludeFileClicked);

    root->addWidget(m_editor, 1);
}

// ── Loading ────────────────────────────────────────────────────────────────
void ScriptViewer::loadScript(const QString &filePath)
{
    m_currentFile = filePath;

    // ── swap highlighter only when the file type changes ─────────────────
    if (filePath.endsWith(".ini", Qt::CaseInsensitive)) {
        if (!m_editor->hasIniHighlighter())
            m_editor->setIniHighlighting(true);
    } else {
        if (!m_editor->hasScriptHighlighter()) {
            m_editor->setIniHighlighting(false);   // tears down INI if set
            m_editor->setHighlighting(true);
        }
    }

    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        // Block signals while loading so we don't get a spurious modificationChanged
        m_editor->setPlainText(ts.readAll());
        // Mark clean AFTER setPlainText so the highlighter runs first;
        // setModified(false) fires modificationChanged(false) → tab shows green.
        m_editor->document()->setModified(false);
    } else {
        m_editor->setPlainText(QString("-- could not open: %1 --").arg(filePath));
    }
    // Reset the per-line emission guard: after a new file is loaded the cursor
    // may land on the same line number as before, and the guard would silently
    // suppress the commScriptLineClicked signal.  Clearing it here ensures the
    // first click on any COMM-script line always shows the referenced file.
    m_editor->resetCommScriptLineCache();
    m_editor->clearHighlight();
    m_editor->clearErrorLines();
    m_editor->clearThreadLines();
    m_currentLine = 0;
    updateInfo();
}

void ScriptViewer::loadText(const QString &text)
{
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    m_editor->clearHighlight();
    m_editor->clearErrorLines();
    m_editor->clearThreadLines();
    m_editor->resetCommScriptLineCache();   // reset guard so next click on same line re-emits
    m_currentLine = 0;
    updateInfo();
}

void ScriptViewer::clear()
{
    m_currentFile.clear();
    m_currentLine = 0;
    // Use setPlainText("") instead of QPlainTextEdit::clear().
    // clear() replaces the internal QTextDocument with a brand-new instance,
    // which silently detaches the QSyntaxHighlighter (it still holds a pointer
    // to the old document).  setPlainText("") reuses the same document object,
    // keeping the highlighter attached and its m_highlightedLine state valid.
    m_editor->setPlainText(QString());
    m_editor->document()->setModified(false);
    m_editor->clearHighlight();
    m_editor->clearErrorLines();
    m_editor->clearThreadLines();
    m_editor->resetCommScriptLineCache();   // reset guard so next click on same line re-emits
    updateInfo();
}

// ── Execution marker ───────────────────────────────────────────────────────
QString ScriptViewer::lineText(int lineNo) const
{
    // lineNo is 1-based
    QTextBlock block = m_editor->document()->findBlockByLineNumber(lineNo - 1);
    return block.isValid() ? block.text() : QString{};
}

int ScriptViewer::lineCount() const
{
    return m_editor->document()->blockCount();
}

void ScriptViewer::setCurrentLine(int lineNo)
{
    m_currentLine = lineNo;
    m_editor->highlightLine(lineNo);
    updateInfo();
}

void ScriptViewer::clearHighlight()
{
    m_currentLine = 0;
    m_editor->clearHighlight();
    updateInfo();
}

void ScriptViewer::setErrorLine(int lineNo)
{
    m_editor->setErrorLine(lineNo);
}

void ScriptViewer::clearErrorLines()
{
    m_editor->clearErrorLines();
}

bool ScriptViewer::hasErrorLines() const
{
    return m_editor->hasErrorLines();
}

// ── Thread-active markers ──────────────────────────────────────────────────
void ScriptViewer::addThreadLine(int lineNo)
{
    m_editor->addThreadLine(lineNo);
}

void ScriptViewer::removeThreadLine(int lineNo)
{
    m_editor->removeThreadLine(lineNo);
}

void ScriptViewer::clearThreadLines()
{
    m_editor->clearThreadLines();
}

// ── Editor configuration ───────────────────────────────────────────────────
void ScriptViewer::setEditorFont(const QFont &font)
{
    // QFontInfo resolves the *actual* installed family Qt will use —
    // using font.family() (the requested name) risks a CSS miss when the
    // preferred font isn't installed, causing Qt to pick a proportional
    // fallback and making spaces look collapsed.
    const QFontInfo info(font);
    m_editor->setStyleSheet(QString(
        "QPlainTextEdit#scriptView {"
        "  font-family: '%1';"   // resolved name — guaranteed to exist
        "  font-size: %2pt;"
        "}"
    ).arg(info.family()).arg(font.pointSize()));
    m_editor->refreshGutter();
    m_editor->viewport()->update();
}

void ScriptViewer::enableHighlighting(bool on)
{
    m_editor->setHighlighting(on);
}

void ScriptViewer::enableCommHighlighting(bool on)
{
    m_editor->setCommHighlighting(on);
}

void ScriptViewer::setReadOnly(bool ro)
{
    m_editor->setReadOnly(ro);
}

// ── Persistence ────────────────────────────────────────────────────────────
bool ScriptViewer::isModified() const
{
    return m_editor->document()->isModified();
}

bool ScriptViewer::save()
{
    if (m_currentFile.isEmpty())
        return saveAs();
    return writeFile(m_currentFile);
}

bool ScriptViewer::saveAs()
{
    const QString start = m_currentFile.isEmpty()
                          ? QDir::homePath()
                          : QFileInfo(m_currentFile).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Script As", start,
        "Script files (*.txt *.scr *.script);;All files (*)");
    if (path.isEmpty()) return false;
    m_currentFile = path;
    return writeFile(path);
}

bool ScriptViewer::writeFile(const QString &path)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save failed",
                             QString("Could not write to:\n%1\n\n%2")
                             .arg(path, f.errorString()));
        return false;
    }
    QTextStream ts(&f);
    ts << m_editor->toPlainText();
    if (!f.commit()) {
        QMessageBox::warning(this, "Save failed",
                             QString("Could not commit:\n%1\n\n%2")
                             .arg(path, f.errorString()));
        return false;
    }
    m_editor->document()->setModified(false);
    updateInfo();
    return true;
}

// ── Modification tracking ──────────────────────────────────────────────────
void ScriptViewer::onCommScriptLineClicked(const QString &scriptName)
{
    // Re-emit so MainWindow can intercept; it knows the base directory
    emit commScriptRequested(scriptName);
}

void ScriptViewer::onIncludeFileClicked(const QString &rawPath)
{
    // Resolve relative to the directory of the currently loaded script file.
    // If no file is loaded, fall back to the process working directory —
    // same convention used by the reader's resolveIncludePath().
    const QString baseDir =
        m_currentFile.isEmpty()
            ? QDir::currentPath()
            : QFileInfo(m_currentFile).absolutePath();

    emit includeFileRequested(QDir(baseDir).filePath(rawPath));
}

void ScriptViewer::onModificationChanged(bool modified)
{
    updateInfo();
    emit modificationChanged(modified);
}

// ── Info label ─────────────────────────────────────────────────────────────
void ScriptViewer::updateInfo()
{
    QString info;
    if (!m_currentFile.isEmpty())
        info += QFileInfo(m_currentFile).fileName();
    if (m_currentLine > 0)
        info += QString("  :  ln %1").arg(m_currentLine);
    emit infoChanged(info);
}
