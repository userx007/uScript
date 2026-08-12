#include "LogViewer.hpp"
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextBlock>
#include <QRegularExpression>
#include <QDateTime>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QPainter>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QMouseEvent>

// ─── colour palette (matches AppStyle dark theme) ────────────────────────────
static const QColor C_STATUS (0x4a, 0x9e, 0xff);   // blue  (internal status msgs)
static const QColor C_PLAIN  (0xab, 0xb2, 0xbf);   // grey  (bare / unrecognised)

// Gutter colours – match ScriptViewer / ShellTerminal palette exactly
static const QColor C_GUTTER_BG    (0x11, 0x13, 0x18);
static const QColor C_GUTTER_FG    (0x3E, 0x44, 0x51);
static const QColor C_GUTTER_BORDER(0x20, 0x22, 0x2A);

// Word-match highlight – muted amber tint readable over dark ANSI colours
static const QColor C_WORD_HIGHLIGHT       (0xf1, 0xfa, 0x8c,  60);   // Dracula yellow, low alpha
static const QColor C_WORD_HIGHLIGHT_BORDER(0xf1, 0xc4, 0x0f, 180);

// Saved-label stylesheet — used in three places; single source of truth.
static constexpr auto k_savedOkStyle =
    "color:#50fa7b;font-size:13px;"
    "font-family:'JetBrains Mono','Consolas',monospace;";
static constexpr auto k_savedErrStyle =
    "color:#ff5555;font-size:13px;"
    "font-family:'JetBrains Mono','Consolas',monospace;";

// ─────────────────────────────────────────────────────────────────────────────
//  ANSI SGR escape sequence → QTextCharFormat converter
// ─────────────────────────────────────────────────────────────────────────────

static QColor sgrCodeToColor(int code)
{
    switch (code) {
    case 30: return QColor(0x40, 0x48, 0x55);
    case 31: return QColor(0xff, 0x55, 0x55);
    case 32: return QColor(0x50, 0xfa, 0x7b);
    case 33: return QColor(0xf1, 0xfa, 0x8c);
    case 34: return QColor(0x4a, 0x9e, 0xff);
    case 35: return QColor(0xff, 0x79, 0xc6);
    case 36: return QColor(0x8b, 0xe9, 0xfd);
    case 37: return QColor(0xf8, 0xf8, 0xf2);
    case 90: return QColor(0x62, 0x72, 0xa4);
    case 91: return QColor(0xff, 0x6e, 0x6e);
    case 92: return QColor(0x69, 0xff, 0x94);
    case 93: return QColor(0xff, 0xff, 0xa5);
    case 94: return QColor(0xd6, 0xac, 0xff);
    case 95: return QColor(0xff, 0x92, 0xdf);
    case 96: return QColor(0xa4, 0xff, 0xff);
    case 97: return QColor(0xff, 0xff, 0xff);
    default: return {};
    }
}

// Decomposes an ANSI-coloured string into a list of (text, QTextCharFormat)
// segments.  Returns one segment per colour run.
struct Segment { QString text; QTextCharFormat fmt; };

static QList<Segment> ansiToSegments(const QString &input,
                                     const QTextCharFormat &base)
{
    static const QRegularExpression ansiRe("\x1b\\[([0-9;]*)m");

    QList<Segment> result;
    QTextCharFormat cur = base;
    int pos = 0;

    auto flush = [&](int end) {
        if (end > pos) {
            Segment s;
            s.text = input.mid(pos, end - pos);
            s.fmt  = cur;
            result.append(s);
        }
    };

    QRegularExpressionMatchIterator it = ansiRe.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        flush(m.capturedStart());
        pos = m.capturedEnd();

        const QStringList params = m.captured(1).isEmpty()
                                   ? QStringList{"0"}
                                   : m.captured(1).split(';', Qt::SkipEmptyParts);
        for (const QString &p : params) {
            const int code = p.toInt();
            if (code == 0) {
                cur = base;
            } else if (code == 1) {
                cur.setFontWeight(QFont::Bold);
            } else if (code == 22) {
                cur.setFontWeight(QFont::Normal);
            } else if (code == 3) {
                cur.setFontItalic(true);
            } else if (code == 23) {
                cur.setFontItalic(false);
            } else {
                const QColor c = sgrCodeToColor(code);
                if (c.isValid()) cur.setForeground(c);
            }
        }
    }
    flush(input.length());
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  LogLineNumberArea  –  thin companion widget painted by LogEdit
// ─────────────────────────────────────────────────────────────────────────────
class LogLineNumberArea : public QWidget
{
public:
    explicit LogLineNumberArea(LogEdit *editor)
        : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override
    { return { m_editor->lineNumberAreaWidth(), 0 }; }

protected:
    void paintEvent(QPaintEvent *ev) override
    { m_editor->lineNumberAreaPaintEvent(ev); }

private:
    LogEdit *m_editor;
};

