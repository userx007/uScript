#include "LogViewer.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QRegularExpression>
#include <QDateTime>

// ─── colour palette (matches AppStyle dark theme) ────────────────────────────
static const QString C_STATUS = "#4a9eff";   // blue  (internal status msgs)
static const QString C_PLAIN  = "#abb2bf";   // grey  (bare / unrecognised lines)

// ─────────────────────────────────────────────────────────────────────────────
//  ANSI SGR escape sequence → HTML converter
// ─────────────────────────────────────────────────────────────────────────────

static QString sgrCodeToColor(int code)
{
    switch (code) {
    case 30: return "#404855";
    case 31: return "#ff5555";
    case 32: return "#50fa7b";
    case 33: return "#f1fa8c";
    case 34: return "#4a9eff";
    case 35: return "#ff79c6";
    case 36: return "#8be9fd";
    case 37: return "#f8f8f2";
    case 90: return "#6272a4";   // bright black / slate (uLogger VERBOSE)
    case 91: return "#ff6e6e";
    case 92: return "#69ff94";
    case 93: return "#ffffa5";
    case 94: return "#d6acff";
    case 95: return "#ff92df";
    case 96: return "#a4ffff";
    case 97: return "#ffffff";
    default: return {};
    }
}

struct SgrState {
    QString color;
    bool bold   = false;
    bool italic = false;
    bool empty() const { return color.isEmpty() && !bold && !italic; }
};

static QString ansiToHtml(const QString &input)
{
    static const QRegularExpression ansiRe("\x1b\\[([0-9;]*)m");

    QString  result;
    SgrState cur;
    bool     spanOpen = false;
    int      pos      = 0;

    auto closeSpan = [&]() {
        if (spanOpen) { result += "</span>"; spanOpen = false; }
    };
    auto openSpan = [&]() {
        if (cur.empty()) return;
        result += "<span style='";
        if (!cur.color.isEmpty()) result += "color:" + cur.color + ";";
        if (cur.bold)             result += "font-weight:bold;";
        if (cur.italic)           result += "font-style:italic;";
        result += "'>";
        spanOpen = true;
    };

    QRegularExpressionMatchIterator it = ansiRe.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();

        // flush plain text before this escape
        if (m.capturedStart() > pos)
            result += input.mid(pos, m.capturedStart() - pos).toHtmlEscaped().replace(' ', "&nbsp;");
        pos = m.capturedEnd();

        const QString ps = m.captured(1);
        const QStringList params = ps.isEmpty()
                                   ? QStringList{"0"}
                                   : ps.split(';', Qt::SkipEmptyParts);

        for (const QString &p : params) {
            const int code = p.toInt();
            if      (code == 0)  { closeSpan(); cur = {}; }
            else if (code == 1)  { closeSpan(); cur.bold   = true;  openSpan(); }
            else if (code == 22) { closeSpan(); cur.bold   = false; openSpan(); }
            else if (code == 3)  { closeSpan(); cur.italic = true;  openSpan(); }
            else if (code == 23) { closeSpan(); cur.italic = false; openSpan(); }
            else {
                const QString c = sgrCodeToColor(code);
                if (!c.isEmpty()) { closeSpan(); cur.color = c; openSpan(); }
            }
        }
    }

    if (pos < input.length())
        result += input.mid(pos).toHtmlEscaped().replace(' ', "&nbsp;");
    closeSpan();
    return result;
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

    m_titleLabel = new QLabel("OUTPUT  LOG", header);
    m_titleLabel->setObjectName("panelTitle");

    m_countLabel = new QLabel("", header);
    m_countLabel->setObjectName("panelInfo");

    m_autoScrollCb = new QCheckBox("auto-scroll", header);
    m_autoScrollCb->setChecked(true);
    m_autoScrollCb->setToolTip("Keep scrolled to the latest log line");

    m_clearBtn = new QPushButton("CLR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear log output");

    hlay->addWidget(m_titleLabel);
    hlay->addStretch();
    hlay->addWidget(m_autoScrollCb);
    hlay->addWidget(m_countLabel);
    hlay->addWidget(m_clearBtn);

    m_logEdit = new QTextEdit(this);
    m_logEdit->setObjectName("logView");
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_logEdit->document()->setMaximumBlockCount(10000);

    root->addWidget(header);
    root->addWidget(m_logEdit, 1);

    connect(m_clearBtn,     &QPushButton::clicked,  this, &LogViewer::clear);
    connect(m_autoScrollCb, &QCheckBox::toggled,    this, &LogViewer::setAutoScroll);
}

