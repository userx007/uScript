#include "ScriptViewer.hpp"
#include "ScriptHighlighter.hpp"
#include "CommScriptHighlighter.hpp"

#include <QVBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
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

    // Detect clicks on PLUGIN.SCRIPT lines
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
    int digits = 1, max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 18;
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

        if (m_highlightedLine > 0) {
            QTextBlock block = document()->findBlockByNumber(m_highlightedLine - 1);
            if (block.isValid() && block.isVisible()) {
                const QRectF blockRect =
                    blockBoundingGeometry(block).translated(contentOffset());
                auto *pev = static_cast<QPaintEvent *>(ev);
                if (blockRect.intersects(pev->rect())) {
                    QPainter p(viewport());
                    p.fillRect(QRectF(0, blockRect.top(),
                                     viewport()->width(), blockRect.height()),
                               QColor(0xff, 0x6e, 0xff, 80));
                }
            }
        }
        return true;   // event handled — do not call the default viewport handler again
    }
    return QPlainTextEdit::eventFilter(obj, ev);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *ev)
{
    // Colours kept in sync with LogViewer's inline line-number style:
    //   numbers  → dim slate  #4b5263  (unobtrusive)
    //   separator→ #3b4048  (slightly lighter │ bar)
    //   active   → #ff6eff  (magenta, execution marker)
    static const QColor C_BG     { 0x0d, 0x0f, 0x14 };
    static const QColor C_NUM    { 0x4b, 0x52, 0x63 };   // dim slate
    static const QColor C_SEP    { 0x3b, 0x40, 0x48 };   // separator │
    static const QColor C_ACTIVE { 0xff, 0x6e, 0xff };   // magenta execution line

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

            // ▶ execution arrow (active line only)
            if (isCurrent) {
                painter.setPen(C_ACTIVE);
                painter.drawText(2, top, 14, lineH, Qt::AlignLeft | Qt::AlignVCenter, "▶");
            }

            // Line number — dim normally, magenta on active line
            painter.setPen(isCurrent ? C_ACTIVE : C_NUM);
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


void CodeEditor::checkCurrentLineForCommScript()
{
    const int currentLine = textCursor().blockNumber();

    // Fire only once per line — suppress repeated signals from cursor
    // movement within the same block (click-drag, shift-arrows, etc.).
    if (currentLine == m_lastCommScriptLine) return;
    m_lastCommScriptLine = currentLine;

    const QString line = textCursor().block().text();

    // Pattern 1: PLUGIN.SCRIPT <filename>   — "SCRIPT" must be uppercase
    //   e.g.  CP2112.SCRIPT cp2112_i2c.txt
    static const QRegularExpression scriptCmd(
        R"(\b[A-Z][A-Z0-9_]*\.SCRIPT\s+(\S+))"  // case-sensitive (no flag)
    );

    // Pattern 2: PLUGIN.COMMAND script <filename>  — "script" must be lowercase
    //   e.g.  BUSPIRATE.I2C script ssd_1306bp.txt
    static const QRegularExpression scriptArg(
        R"(\b[A-Z][A-Z0-9_]*\.[A-Z][A-Z0-9_]*\s+script\s+(\S+))"  // case-sensitive
    );

    QRegularExpressionMatch m = scriptCmd.match(line);
    if (!m.hasMatch()) m = scriptArg.match(line);

    if (m.hasMatch())
        emit commScriptLineClicked(m.captured(1));
}

void CodeEditor::setHighlighting(bool on)
{
    if (on && !m_highlighter) {
        m_highlighter = new ScriptHighlighter(document());
    } else if (!on && m_highlighter) {
        delete m_highlighter;
        m_highlighter = nullptr;
    }
}

void CodeEditor::setCommHighlighting(bool on)
{
    // Remove main-script highlighter if present
    if (m_highlighter) { delete m_highlighter; m_highlighter = nullptr; }
    if (on && !m_commHighlighter) {
        m_commHighlighter = new CommScriptHighlighter(document());
    } else if (!on && m_commHighlighter) {
        delete m_commHighlighter;
        m_commHighlighter = nullptr;
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
ScriptViewer::ScriptViewer(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    Q_UNUSED(title)
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

    root->addWidget(m_editor, 1);
}

// ── Loading ────────────────────────────────────────────────────────────────
void ScriptViewer::loadScript(const QString &filePath)
{
    m_currentFile = filePath;
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
    m_editor->clearHighlight();
    m_currentLine = 0;
    updateInfo();
}

void ScriptViewer::loadText(const QString &text)
{
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    m_editor->clearHighlight();
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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save failed",
                             QString("Could not write to:\n%1\n\n%2")
                             .arg(path, f.errorString()));
        return false;
    }
    QTextStream ts(&f);
    ts << m_editor->toPlainText();
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