// ─────────────────────────────────────────────────────────────────────────────
//  LogEdit
// ─────────────────────────────────────────────────────────────────────────────
LogEdit::LogEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName("logView");
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    document()->setMaximumBlockCount(10000);

    m_lineNumberArea = new LogLineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &LogEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &LogEdit::updateLineNumberArea);

    updateLineNumberAreaWidth(0);
}

// Fixed 5-digit field – matches CodeEditor / ShellTerminal gutter width.
int LogEdit::lineNumberAreaWidth() const
{
    return 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * 5 + 18;
}

void LogEdit::refreshGutter()
{
    updateLineNumberAreaWidth(0);
    m_lineNumberArea->update();
}

void LogEdit::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void LogEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) m_lineNumberArea->scroll(0, dy);
    else    m_lineNumberArea->update(0, rect.y(),
                                     m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void LogEdit::resizeEvent(QResizeEvent *ev)
{
    QPlainTextEdit::resizeEvent(ev);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(cr.left(), cr.top(),
                                  lineNumberAreaWidth(), cr.height());
}

void LogEdit::lineNumberAreaPaintEvent(QPaintEvent *ev)
{
    QPainter p(m_lineNumberArea);
    p.fillRect(ev->rect(), C_GUTTER_BG);

    // Right border / separator line
    const int bx = m_lineNumberArea->width() - 1;
    p.setPen(C_GUTTER_BORDER);
    p.drawLine(bx, ev->rect().top(), bx, ev->rect().bottom());

    // Iterate over visible blocks and draw their 1-based line number
    QTextBlock block     = firstVisibleBlock();
    int        blockNum  = block.blockNumber();
    int        top       = qRound(blockBoundingGeometry(block)
                                  .translated(contentOffset()).top());
    int        bottom    = top + qRound(blockBoundingRect(block).height());

    // Use a slightly smaller font, matching ShellTerminal's gutter style
    QFont gf = font();
    if (gf.pointSize() > 1) gf.setPointSize(gf.pointSize() - 1);
    p.setFont(gf);
    p.setPen(C_GUTTER_FG);

    const int lh = fontMetrics().height();

    while (block.isValid() && top <= ev->rect().bottom()) {
        if (block.isVisible() && bottom >= ev->rect().top()) {
            const QString num = QString::number(blockNum + 1);
            // Right-align, 4 px padding before separator
            p.drawText(0, top,
                       m_lineNumberArea->width() - 4 - 1, lh,
                       Qt::AlignRight | Qt::AlignVCenter, num);
        }
        block  = block.next();
        ++blockNum;
        top    = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
}

// ── Word-match highlight ──────────────────────────────────────────────────────
void LogEdit::applyWordHighlights(const QString &word)
{
    m_highlightedWord = word;
    QList<QTextEdit::ExtraSelection> selections;

    if (!word.isEmpty()) {
        // Whole-word, case-sensitive search
        const QRegularExpression re(
            "\\b" + QRegularExpression::escape(word) + "\\b",
            QRegularExpression::NoPatternOption);

        QTextCharFormat fmt;
        fmt.setBackground(C_WORD_HIGHLIGHT);
        fmt.setProperty(QTextFormat::OutlinePen,
                        QVariant::fromValue(QPen(C_WORD_HIGHLIGHT_BORDER, 1)));

        QTextDocument *doc = document();
        QTextCursor    hit = doc->find(re);
        while (!hit.isNull()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = hit;
            sel.format = fmt;
            selections.append(sel);
            hit = doc->find(re, hit);
        }
    }

    setExtraSelections(selections);
}

void LogEdit::clearWordHighlights()
{
    if (!m_highlightedWord.isEmpty()) {
        m_highlightedWord.clear();
        setExtraSelections({});
    }
}

void LogEdit::mouseDoubleClickEvent(QMouseEvent *ev)
{
    // Let the base class select the word first, then read it back.
    QPlainTextEdit::mouseDoubleClickEvent(ev);

    const QString word = textCursor().selectedText().trimmed();
    if (word.isEmpty() || word == m_highlightedWord) {
        // Second double-click on the same word → toggle off
        clearWordHighlights();
        return;
    }
    applyWordHighlights(word);
}

void LogEdit::mousePressEvent(QMouseEvent *ev)
{
    // A plain single-click clears highlights; if this becomes a double-click,
    // mouseDoubleClickEvent() will run immediately after and re-apply them.
    clearWordHighlights();
    QPlainTextEdit::mousePressEvent(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  LogViewer
// ─────────────────────────────────────────────────────────────────────────────
LogViewer::LogViewer(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("panelFrame");
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName("panelHeader");
    header->setFrameShape(QFrame::NoFrame);

    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_titleLabel = new QLabel("OUTPUT  LOG | Severity", header);
    m_titleLabel->setObjectName("panelTitle");

    // ── Log-level filter combo ────────────────────────────────────────────────
    // Adds -l <N> to the interpreter command line when anything other than
    // DEFAULT is selected.  The numeric value matches the LogLevel enum order:
    //   WERBOSE=0, VERBOSE=1, DEBUG=2, INFO=3, WARNING=4, ERROR=5, FATAL=6, FIXED=7.
    m_logLevelCb = new QComboBox(header);
    m_logLevelCb->setToolTip("Minimum log severity passed to the interpreter\n");
    for (const char *name : {"DEFAULT", "WERBOSE", "VERBOSE", "DEBUG", "INFO",
                              "WARNING", "ERROR",  "FATAL", "FIXED"}) {
        m_logLevelCb->addItem(QString::fromLatin1(name));
    }
    m_logLevelCb->setCurrentIndex(0);   // DEFAULT

    m_countLabel = new QLabel("", header);
    m_countLabel->setObjectName("panelInfo");

    m_autoScrollCb = new QCheckBox("auto-scroll", header);
    m_autoScrollCb->setChecked(true);
    m_autoScrollCb->setToolTip("Keep scrolled to the latest log line");

    m_clearBtn = new QPushButton("CLEAR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear log output");

    m_saveBtn = new QPushButton("SAVE", header);
    m_saveBtn->setObjectName("clearBtn");   // reuse same QSS
    m_saveBtn->setToolTip("Save log to log_<date>_<time>.log");
    m_saveBtn->setEnabled(false);           // nothing to save yet

    m_savedLabel = new QLabel("", header);
    m_savedLabel->setObjectName("panelInfo");
    m_savedLabel->setStyleSheet(k_savedOkStyle);

    hlay->addWidget(m_titleLabel);
    hlay->addWidget(m_logLevelCb);
    hlay->addSpacing(8);
    hlay->addWidget(m_savedLabel, 1);   // stretch=1 so it takes remaining space
    hlay->addWidget(m_autoScrollCb);
    hlay->addWidget(m_countLabel);
    hlay->addWidget(m_saveBtn);
    hlay->addWidget(m_clearBtn);

    m_logEdit = new LogEdit(this);

    root->addWidget(header);
    root->addWidget(m_logEdit, 1);

    connect(m_clearBtn,     &QPushButton::clicked,  this, &LogViewer::clear);
    connect(m_autoScrollCb, &QCheckBox::toggled,    this, &LogViewer::setAutoScroll);
    connect(m_saveBtn,      &QPushButton::clicked,  this, &LogViewer::saveLog);
}

void LogViewer::setLogFont(const QFont &font)
{
    // Apply via setFont() so QPlainTextEdit and the gutter both use the same
    // metrics.  Also update the gutter width since character width may change.
    m_logEdit->setFont(font);
    m_logEdit->refreshGutter();
}

// ─────────────────────────────────────────────────────────────────────────────
//  logLevelArg — numeric enum value for the selected severity, or -1 (DEFAULT)
// ─────────────────────────────────────────────────────────────────────────────
int LogViewer::logLevelArg() const
{
    // Combo items: index 0 = DEFAULT (no flag), indices 1-8 = WERBOSE…FIXED.
    // The numeric value passed to -l matches the LogLevel enum, so index 1
    // maps to 0 (WERBOSE), index 2 to 1 (VERBOSE), etc.
    const int idx = m_logLevelCb->currentIndex();
    return (idx <= 0) ? -1 : idx - 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  markDirty — enable the save button on first new content since last save/clear
// ─────────────────────────────────────────────────────────────────────────────
void LogViewer::markDirty()
{
    if (m_savedClean) {
        m_savedClean = false;
        m_saveBtn->setEnabled(true);
        m_savedLabel->setText("");
    }
}

// ── helpers ───────────────────────────────────────────────────────────────────
// Returns a cursor positioned at the end of the document, with a new block
// inserted unless the document is currently empty.
static QTextCursor cursorAtNewLine(QTextDocument *doc)
{
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    if (!doc->isEmpty())
        cursor.insertBlock();
    return cursor;
}

void LogViewer::appendLine(const QString &line)
{
    // Base format: default foreground colour, normal weight
    QTextCharFormat baseFmt;
    baseFmt.setForeground(C_PLAIN);

    // Decompose ANSI escape codes into (text, format) segments
    const QList<Segment> segments = ansiToSegments(line, baseFmt);

    // Guard: if every segment is empty (e.g. the line contained only ANSI
    // escape codes with no visible text), skip the block insertion entirely.
    // Without this guard, cursorAtNewLine() would insert an empty QTextDocument
    // block, which toPlainText() serialises as a blank line.
    const bool hasText = std::any_of(segments.cbegin(), segments.cend(),
                                     [](const Segment &s){ return !s.text.isEmpty(); });
    if (!hasText)
        return;

    ++m_lineCount;

    QTextCursor cursor = cursorAtNewLine(m_logEdit->document());

    for (const Segment &s : segments)
        cursor.insertText(s.text, s.fmt);

    // Read blockCount() AFTER insertion, not before — otherwise the label
    // always shows the count as of the previous line and permanently lags
    // by one.
    m_countLabel->setText(QString("%1 lines").arg(m_logEdit->document()->blockCount()));

    if (m_autoScroll)
        m_logEdit->verticalScrollBar()->setValue(
            m_logEdit->verticalScrollBar()->maximum());

    markDirty();
}

void LogViewer::appendStatus(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");

    QTextCharFormat fmt;
    fmt.setForeground(C_STATUS);
    fmt.setFontItalic(true);

    QTextCursor cursor = cursorAtNewLine(m_logEdit->document());
    cursor.insertText(QString("── %1  %2 ──").arg(ts, msg), fmt);

    if (m_autoScroll)
        m_logEdit->verticalScrollBar()->setValue(
            m_logEdit->verticalScrollBar()->maximum());

    markDirty();
}

void LogViewer::clear()
{
    m_logEdit->clear();
    m_lineCount  = 0;
    m_savedClean = true;
    m_saveBtn->setEnabled(false);
    m_savedLabel->setText("");
    m_savedLabel->setStyleSheet(k_savedOkStyle);
    m_countLabel->setText("");
}

void LogViewer::setScriptPath(const QString &scriptPath)
{
    m_scriptDir = scriptPath.isEmpty()
                  ? QString()
                  : QFileInfo(scriptPath).absolutePath();
}

void LogViewer::saveLog()
{
    if (m_savedClean) return;   // nothing new — button should be disabled anyway
    // Determine save directory: <scriptDir>/logs/  (create if needed)
    QString saveDir;
    if (!m_scriptDir.isEmpty()) {
        saveDir = m_scriptDir + "/logs";
        QDir().mkpath(saveDir);
    } else {
        saveDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }

    // Build default filename:  log_YYYYMMDD_HHMMSS.log
    const QString ts       = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString fileName = QString("log_%1.log").arg(ts);
    const QString filePath = QDir(saveDir).filePath(fileName);

    QSaveFile f(filePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << m_logEdit->toPlainText() << "\n";
        if (f.commit()) {
            m_savedClean = true;
            m_saveBtn->setEnabled(false);
            const QString display = m_scriptDir.isEmpty()
                ? filePath
                : QString("logs/%1").arg(fileName);
            m_savedLabel->setText(QString("saved: %1").arg(display));
            m_savedLabel->setStyleSheet(k_savedOkStyle);
            return;
        }
    }
    m_savedLabel->setText(QString("save failed: %1").arg(f.errorString()));
    m_savedLabel->setStyleSheet(k_savedErrStyle);
}