void LogViewer::setLogFont(const QFont &font)
{
    // Same QSS-priority issue as ScriptViewer — use setStyleSheet().
    m_logEdit->setStyleSheet(QString(
        "QTextEdit#logView {"
        "  font-family: '%1', 'Cascadia Code', 'Consolas', monospace;"
        "  font-size: %2pt;"
        "}"
    ).arg(font.family()).arg(font.pointSize()));
}

void LogViewer::appendLine(const QString &line)
{
    ++m_lineCount;
    m_countLabel->setText(QString("%1 lines").arg(m_lineCount));

    // Line-number gutter: right-aligned in a fixed 5-char field, muted colour,
    // separated from the log text by a thin vertical bar.
    // Colours match ScriptViewer's QPainter gutter.  HTML rendering in QTextEdit
    // is slightly lighter than QPainter for the same hex, so C_LINENUM is darkened
    // by ~10 on each channel to visually compensate (#4b5263 → #404856).
    static const QString C_LINENUM = "#1b1b1b";   // dim slate (HTML-gamma corrected)
    static const QString C_LINESEP = "#3b4048";   // separator │

    // Build a 5-char right-aligned field using &nbsp; so HTML doesn't collapse
    // the leading spaces (unlike QString::arg padding which HTML would strip).
    const QString rawNum = QString("%1").arg(m_lineCount, 5);
    QString paddedNum;
    paddedNum.reserve(rawNum.size() * 6);
    for (const QChar c : rawNum)
        paddedNum += (c == QLatin1Char(' ') ? QStringLiteral("&nbsp;") : QString(c));

    // No leading space before │ — matches ScriptViewer gutter where the number
    // ends flush against the separator.  One trailing &nbsp; before log text.
    const QString lineNum = QString(
        "<span style='color:%1;font-family:&quot;JetBrains Mono&quot;,&quot;Cascadia Code&quot;,&quot;Consolas&quot;,monospace;"
        "user-select:none;'>%2</span>"
        "<span style='color:%3;'>│&nbsp;</span>")
        .arg(C_LINENUM, paddedNum, C_LINESEP);

    // Convert ANSI escape codes → HTML colour spans, HTML-escape plain text.
    // Lines with no ANSI codes pass through with only HTML escaping applied.
    const QString htmlBody = ansiToHtml(line);
    // font-family here overrides QSS so monospace is guaranteed inside HTML spans
    const QString html     = lineNum +
                             QString("<span style='color:%1;font-family:&quot;JetBrains Mono&quot;,&quot;Cascadia Code&quot;,&quot;Consolas&quot;,monospace;'>%2</span>")
                             .arg(C_PLAIN, htmlBody);
    appendHtml(html);
}

void LogViewer::appendStatus(const QString &msg)
{
    const QString ts   = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString html = QString(
        "<span style='color:%1;font-style:italic;'>── %2  %3 ──</span>")
        .arg(C_STATUS, ts, msg.toHtmlEscaped());
    appendHtml(html);
}

void LogViewer::clear()
{
    m_logEdit->clear();
    m_lineCount = 0;
    m_countLabel->setText("");
}

void LogViewer::appendHtml(const QString &html)
{
    QTextCursor cursor = m_logEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html + "<br>");

    if (m_autoScroll)
        m_logEdit->verticalScrollBar()->setValue(
            m_logEdit->verticalScrollBar()->maximum());
}
