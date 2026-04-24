#include "LogViewer.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QRegularExpression>
#include <QDateTime>

// ─── colour palette (matches AppStyle dark theme) ────────────────────────────
static const QString C_DEBUG  = "#404855";   // dim grey
static const QString C_INFO   = "#c8d0dc";   // near-white
static const QString C_WARN   = "#ffb86c";   // amber
static const QString C_ERROR  = "#ff5555";   // red
static const QString C_TRACE  = "#8be9fd";   // cyan  (TRACE / VERBOSE)
static const QString C_STATUS = "#4a9eff";   // blue  (internal status msgs)
static const QString C_PLAIN  = "#abb2bf";   // grey  (bare LOG_EMPTY lines)
static const QString C_TS     = "#2d333f";   // very dim  (timestamps)

// ─────────────────────────────────────────────────────────────────────────────
LogViewer::LogViewer(QWidget *parent)
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

    // ── Log text area ─────────────────────────────────────────────────────
    m_logEdit = new QTextEdit(this);
    m_logEdit->setObjectName("logView");
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    // Limit document size to avoid unbounded memory growth
    m_logEdit->document()->setMaximumBlockCount(10000);

    root->addWidget(header);
    root->addWidget(m_logEdit, 1);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_clearBtn,    &QPushButton::clicked,
            this,          &LogViewer::clear);
    connect(m_autoScrollCb, &QCheckBox::toggled,
            this,           &LogViewer::setAutoScroll);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Interpret one raw log payload (the part after "GUI:LOG:").
//
//  Log lines from uLogger have the structure:
//      HH:MM:SS.µµµµµµ | LEVEL | <header> | <message>
//  or bare text for LOG_EMPTY-style lines.
// ─────────────────────────────────────────────────────────────────────────────
void LogViewer::appendLine(const QString &line)
{
    ++m_lineCount;
    m_countLabel->setText(QString("%1 lines").arg(m_lineCount));

    // Detect level keyword in the structured form
    // "HH:MM:SS.µµµµµµ | LEVEL | …"
    static const QRegularExpression levelRe(
        R"(\|\s*(TRACE|DEBUG|INFO|WARN(?:ING)?|ERROR|FATAL|CRITICAL)\s*\|)",
        QRegularExpression::CaseInsensitiveOption
    );

    QString color  = C_PLAIN;
    QString weight = "normal";

    const auto m = levelRe.match(line);
    if (m.hasMatch()) {
        const QString level = m.captured(1).toUpper();
        if      (level == "TRACE")                color = C_TRACE;
        else if (level == "DEBUG")                color = C_DEBUG;
        else if (level == "INFO")                 color = C_INFO;
        else if (level.startsWith("WARN"))        color = C_WARN;
        else if (level == "ERROR"
                 || level == "FATAL"
                 || level == "CRITICAL") {
            color  = C_ERROR;
            weight = "bold";
        }

        // Render timestamp in a much dimmer colour than the rest
        //  e.g.  <dim>HH:MM:SS.µµ</dim> | INFO | header | message
        const int pipeIdx = line.indexOf('|');
        if (pipeIdx > 0) {
            const QString ts   = line.left(pipeIdx).trimmed().toHtmlEscaped();
            const QString rest = line.mid(pipeIdx).toHtmlEscaped();
            const QString html = QString(
                "<span style='color:%1;font-weight:%2;'>"
                "<span style='color:%3;'>%4 </span>%5"
                "</span>")
                .arg(color, weight, C_TS, ts, rest);
            appendHtml(html);
            return;
        }
    }

    // Bare / unstructured line
    const QString html = QString("<span style='color:%1;font-weight:%2;'>%3</span>")
                         .arg(color, weight, line.toHtmlEscaped());
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

    if (m_autoScroll) {
        m_logEdit->verticalScrollBar()->setValue(
            m_logEdit->verticalScrollBar()->maximum());
    }
}
