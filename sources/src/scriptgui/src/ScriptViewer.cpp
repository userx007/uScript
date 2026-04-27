#include "ScriptViewer.hpp"
#include "ScriptHighlighter.hpp"
#include "CommScriptHighlighter.hpp"

#include <QVBoxLayout>
#include <QPainter>
#include <QScrollBar>
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

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *ev)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(ev->rect(), QColor(0x0d, 0x0f, 0x14));
    painter.setPen(QColor(0x25, 0x2a, 0x35));
    painter.drawLine(m_lineNumberArea->width() - 1, ev->rect().top(),
                     m_lineNumberArea->width() - 1, ev->rect().bottom());

    QTextBlock block    = firstVisibleBlock();
    int        blockNum = block.blockNumber();
    int        top      = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int        bottom   = top + qRound(blockBoundingRect(block).height());
    const int  lineH    = fontMetrics().height();
    const int  gutterW  = m_lineNumberArea->width();

    while (block.isValid() && top <= ev->rect().bottom()) {
        if (block.isVisible() && bottom >= ev->rect().top()) {
            const int  lineNo    = blockNum + 1;
            const bool isCurrent = (lineNo == m_highlightedLine);
            if (isCurrent) {
                painter.setPen(QColor(0xff, 0x6e, 0xff));
                painter.drawText(2, top, 14, lineH, Qt::AlignLeft | Qt::AlignVCenter, "▶");
            }
            painter.setPen(isCurrent ? QColor(0xff, 0x6e, 0xff) : QColor(0x40, 0x48, 0x55));
            painter.drawText(16, top, gutterW - 20, lineH,
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(lineNo));
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
        setExtraSelections({});
        m_lineNumberArea->update();
        return;
    }

    QTextBlock block = document()->findBlockByLineNumber(lineNo - 1);
    if (!block.isValid()) {
        setExtraSelections({});
        m_lineNumberArea->update();
        return;
    }

    // ── Scroll to the line FIRST, then apply ExtraSelections ────────────
    // setTextCursor() triggers an internal viewport update that would wipe
    // any ExtraSelections set before it, so cursor movement must come first.
    QTextCursor nav(block);
    nav.clearSelection();
    setTextCursor(nav);
    centerCursor();

    // ── Execution band (applied after cursor move so it is not wiped) ─────
    // FullWidthSelection paints a full-width background across the line even
    // beyond the end of text.  A second selection overlays white foreground
    // so code is readable against the magenta band.
    QTextEdit::ExtraSelection band;
    band.cursor = QTextCursor(block);
    band.cursor.clearSelection();
    band.format.setBackground(QColor(0xff, 0x6e, 0xff, 140));
    band.format.setProperty(QTextFormat::FullWidthSelection, true);

    QTextEdit::ExtraSelection textFg;
    textFg.cursor = QTextCursor(block);
    textFg.cursor.select(QTextCursor::LineUnderCursor);
    textFg.format.setForeground(QColor(0xff, 0xff, 0xff));

    setExtraSelections({band, textFg});
    viewport()->repaint();      // synchronous repaint — ensures band is visible immediately

    m_lineNumberArea->update();
}

void CodeEditor::clearHighlight()
{
    m_highlightedLine = 0;
    setExtraSelections({});
    m_lineNumberArea->update();
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
    m_editor->clear();
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

void ScriptViewer::setCurrentLine(int lineNo)
{
    m_currentLine = lineNo;
    m_editor->highlightLine(lineNo);
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
