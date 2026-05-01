#include "ShellTerminal.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QTextBlock>
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
        m_display->clear();
        m_lineRawBuf.clear();
        m_liveBlockNumber = -1;
    }
    updateHeaderState();
    if (active)
        m_display->setFocus();
}

// ── output processing ─────────────────────────────────────────────────────────

void ShellTerminal::processRawBytes(const QByteArray &bytes)
{
    for (int i = 0; i < bytes.size(); ++i) {
        const uchar c = static_cast<uchar>(bytes[i]);

        if (c == '\n') {
            // Commit whatever is in the live buffer as a permanent block.
            m_cursorOffset = 0;
            commitLiveLine();

        } else if (c == '\r') {
            // Carriage return: shell is about to rewrite this line from the
            // start (echo, autocomplete, backspace redraw). Clear the buffer —
            // next chars will form the new visible content.
            m_lineRawBuf.clear();
            m_cursorOffset = 0;

        } else {
            // Accumulate, consuming escape sequences as a single unit so they
            // are never split across separate renderLiveLine() calls.
            if (c == 0x1B && i + 1 < bytes.size()) {
                if (static_cast<uchar>(bytes[i + 1]) == '[') {
                    // CSI: ESC [ <params 0x20-0x3F>* <final 0x40-0x7E>
                    int j = i + 2;
                    while (j < bytes.size() &&
                           static_cast<uchar>(bytes[j]) >= 0x20 &&
                           static_cast<uchar>(bytes[j]) <= 0x3F)
                        ++j;
                    if (j < bytes.size()) ++j;
                    m_lineRawBuf += bytes.mid(i, j - i);
                    i = j - 1;
                } else {
                    // Non-CSI escape (e.g. \033= \033>)
                    m_lineRawBuf += bytes.mid(i, 2);
                    ++i;
                }
            } else {
                m_lineRawBuf += c;
            }
            // Re-render live line immediately — this is what makes character
            // echo and autocomplete visible as they happen.
            renderLiveLine();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cursor helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Walk raw bytes accumulating cursor-left (\x1B[nD) and cursor-right
 * (\x1B[nC) CSI sequences to determine how many visible characters the
 * shell's cursor is from the RIGHT end of the visible content.
 *
 * uShell pattern on every keypress:
 *   \r  \x1B[?25l  <full line content>  [\x1B[nD]  \x1B[?25h
 *                                         └─ only present when cursor
 *                                            is not at end of content
 */
static int parseCursorOffset(const QByteArray &raw)
{
    int offset = 0;
    for (int i = 0; i < raw.size(); ++i) {
        if (static_cast<uchar>(raw[i]) != 0x1B || i + 1 >= raw.size())
            continue;
        if (raw[i + 1] != '[')
            continue;
        // Collect parameter bytes (0x30-0x3F)
        int j = i + 2;
        while (j < raw.size() &&
               static_cast<uchar>(raw[j]) >= 0x30 &&
               static_cast<uchar>(raw[j]) <= 0x3F)
            ++j;
        if (j >= raw.size()) break;
        const char finalByte = raw[j];
        const QString param  = QString::fromLatin1(raw.mid(i + 2, j - i - 2));
        const int     n      = param.isEmpty() ? 1 : param.toInt();
        if      (finalByte == 'D') offset += n;   // cursor left  → further from end
        else if (finalByte == 'C') offset -= n;   // cursor right → closer to end
        i = j;
    }
    return qMax(0, offset);
}

/**
 * Insert a block-cursor marker into already-colorised HTML at the position
 * that is @p offsetFromRight visible characters from the right.
 *
 * The function walks the HTML string counting printable characters (skipping
 * tags and treating HTML entities as single chars) and wraps the target
 * character in a highlighted <span>.  If the cursor is at the very end, a
 * thin blinking-bar span is appended instead.
 */
static QString insertCursorInHtml(const QString &html, int offsetFromRight)
{
    // ── count total visible chars ─────────────────────────────────────────
    int total = 0;
    bool inTag = false;
    for (int i = 0; i < html.length(); ++i) {
        const QChar ch = html[i];
        if      (ch == '<')  { inTag = true;  }
        else if (ch == '>')  { inTag = false; }
        else if (!inTag) {
            if (ch == '&') { while (i < html.length() && html[i] != ';') ++i; }
            ++total;
        }
    }

    const int cursorAt = qMax(0, total - offsetFromRight);

    // ── re-walk, injecting cursor at cursorAt ─────────────────────────────
    static const char *C_CUR_BG = "#528bff";
    static const char *C_CUR_FG = "#0d0f14";

    QString result;
    result.reserve(html.size() + 80);
    int  vis      = 0;
    bool inTag2   = false;
    bool inserted = false;

    auto wrapCursor = [&](const QString &inner) {
        result += QString("<span style='background:%1;color:%2;'>%3</span>")
                  .arg(C_CUR_BG, C_CUR_FG, inner);
        inserted = true;
    };

    for (int i = 0; i < html.length(); ++i) {
        const QChar ch = html[i];
        if (ch == '<') {
            inTag2 = true;
            result += ch;
        } else if (ch == '>') {
            inTag2 = false;
            result += ch;
        } else if (inTag2) {
            result += ch;
        } else if (ch == '&') {
            // HTML entity — treat as one visible char
            int end = html.indexOf(';', i);
            if (end < 0) { result += ch; continue; }
            const QString entity = html.mid(i, end - i + 1);
            if (!inserted && vis == cursorAt)
                wrapCursor(entity);
            else
                result += entity;
            ++vis;
            i = end;
        } else {
            if (!inserted && vis == cursorAt)
                wrapCursor(ch);
            else
                result += ch;
            ++vis;
        }
    }

    if (!inserted) {
        // Cursor is at the very end of the line — draw a thin block bar
        result += QString("<span style='border-left:2px solid %1;"
                          "margin-left:1px;'>&nbsp;</span>").arg(C_CUR_BG);
    }

    return result;
}

void ShellTerminal::renderLiveLine()
{
    // ── 1. determine cursor position from raw buffer ────────────────────
    m_cursorOffset = parseCursorOffset(m_lineRawBuf);

    // ── 2. strip non-SGR CSI (cursor show/hide, movement, erase…) ────────
    //    Keep SGR sequences (\x1B[...m) — ansiToHtml handles them.
    static const QRegularExpression csiNonSgrRe(
        "\x1b\\[[\x20-\x3f]*[\x40-\x6c\x6e-\x7e]"
    );
    QString text = QString::fromUtf8(m_lineRawBuf);
    text.remove(csiNonSgrRe);

    // ── 3. colourise + inject cursor ─────────────────────────────────
    const QString coloured   = ansiToHtml(text);
    const QString withCursor = insertCursorInHtml(coloured, m_cursorOffset);
    const QString html = QString(
        "<span style='font-family:&quot;JetBrains Mono&quot;,&quot;Cascadia Code&quot;,"
        "&quot;Consolas&quot;,monospace;color:%1;'>%2</span>")
        .arg(C_PLAIN, withCursor);

    // ── 4. update or create the live block ────────────────────────────
    QTextCursor cursor(m_display->document());

    if (m_liveBlockNumber >= 0) {
        QTextBlock block = m_display->document()->findBlockByNumber(m_liveBlockNumber);
        if (block.isValid()) {
            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertHtml(html);
            // Force an immediate repaint — QTextEdit doesn't always do this
            // on its own for programmatic edits when read-only.
            m_display->viewport()->update();
            return;
        }
        m_liveBlockNumber = -1;
    }

    // No live block yet — append a new one.
    cursor.movePosition(QTextCursor::End);
    if (!m_display->document()->isEmpty())
        cursor.insertBlock();
    m_liveBlockNumber = cursor.blockNumber();
    cursor.insertHtml(html);
    m_display->verticalScrollBar()->setValue(
        m_display->verticalScrollBar()->maximum());
    m_display->viewport()->update();
}

void ShellTerminal::commitLiveLine()
{
    if (!m_lineRawBuf.isEmpty())
        renderLiveLine();
    m_liveBlockNumber = -1;
    m_lineRawBuf.clear();
    m_display->verticalScrollBar()->setValue(
        m_display->verticalScrollBar()->maximum());
}

void ShellTerminal::clear()
{
    m_display->clear();
    m_lineRawBuf.clear();
    m_liveBlockNumber = -1;
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
