#include "ShellTerminal.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QFontMetrics>
#include <QApplication>
#include <QStyleHints>

// ═════════════════════════════════════════════════════════════════════════════
//  TermView
// ═════════════════════════════════════════════════════════════════════════════

TermView::TermView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    // Default monospace font
    m_font.setFamily("JetBrains Mono");
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPointSize(12);
    QFontMetrics fm(m_font);
    m_cw = fm.horizontalAdvance('M');
    m_ch = fm.height();

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setStyleSheet(QString("background: #%1;").arg(C_BG & 0xFFFFFF, 6, 16, QChar('0')));
    viewport()->setCursor(Qt::IBeamCursor);
    setFocusPolicy(Qt::StrongFocus);

    // Cursor blink
    const int blinkMs = QGuiApplication::styleHints()->cursorFlashTime() / 2;
    m_blinkTimer.setInterval(blinkMs > 0 ? blinkMs : 500);
    connect(&m_blinkTimer, &QTimer::timeout, this, &TermView::blinkCursor);
    m_blinkTimer.start();

    ensureLine(0);
}

// ── font ─────────────────────────────────────────────────────────────────────

void TermView::setTermFont(const QFont &font)
{
    m_font = font;
    QFontMetrics fm(m_font);
    m_cw = fm.horizontalAdvance('M');
    m_ch = fm.height();
    updateScrollbar();
    viewport()->update();
}

// ── grid helpers ──────────────────────────────────────────────────────────────

void TermView::ensureLine(int row)
{
    if (m_grid.size() <= row)
        m_grid.resize(row + 1);
}

TermCell &TermView::cell(int row, int col)
{
    ensureLine(row);
    if (m_grid[row].size() <= col)
        m_grid[row].resize(col + 1);
    return m_grid[row][col];
}

void TermView::putChar(QChar c)
{
    TermCell &tc = cell(m_cursor.y(), m_cursor.x());
    tc.c    = c;
    tc.fg   = m_fgCur;
    tc.bg   = m_bgCur;
    tc.bold = m_boldCur;
    m_cursor.setX(m_cursor.x() + 1);
    // update() called once at end of processBytes() — not per character
}

void TermView::newline()
{
    m_cursor.setX(0);
    m_cursor.setY(m_cursor.y() + 1);
    ensureLine(m_cursor.y());
    updateScrollbar();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    // update() called once at end of processBytes()
}

void TermView::eraseToEndOfLine()
{
    const int row = m_cursor.y();
    const int col = m_cursor.x();
    ensureLine(row);
    if (col < m_grid[row].size())
        m_grid[row].resize(col);
    // update() called once at end of processBytes()
}

// ── SGR ───────────────────────────────────────────────────────────────────────

QColor TermView::sgrColor(int code)
{
    // Dracula palette — same as previous ansiToHtml()
    switch (code) {
    case 30: return QColor(0x40, 0x48, 0x55);
    case 31: return QColor(0xFF, 0x55, 0x55);
    case 32: return QColor(0x50, 0xFA, 0x7B);
    case 33: return QColor(0xF1, 0xFA, 0x8C);
    case 34: return QColor(0x4A, 0x9E, 0xFF);
    case 35: return QColor(0xFF, 0x79, 0xC6);
    case 36: return QColor(0x8B, 0xE9, 0xFD);
    case 37: return QColor(0xF8, 0xF8, 0xF2);
    case 39: return QColor();               // default fg
    case 40: return QColor(0x28, 0x2A, 0x36);
    case 41: return QColor(0xFF, 0x55, 0x55);
    case 42: return QColor(0x50, 0xFA, 0x7B);
    case 43: return QColor(0xF1, 0xFA, 0x8C);
    case 44: return QColor(0x4A, 0x9E, 0xFF);
    case 45: return QColor(0xFF, 0x79, 0xC6);
    case 46: return QColor(0x8B, 0xE9, 0xFD);
    case 47: return QColor(0xF8, 0xF8, 0xF2);
    case 49: return QColor();               // default bg
    case 90: return QColor(0x62, 0x72, 0xA4);
    case 91: return QColor(0xFF, 0x6E, 0x6E);
    case 92: return QColor(0x69, 0xFF, 0x94);
    case 93: return QColor(0xFF, 0xFF, 0xA5);
    case 94: return QColor(0xD6, 0xAC, 0xFF);
    case 95: return QColor(0xFF, 0x92, 0xDF);
    case 96: return QColor(0xA4, 0xFF, 0xFF);
    case 97: return QColor(0xFF, 0xFF, 0xFF);
    default: return QColor();
    }
}

