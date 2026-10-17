#pragma once
#include <QAbstractScrollArea>
#include <QChar>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QPoint>
#include <QPushButton>
#include <QRgb>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QVector>
#include <utility>

class QByteArray;
class QContextMenuEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QWidget;
template <typename T>
class QList;

// ─────────────────────────────────────────────────────────────────────────────
//  TermCell  —  one character cell in the grid
// ─────────────────────────────────────────────────────────────────────────────
struct TermCell
{
        QString text = QStringLiteral(" "); // 1 QChar for BMP, 2 for supplementary plane
        QColor fg;                          // invalid = use default
        QColor bg;                          // invalid = use default (transparent)
        bool bold = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TermView  —  QPainter-based scrolling character grid
//
//  Implements the subset of VT100/ANSI that uShell actually emits:
//    \r          carriage return (cursor col 0, no newline)
//    \n          newline (append line, cursor down)
//    \b          backspace (cursor left 1)
//    ESC [ n D   cursor left  n  (default 1)
//    ESC [ n C   cursor right n  (default 1)
//    ESC [ n A   cursor up    n
//    ESC [ n B   cursor down  n
//    ESC [ K     erase from cursor to end of line
//    ESC [ n m   SGR colour / attribute
//    ESC [ ? h/l private modes (cursor show/hide)
// ─────────────────────────────────────────────────────────────────────────────
class TermView : public QAbstractScrollArea
{
        Q_OBJECT
    public:
        explicit TermView(QWidget *pParent = nullptr);

        void setTermFont(const QFont &font);
        void processBytes(const QByteArray &bytes);
        void clearAll();
        /** Clears the history but keeps the current prompt line and cursor column. */
        void clearKeepPrompt();

    protected:
        void paintEvent(QPaintEvent *) override;
        /** Updates the scrollbar for the new size. */
        void resizeEvent(QResizeEvent *) override;
        void keyPressEvent(QKeyEvent *pEv) override;
        void mousePressEvent(QMouseEvent *pEv) override;
        /** Extends the selection while dragging. */
        void mouseMoveEvent(QMouseEvent *pEv) override;
        /** Ends the selection. */
        void mouseReleaseEvent(QMouseEvent *pEv) override;
        void contextMenuEvent(QContextMenuEvent *pEv) override;

    signals:
        /** Emitted with the bytes produced by a key press. */
        void keyBytesReady(const QByteArray &bytes);

    private slots:
        void blinkCursor();

    private:
        void ensureLine(int iRow);
        /** Returns the cell at (@p row, @p col), growing the grid as needed. */
        TermCell &cell(int row, int col);
        /** Writes @p c at the cursor using the current colours. */
        void putChar(QChar c);
        /** Moves the cursor to the start of the next line. */
        void newline();
        /** Erases from the cursor to the end of the line. */
        void eraseToEndOfLine();
        /** Applies ANSI SGR (colour/attribute) parameters. */
        void applySgr(const QList<int> &params);
        static QColor sgrColor(int iCode);
        void updateScrollbar();
        // Scrolls the viewport to the bottom, matching what every newline() used
        // to do individually — now called at most once per processBytes() batch
        // (see m_scrollToBottomPending).
        void scrollToBottom();

        // ── selection helpers ─────────────────────────────────────────────────
        // Convert a viewport pixel position to a character-grid cell (col, row).
        // X positions inside the gutter clamp to col 0 so the gutter is never
        // part of a selection.
        QPoint pixToCell(const QPoint &vp) const;

        // Normalise anchor/end so that "start" is always top-left of selection.
        std::pair<QPoint, QPoint> normSel() const;

        /** Returns whether text is selected. */
        bool hasSelection() const;
        /** Clears the selection. */
        void clearSelection();
        /** Returns the selected text. */
        QString selectedText() const;
        /** Copies the selection to the clipboard. */
        void copySelectionToClipboard() const;

        // Reads plain text off the system clipboard and sends it to the shell
        // exactly as if it had been typed (one keyBytesReady emission for the
        // whole chunk). CR/CRLF are normalised to LF first, matching what Enter
        // itself sends, so pasted multi-line text doesn't confuse uShell's line
        // editing with a bare '\r'.
        void pasteFromClipboard();

        QVector<QVector<TermCell>> m_grid;
        QPoint m_cursor{0, 0};

        // Set by newline() and consumed once at the end of processBytes() —
        // avoids recomputing the scrollbar range and re-snapping to the bottom
        // once per '\n' in a chunk (a script that prints thousands of lines in
        // one burst used to pay for that on every single line).
        bool m_scrollToBottomPending = false;

        // ── selection state ───────────────────────────────────────────────────
        // Both points are in character-grid coordinates (col, row).
        // (-1,-1) means no selection.
        QPoint m_selAnchor{-1, -1};
        QPoint m_selEnd{-1, -1};
        bool m_selecting = false;

        // ── gutter ────────────────────────────────────────────────────────────
        // Width in pixels of the line-number margin painted to the left of the
        // character grid.  Computed from font metrics in setTermFont / ctor.
        int m_gutterW    = 0;

        QColor m_fgCur;
        QColor m_bgCur;
        bool m_boldCur                 = false;

        static constexpr QRgb C_BG     = 0xFF0A0C10;
        static constexpr QRgb C_FG     = 0xFFABB2BF;
        static constexpr QRgb C_CURSOR = 0xFF528BFF;

        enum class St
        {
            Text,
            Esc,
            Csi,
            CsiPriv
        };
        St m_state = St::Text;
        QString m_param;

        // ── UTF-8 multi-byte decoder ──────────────────────────────────────────
        // Accumulates continuation bytes until a full codepoint is ready.
        char32_t m_utf8Codepoint = 0; // codepoint being assembled
        int m_utf8Remaining      = 0; // continuation bytes still expected

        QFont m_font;
        int m_cw = 10;
        int m_ch = 18;

        QTimer m_blinkTimer;
        bool m_cursorVisible = true;
        bool m_cursorEnabled = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ShellTerminal  —  header bar + TermView
// ─────────────────────────────────────────────────────────────────────────────
class ShellTerminal : public QFrame
{
        Q_OBJECT
    public:
        explicit ShellTerminal(QWidget *pParent = nullptr);

        /** Marks the terminal as accepting input or not. */
        void setActive(bool bActive);
        /** Feeds raw interpreter output to the terminal view. */
        void processRawBytes(const QByteArray &bytes);
        /** Sets the terminal font. */
        void setTerminalFont(const QFont &font);
        void clear();       // full wipe (used on new session)
        void clearPrompt(); // wipe history, keep current prompt line + cursor

    signals:
        /** Emitted with the bytes produced by a key press. */
        void keyBytesReady(const QByteArray &bytes);

    private:
        /** Updates the header label to ACTIVE or inactive. */
        void updateHeaderState();

        QLabel *m_titleLabel;
        QLabel *m_stateLabel;
        QPushButton *m_clearBtn;
        QPushButton *m_stopBtn;
        TermView *m_view;
        bool m_active = false;
};
