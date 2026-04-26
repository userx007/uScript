#include "ScriptViewer.hpp"
#include "ScriptHighlighter.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QTextBlock>
#include <QTextEdit>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QAbstractTextDocumentLayout>

// ─────────────────────────────────────────────────────────────────────────────
//  LineNumberArea – thin widget painted inside CodeEditor's viewport margin
// ─────────────────────────────────────────────────────────────────────────────
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override {
        return {m_editor->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent *ev) override {
        m_editor->lineNumberAreaPaintEvent(ev);
    }

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
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &CodeEditor::updateLineNumberArea);

    updateLineNumberAreaWidth(0);

    // Syntax highlighter — attached to the document so it survives setPlainText()
    m_highlighter = new ScriptHighlighter(document());
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max    = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    // 3 extra px padding each side + arrow column
    return 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 18;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *ev)
{
    QPlainTextEdit::resizeEvent(ev);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(cr.left(), cr.top(),
                                  lineNumberAreaWidth(), cr.height());
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *ev)
{
    QPainter painter(m_lineNumberArea);

    // Gutter background
    painter.fillRect(ev->rect(), QColor(0x0d, 0x0f, 0x14));

    // Right separator line
    painter.setPen(QColor(0x25, 0x2a, 0x35));
    painter.drawLine(m_lineNumberArea->width() - 1, ev->rect().top(),
                     m_lineNumberArea->width() - 1, ev->rect().bottom());

    QTextBlock block     = firstVisibleBlock();
    int        blockNum  = block.blockNumber();
    int        top       = qRound(blockBoundingGeometry(block)
                                  .translated(contentOffset()).top());
    int        bottom    = top + qRound(blockBoundingRect(block).height());
    const int  lineH     = fontMetrics().height();
    const int  gutterW   = m_lineNumberArea->width();

    while (block.isValid() && top <= ev->rect().bottom()) {
        if (block.isVisible() && bottom >= ev->rect().top()) {
            const int lineNumber = blockNum + 1;
            const bool isCurrent = (lineNumber == m_highlightedLine);

            if (isCurrent) {
                // Amber arrow indicator
                painter.setPen(QColor(0xff, 0xb8, 0x6c));
                painter.setFont(font());
                painter.drawText(2, top, 14, lineH,
                                 Qt::AlignLeft | Qt::AlignVCenter, "▶");
            }

            // Line number text
            QColor numColor = isCurrent
                              ? QColor(0xff, 0xb8, 0x6c)
                              : QColor(0x40, 0x48, 0x55);
            painter.setPen(numColor);
            painter.setFont(font());
            painter.drawText(16, top, gutterW - 20, lineH,
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(lineNumber));
        }
        block  = block.next();
        top    = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNum;
    }
}

void CodeEditor::highlightLine(int lineNo)
{
    m_highlightedLine = lineNo;

    // Amber band on the active line
    QTextEdit::ExtraSelection sel;
    if (lineNo > 0) {
        QTextBlock block = document()->findBlockByLineNumber(lineNo - 1);
        if (block.isValid()) {
            sel.cursor = QTextCursor(block);
            sel.format.setBackground(QColor(0xff, 0xb8, 0x6c, 45));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor.clearSelection();
            setExtraSelections({sel});

            // Auto-scroll so the execution line is visible (centred if possible)
            QTextCursor c(block);
            setTextCursor(c);
            centerCursor();
        } else {
            setExtraSelections({});
        }
    } else {
        setExtraSelections({});
    }

    // Repaint gutter for arrow update
    m_lineNumberArea->update();
}

void CodeEditor::clearHighlight()
{
    m_highlightedLine = 0;
    setExtraSelections({});
    m_lineNumberArea->update();
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

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptViewer
// ─────────────────────────────────────────────────────────────────────────────
ScriptViewer::ScriptViewer(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    setObjectName("panelFrame");
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar ────────────────────────────────────────────────────────
    auto *header = new QFrame(this);
    header->setObjectName("panelHeader");
    header->setFrameShape(QFrame::NoFrame);

    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(0);

    m_titleLabel = new QLabel(title.toUpper(), header);
    m_titleLabel->setObjectName("panelTitle");

    m_infoLabel = new QLabel("", header);
    m_infoLabel->setObjectName("panelInfo");
    m_infoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hlay->addWidget(m_titleLabel);
    hlay->addStretch();
    hlay->addWidget(m_infoLabel);

    // ── Code editor ───────────────────────────────────────────────────────
    m_editor = new CodeEditor(this);

    root->addWidget(header);
    root->addWidget(m_editor, 1);
}

void ScriptViewer::loadScript(const QString &filePath)
{
    m_currentFile = filePath;
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        loadText(ts.readAll());
    } else {
        loadText(QString("-- could not open: %1 --").arg(filePath));
    }
}

void ScriptViewer::loadText(const QString &text)
{
    m_editor->setPlainText(text);
    m_editor->clearHighlight();
    m_currentLine = 0;
    updateInfo();
}

void ScriptViewer::setCurrentLine(int lineNo)
{
    m_currentLine = lineNo;
    m_editor->highlightLine(lineNo);
    updateInfo();
}

void ScriptViewer::clear()
{
    m_currentFile.clear();
    m_currentLine = 0;
    m_editor->clear();
    m_editor->clearHighlight();
    updateInfo();
}

void ScriptViewer::setEditorFont(const QFont &font)
{
    // setFont() loses to any QSS font rule, even inherited ones from the
    // global QWidget{} rule.  Widget-level setStyleSheet() wins over the
    // application stylesheet, so we use that instead.
    m_editor->setStyleSheet(QString(
        "QPlainTextEdit#scriptView {"
        "  font-family: '%1', 'Cascadia Code', 'Consolas', monospace;"
        "  font-size: %2pt;"
        "}"
    ).arg(font.family()).arg(font.pointSize()));
    m_editor->viewport()->update();
}

void ScriptViewer::enableHighlighting(bool on)
{
    // ScriptHighlighter parents itself to the document — deleting it
    // removes it from Qt's object tree and unhooks it from the document.
    // We track it via a pointer on CodeEditor so we can query / delete it.
    m_editor->setHighlighting(on);
}

void ScriptViewer::updateInfo()
{
    QString info;
    if (!m_currentFile.isEmpty())
        info += QFileInfo(m_currentFile).fileName();
    if (m_currentLine > 0)
        info += QString("  :  ln %1").arg(m_currentLine);
    m_infoLabel->setText(info);
}