void TermView::applySgr(const QList<int> &params)
{
    for (const int p : params) {
        if (p == 0) {
            m_fgCur   = QColor();
            m_bgCur   = QColor();
            m_boldCur = false;
        } else if (p == 1) {
            m_boldCur = true;
        } else if (p == 22) {
            m_boldCur = false;
        } else if ((p >= 30 && p <= 37) || p == 39 || (p >= 90 && p <= 97)) {
            m_fgCur = sgrColor(p);
        } else if ((p >= 40 && p <= 47) || p == 49) {
            m_bgCur = sgrColor(p);
        }
    }
}

// ── CSI state machine ─────────────────────────────────────────────────────────

void TermView::processBytes(const QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i) {
        const uchar c = static_cast<uchar>(data[i]);

        switch (m_state) {

        // ── normal text ───────────────────────────────────────────────────
        case St::Text:
            if (c == 0x1B) {
                m_state = St::Esc;
            } else if (c == '\n') {
                newline();
            } else if (c == '\r') {
                // Carriage return: move cursor to col 0, stay on same line.
                // uShell uses \r to rewrite the current input line from scratch.
                m_cursor.setX(0);
            } else if (c == '\b') {
                if (m_cursor.x() > 0)
                    m_cursor.setX(m_cursor.x() - 1);
            } else if (c >= 0x20 || c == '\t') {
                putChar(c == '\t' ? QChar(' ') : QChar(c));
            }
            break;

        // ── ESC received ──────────────────────────────────────────────────
        case St::Esc:
            m_param.clear();
            if (c == '[') {
                m_state = St::Csi;
            } else {
                // Non-CSI escape (e.g. \x1B= \x1B>) — consume and reset
                m_state = St::Text;
            }
            break;

        // ── inside CSI  ESC [ … ───────────────────────────────────────────
        case St::Csi:
            if (c == '?') {
                // Private parameter prefix: ESC [ ? …
                m_state = St::CsiPriv;
            } else if ((c >= '0' && c <= '9') || c == ';') {
                m_param += QChar(c);
            } else {
                // Final byte — dispatch
                const QStringList parts = m_param.split(';', Qt::KeepEmptyParts);
                QList<int> nums;
                for (const QString &p : parts)
                    nums << (p.isEmpty() ? 0 : p.toInt());
                const int n = nums.isEmpty() ? 0 : nums[0];
                const int move = (n == 0) ? 1 : n;   // cursor moves default to 1

                switch (c) {
                case 'A': // cursor up
                    m_cursor.setY(qMax(0, m_cursor.y() - move));
                    break;
                case 'B': // cursor down
                    m_cursor.setY(m_cursor.y() + move);
                    ensureLine(m_cursor.y());
                    break;
                case 'C': // cursor right
                    m_cursor.setX(m_cursor.x() + move);
                    break;
                case 'D': // cursor left
                    m_cursor.setX(qMax(0, m_cursor.x() - move));
                    break;
                case 'H': { // cursor position ESC [ row ; col H
                    const int row = (nums.size() > 1 ? nums[1] : 1) - 1;
                    const int col = (nums.size() > 0 ? nums[0] : 1) - 1;
                    m_cursor.setY(qMax(0, row));
                    m_cursor.setX(qMax(0, col));
                    ensureLine(m_cursor.y());
                    break;
                }
                case 'J': // erase display
                    if (n == 2) clearAll();
                    break;
                case 'K': // erase in line
                    if (n == 0) eraseToEndOfLine();
                    break;
                case 'm': // SGR
                    applySgr(nums);
                    break;
                default:
                    break;  // silently consume unhandled sequences
                }
                m_param.clear();
                m_state = St::Text;
            }
            break;

        // ── private CSI  ESC [ ? … ───────────────────────────────────────
        case St::CsiPriv:
            if ((c >= '0' && c <= '9') || c == ';') {
                m_param += QChar(c);
            } else {
                // ESC [ ? 25 h  = show cursor
                // ESC [ ? 25 l  = hide cursor
                if (m_param == "25") {
                    m_cursorEnabled = (c == 'h');
                    m_cursorVisible = m_cursorEnabled;
                }
                m_param.clear();
                m_state = St::Text;
            }
            break;
        }
    }
    viewport()->update();
}

