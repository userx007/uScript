#include "ShellTerminal.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QRegularExpression>

// ─── colour constants (dark theme, matches AppStyle) ──────────────────────────
static const QString C_PLAIN  = "#abb2bf";   // default text colour
static const QString C_STATUS = "#4a9eff";   // blue – status / info messages

// ─────────────────────────────────────────────────────────────────────────────
//  ANSI SGR → HTML  (identical logic to LogViewer's ansiToHtml)
// ─────────────────────────────────────────────────────────────────────────────

static QString sgrColor(int code)
{
    switch (code) {
    case 30: return "#404855"; case 31: return "#ff5555"; case 32: return "#50fa7b";
    case 33: return "#f1fa8c"; case 34: return "#4a9eff"; case 35: return "#ff79c6";
    case 36: return "#8be9fd"; case 37: return "#f8f8f2"; case 90: return "#6272a4";
    case 91: return "#ff6e6e"; case 92: return "#69ff94"; case 93: return "#ffffa5";
    case 94: return "#d6acff"; case 95: return "#ff92df"; case 96: return "#a4ffff";
    case 97: return "#ffffff";
    default: return {};
    }
}

struct SgrState {
    QString color;
    bool bold = false, italic = false;
    bool empty() const { return color.isEmpty() && !bold && !italic; }
};

static QString ansiToHtml(const QString &input)
{
    static const QRegularExpression ansiRe("\x1b\\[([0-9;]*)m");

    QString  result;
    SgrState cur;
    bool     spanOpen = false;
    int      pos      = 0;

    auto closeSpan = [&]() { if (spanOpen) { result += "</span>"; spanOpen = false; } };
    auto openSpan  = [&]() {
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
        if (m.capturedStart() > pos)
            result += input.mid(pos, m.capturedStart() - pos).toHtmlEscaped().replace(' ', "&nbsp;");
        pos = m.capturedEnd();

        const QStringList params = m.captured(1).isEmpty()
                                   ? QStringList{"0"}
                                   : m.captured(1).split(';', Qt::SkipEmptyParts);
        for (const QString &p : params) {
            const int code = p.toInt();
            if      (code == 0)  { closeSpan(); cur = {}; }
            else if (code == 1)  { closeSpan(); cur.bold   = true;  openSpan(); }
            else if (code == 22) { closeSpan(); cur.bold   = false; openSpan(); }
            else if (code == 3)  { closeSpan(); cur.italic = true;  openSpan(); }
            else if (code == 23) { closeSpan(); cur.italic = false; openSpan(); }
            else {
                const QString c = sgrColor(code);
                if (!c.isEmpty()) { closeSpan(); cur.color = c; openSpan(); }
            }
        }
    }
    if (pos < input.length())
        result += input.mid(pos).toHtmlEscaped().replace(' ', "&nbsp;");
    closeSpan();

    // Safety net: remove any ESC characters that survived the regex passes
    // (e.g. from non-CSI sequences like \033= or \033>).  They are invisible
    // in most HTML renderers but can cause subtle layout issues.
    result.remove(QChar(0x1B));
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ShellTerminal
// ─────────────────────────────────────────────────────────────────────────────

ShellTerminal::ShellTerminal(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("panelFrame");
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── header ────────────────────────────────────────────────────────────
    auto *header = new QFrame(this);
    header->setObjectName("panelHeader");
    header->setFrameShape(QFrame::NoFrame);

    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_titleLabel = new QLabel("SHELL  TERMINAL", header);
    m_titleLabel->setObjectName("panelTitle");

    m_stateLabel = new QLabel("○  IDLE", header);
    m_stateLabel->setObjectName("panelInfo");

    m_clearBtn = new QPushButton("CLR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear terminal output");

    hlay->addWidget(m_titleLabel);
    hlay->addStretch();
    hlay->addWidget(m_stateLabel);
    hlay->addWidget(m_clearBtn);

    // ── display area ──────────────────────────────────────────────────────
    m_display = new QTextEdit(this);
    m_display->setObjectName("shellView");
    m_display->setReadOnly(true);
    m_display->setLineWrapMode(QTextEdit::NoWrap);
    m_display->document()->setMaximumBlockCount(5000);

    // Install event filter so we can capture key presses for the shell
    m_display->installEventFilter(this);
    // Also accept focus so arrow keys etc. reach us
    m_display->setFocusPolicy(Qt::StrongFocus);

    root->addWidget(header);
    root->addWidget(m_display, 1);

    connect(m_clearBtn, &QPushButton::clicked, this, &ShellTerminal::clear);

    updateHeaderState();
}

// ── public API ────────────────────────────────────────────────────────────────

void ShellTerminal::setActive(bool active)
{
    m_active = active;
    if (active) {
        m_lineCount = 0;
        m_display->clear();
    }
    updateHeaderState();
    // Give focus to the display so key events arrive immediately
    if (active)
        m_display->setFocus();
}

void ShellTerminal::processLineBytes(const QByteArray &lineWithoutNewline)
{
    if (lineWithoutNewline.isEmpty())
        return;

    // ── "last \r wins" ─────────────────────────────────────────────────────
    // If the shell did a carriage-return-based line redraw (typical for
    // backspace/editing), the last \r marks where the final content starts.
    QByteArray effective = lineWithoutNewline;
    const int lastCr = effective.lastIndexOf('\r');
    if (lastCr >= 0)
        effective = effective.mid(lastCr + 1);

    // ── strip all non-SGR CSI escape sequences ─────────────────────────────
    // CSI structure: ESC [ <param bytes 0x20-0x3F>* <final byte 0x40-0x7E>
    // SGR sequences end in 'm' (0x6D) and are kept for ansiToHtml().
    // Everything else — cursor show/hide \033[?25l/h, cursor movement
    // \033[nA-D, erase \033[K, cursor position \033[nG/H, etc. — is stripped.
    // NOTE: do NOT add a stray-ESC stripper here — it would consume the \x1B
    //       from SGR sequences before ansiToHtml() can process them.
    //       ansiToHtml() removes any remaining \x1B chars itself after SGR pass.
    static const QRegularExpression csiNonSgrRe(
        "\x1b\\[[\x20-\x3f]*[\x40-\x6c\x6e-\x7e]"   // CSI + params + final≠m
    );

    QString text = QString::fromUtf8(effective);
    text.remove(csiNonSgrRe);

    if (text.isEmpty())
        return;

    const QString body = ansiToHtml(text);
    const QString html = QString("<span style='color:%1;font-family:&quot;JetBrains Mono&quot;,&quot;Cascadia Code&quot;,"
                                 "&quot;Consolas&quot;,monospace;'>%2</span>")
                         .arg(C_PLAIN, body);
    appendHtml(html);
}

void ShellTerminal::clear()
{
    m_display->clear();
    m_lineCount = 0;
}

void ShellTerminal::setTerminalFont(const QFont &font)
{
    m_display->setStyleSheet(QString(
        "QTextEdit#shellView {"
        "  font-family: '%1', 'Cascadia Code', 'Consolas', monospace;"
        "  font-size: %2pt;"
        "}"
    ).arg(font.family()).arg(font.pointSize()));
}

// ── key translation ───────────────────────────────────────────────────────────
//
// Follows the non-MinGW (Linux) column of ushell_core_keys.h.

QByteArray ShellTerminal::translateKey(QKeyEvent *ev) const
{
    const Qt::KeyboardModifiers mod = ev->modifiers();

    // Ctrl combinations
    if (mod & Qt::ControlModifier) {
        switch (ev->key()) {
        case Qt::Key_U: return "\x15";   // uSHELL_KEY_CTRL_U
        case Qt::Key_K: return "\x0B";   // uSHELL_KEY_CTRL_K
        case Qt::Key_C: return "\x03";   // SIGINT / interrupt
        default: break;
        }
    }

    switch (ev->key()) {
    // ── basic keys ────────────────────────────────────────────────────────
    case Qt::Key_Return:
    case Qt::Key_Enter:     return "\x0A";        // uSHELL_KEY_ENTER   (Linux: 0x0A)
    case Qt::Key_Backspace: return "\x7F";        // uSHELL_KEY_BACKSPACE (Linux: 0x7F)
    case Qt::Key_Tab:       return "\x09";        // uSHELL_KEY_TAB
    case Qt::Key_Escape:    return "\x1B";        // uSHELL_KEY_ESCAPE
    case Qt::Key_Space:     return " ";           // uSHELL_KEY_SPACE

    // ── arrow keys (\x1B [ letter) ────────────────────────────────────────
    case Qt::Key_Up:        return "\x1B[A";      // uSHELL_KEY_ESCAPESEQ_ARROW_UP    (0x41)
    case Qt::Key_Down:      return "\x1B[B";      // uSHELL_KEY_ESCAPESEQ_ARROW_DOWN  (0x42)
    case Qt::Key_Right:     return "\x1B[C";      // uSHELL_KEY_ESCAPESEQ_ARROW_RIGHT (0x43)
    case Qt::Key_Left:      return "\x1B[D";      // uSHELL_KEY_ESCAPESEQ_ARROW_LEFT  (0x44)

    // ── home / end ────────────────────────────────────────────────────────
    case Qt::Key_Home:      return "\x1B[H";      // uSHELL_KEY_ESCAPESEQ_HOME    (0x48 → \033[H)
    case Qt::Key_End:       return "\x1B[F";      // uSHELL_KEY_ESCAPESEQ_END     (0x46 → \033[F)

    // ── insert / delete / page keys (\x1B [ digit ~) ─────────────────────
    case Qt::Key_Insert:    return "\x1B[2~";     // uSHELL_KEY_ESCAPESEQ1_INSERT  (0x32)
    case Qt::Key_Delete:    return "\x1B[3~";     // uSHELL_KEY_ESCAPESEQ1_DELETE  (0x33)
    case Qt::Key_PageUp:    return "\x1B[5~";     // uSHELL_KEY_ESCAPESEQ1_PAGEUP  (0x35)
    case Qt::Key_PageDown:  return "\x1B[6~";     // uSHELL_KEY_ESCAPESEQ1_PAGEDOWN(0x36)

    default:
        // Printable characters (including letters, digits, symbols)
        const QString text = ev->text();
        if (!text.isEmpty() && text.at(0).isPrint())
            return text.toUtf8();
        return {};
    }
}

// ── event filter ──────────────────────────────────────────────────────────────

bool ShellTerminal::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_display && ev->type() == QEvent::KeyPress && m_active) {
        auto *ke = static_cast<QKeyEvent *>(ev);

        // Let Ctrl+C/A/V pass through to the OS for copy/paste when not
        // in active shell mode — but when active, Ctrl+C must go to the shell.
        const QByteArray bytes = translateKey(ke);
        if (!bytes.isEmpty()) {
            emit keyBytesReady(bytes);
            return true;   // consumed — do NOT let QTextEdit handle it
        }
    }
    return QFrame::eventFilter(obj, ev);
}

// ── private helpers ───────────────────────────────────────────────────────────

void ShellTerminal::appendHtml(const QString &html)
{
    QTextCursor cursor = m_display->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html + "<br>");
    m_display->verticalScrollBar()->setValue(
        m_display->verticalScrollBar()->maximum());
}

void ShellTerminal::updateHeaderState()
{
    if (m_active) {
        m_stateLabel->setText("●&nbsp;&nbsp;ACTIVE");
        m_stateLabel->setStyleSheet("color: #50fa7b; font-size: 10px;"
                                    "font-family: 'JetBrains Mono','Consolas',monospace;"
                                    "font-weight: bold; padding-right: 8px;");
    } else {
        m_stateLabel->setText("○&nbsp;&nbsp;IDLE");
        m_stateLabel->setStyleSheet("color: #4b5263; font-size: 10px;"
                                    "font-family: 'JetBrains Mono','Consolas',monospace;"
                                    "padding-right: 8px;");
    }
}