// ── painting ──────────────────────────────────────────────────────────────────

void TermView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(m_font);

    const QColor defaultFg(C_FG);
    const QColor defaultBg(C_BG);

    p.fillRect(viewport()->rect(), defaultBg);

    const int scrollY  = verticalScrollBar()->value();
    const int firstRow = scrollY / m_ch;
    const int lastRow  = qMin(m_grid.size() - 1,
                              firstRow + viewport()->height() / m_ch + 1);

    const int xOff = 2;
    const int yOff = -scrollY;

    for (int row = firstRow; row <= lastRow; ++row) {
        const auto &line = m_grid[row];
        const int   y    = yOff + row * m_ch;

        for (int col = 0; col < line.size(); ++col) {
            const TermCell &tc = line[col];
            const int x = xOff + col * m_cw;

            // Background
            if (tc.bg.isValid()) {
                p.fillRect(x, y, m_cw, m_ch, tc.bg);
            }

            // Foreground
            QFont f = m_font;
            if (tc.bold) f.setBold(true);
            p.setFont(f);
            p.setPen(tc.fg.isValid() ? tc.fg : defaultFg);
            p.drawText(QRect(x, y, m_cw, m_ch), Qt::AlignCenter,
                       tc.c == ' ' ? QString() : QString(tc.c));
        }
    }

    // Draw cursor
    if (m_cursorVisible && m_cursorEnabled) {
        const int cx = xOff + m_cursor.x() * m_cw;
        const int cy = yOff + m_cursor.y() * m_ch;
        p.fillRect(cx, cy, m_cw, m_ch, QColor(C_CURSOR));

        // Draw the character under the cursor inverted
        if (m_cursor.y() < m_grid.size() &&
            m_cursor.x() < m_grid[m_cursor.y()].size()) {
            const TermCell &tc = m_grid[m_cursor.y()][m_cursor.x()];
            p.setPen(QColor(C_BG));
            p.setFont(m_font);
            p.drawText(QRect(cx, cy, m_cw, m_ch), Qt::AlignCenter,
                       tc.c == ' ' ? QString() : QString(tc.c));
        }
    }
}

// ── scrollbar / resize ────────────────────────────────────────────────────────

void TermView::updateScrollbar()
{
    const int total  = m_grid.size() * m_ch;
    const int visible = viewport()->height();
    verticalScrollBar()->setRange(0, qMax(0, total - visible));
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setSingleStep(m_ch);
}

void TermView::resizeEvent(QResizeEvent *ev)
{
    QAbstractScrollArea::resizeEvent(ev);
    updateScrollbar();
}

// ── key events ────────────────────────────────────────────────────────────────

void TermView::keyPressEvent(QKeyEvent *ev)
{
    const Qt::KeyboardModifiers mod = ev->modifiers();
    QByteArray bytes;

    if (mod & Qt::ControlModifier) {
        switch (ev->key()) {
        case Qt::Key_U: bytes = "\x15"; break;
        case Qt::Key_K: bytes = "\x0B"; break;
        case Qt::Key_C: bytes = "\x03"; break;
        default: break;
        }
    }

    if (bytes.isEmpty()) {
        switch (ev->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:     bytes = "\x0A";    break;
        case Qt::Key_Backspace: bytes = "\x7F";    break;
        case Qt::Key_Tab:       bytes = "\x09";    break;
        case Qt::Key_Escape:    bytes = "\x1B";    break;
        case Qt::Key_Up:        bytes = "\x1B[A";  break;
        case Qt::Key_Down:      bytes = "\x1B[B";  break;
        case Qt::Key_Right:     bytes = "\x1B[C";  break;
        case Qt::Key_Left:      bytes = "\x1B[D";  break;
        case Qt::Key_Home:      bytes = "\x1B[H";  break;
        case Qt::Key_End:       bytes = "\x1B[F";  break;
        case Qt::Key_Insert:    bytes = "\x1B[2~"; break;
        case Qt::Key_Delete:    bytes = "\x1B[3~"; break;
        case Qt::Key_PageUp:    bytes = "\x1B[5~"; break;
        case Qt::Key_PageDown:  bytes = "\x1B[6~"; break;
        default:
            if (!ev->text().isEmpty() && ev->text().at(0).isPrint())
                bytes = ev->text().toUtf8();
            break;
        }
    }

    if (!bytes.isEmpty())
        emit keyBytesReady(bytes);
}

// ── cursor blink ──────────────────────────────────────────────────────────────

void TermView::blinkCursor()
{
    if (m_cursorEnabled) {
        m_cursorVisible = !m_cursorVisible;
        viewport()->update();
    }
}

// ── clear ─────────────────────────────────────────────────────────────────────

void TermView::clearAll()
{
    m_grid.clear();
    m_cursor = {0, 0};
    m_fgCur  = QColor();
    m_bgCur  = QColor();
    m_boldCur = false;
    m_state  = St::Text;
    m_param.clear();
    ensureLine(0);
    updateScrollbar();
    viewport()->update();
}

void TermView::clearKeepPrompt()
{
    // Save the current (prompt) line and cursor column before wiping.
    const QVector<TermCell> promptLine =
        m_cursor.y() < m_grid.size() ? m_grid[m_cursor.y()] : QVector<TermCell>{};
    const int savedCursorCol = m_cursor.x();

    // Full clear
    m_grid.clear();
    m_fgCur   = QColor();
    m_bgCur   = QColor();
    m_boldCur = false;
    m_state   = St::Text;
    m_param.clear();

    // Restore prompt line at row 0 with cursor at saved column
    ensureLine(0);
    m_grid[0]  = promptLine;
    m_cursor   = { savedCursorCol, 0 };

    updateScrollbar();
    viewport()->update();
}

// ═════════════════════════════════════════════════════════════════════════════
//  ShellTerminal  —  header wrapper
// ═════════════════════════════════════════════════════════════════════════════

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

    m_stopBtn = new QPushButton("STOP", header);
    m_stopBtn->setObjectName("stopBtn");
    m_stopBtn->setToolTip("Send exit command to shell (#q + Enter)");

    hlay->addWidget(m_titleLabel);
    hlay->addStretch();
    hlay->addWidget(m_stateLabel);
    hlay->addWidget(m_stopBtn);
    hlay->addWidget(m_clearBtn);

    // ── terminal view ─────────────────────────────────────────────────────
    m_view = new TermView(this);
    m_view->setFocusPolicy(Qt::StrongFocus);
    connect(m_view, &TermView::keyBytesReady,
            this,   &ShellTerminal::keyBytesReady);

    root->addWidget(header);
    root->addWidget(m_view, 1);

    connect(m_clearBtn, &QPushButton::clicked, this, &ShellTerminal::clearPrompt);
    connect(m_stopBtn,  &QPushButton::clicked, this, [this]() {
        emit keyBytesReady("#q\x0A");   // '#q' + Enter (0x0A) — uShell exit sequence
    });
    updateHeaderState();
}

void ShellTerminal::setActive(bool active)
{
    const bool wasActive = m_active;
    m_active = active;
    if (active) {
        // Clear on every new session (re-activation), but NOT on the very
        // first call — bytes may already have arrived in the same chunk as
        // GUI:SHELL_RUN and been forwarded before setActive() was called.
        if (wasActive)
            m_view->clearAll();
        m_view->setFocus();
    }
    updateHeaderState();
}

void ShellTerminal::processRawBytes(const QByteArray &bytes)
{
    m_view->processBytes(bytes);
}

void ShellTerminal::setTerminalFont(const QFont &font)
{
    m_view->setTermFont(font);
}

void ShellTerminal::clear()
{
    m_view->clearAll();
}

void ShellTerminal::clearPrompt()
{
    m_view->clearKeepPrompt();
}

void ShellTerminal::updateHeaderState()
{
    if (m_active) {
        m_stateLabel->setText("●  ACTIVE");
        m_stateLabel->setStyleSheet("color:#50fa7b;font-size:10px;"
            "font-family:'JetBrains Mono','Consolas',monospace;"
            "font-weight:bold;padding-right:8px;");
        m_stopBtn->setEnabled(true);
    } else {
        m_stateLabel->setText("○  IDLE");
        m_stateLabel->setStyleSheet("color:#4b5263;font-size:10px;"
            "font-family:'JetBrains Mono','Consolas',monospace;"
            "padding-right:8px;");
        m_stopBtn->setEnabled(false);
    }
}
