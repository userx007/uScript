#pragma once

#include <QAbstractScrollArea>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QTimer>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────────
//  TermCell  —  one character cell in the grid
// ─────────────────────────────────────────────────────────────────────────────
struct TermCell
{
    QChar  c   = ' ';
    QColor fg;          // invalid = use default
    QColor bg;          // invalid = use default (transparent)
    bool   bold = false;
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
    explicit TermView(QWidget *parent = nullptr);

    void setTermFont(const QFont &font);
    void processBytes(const QByteArray &data);
    void clearAll();
    void clearKeepPrompt();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;

signals:
    void keyBytesReady(const QByteArray &bytes);

private slots:
    void blinkCursor();

private:
    void  ensureLine(int row);
    TermCell &cell(int row, int col);
    void  putChar(QChar c);
    void  newline();
    void  eraseToEndOfLine();
    void  applySgr(const QList<int> &params);
    static QColor sgrColor(int code);
    void  updateScrollbar();

    // ── selection helpers ─────────────────────────────────────────────────
    // Convert a viewport pixel position to a character-grid cell (col, row).
    // X positions inside the gutter clamp to col 0 so the gutter is never
    // part of a selection.
    QPoint pixToCell(const QPoint &vp) const;

    // Normalise anchor/end so that "start" is always top-left of selection.
    std::pair<QPoint,QPoint> normSel() const;

    bool    hasSelection() const;
    void    clearSelection();
    QString selectedText()  const;
    void    copySelectionToClipboard() const;

    QVector<QVector<TermCell>> m_grid;
    QPoint  m_cursor  {0, 0};

    // ── selection state ───────────────────────────────────────────────────
    // Both points are in character-grid coordinates (col, row).
    // (-1,-1) means no selection.
    QPoint  m_selAnchor {-1, -1};
    QPoint  m_selEnd    {-1, -1};
    bool    m_selecting = false;

    // ── gutter ────────────────────────────────────────────────────────────
    // Width in pixels of the line-number margin painted to the left of the
    // character grid.  Computed from font metrics in setTermFont / ctor.
    int     m_gutterW = 0;

    QColor  m_fgCur;
    QColor  m_bgCur;
    bool    m_boldCur = false;

    static constexpr QRgb C_BG     = 0xFF0A0C10;
    static constexpr QRgb C_FG     = 0xFFABB2BF;
    static constexpr QRgb C_CURSOR = 0xFF528BFF;

    enum class St { Text, Esc, Csi, CsiPriv };
    St      m_state = St::Text;
    QString m_param;

    QFont   m_font;
    int     m_cw = 10;
    int     m_ch = 18;

    QTimer  m_blinkTimer;
    bool    m_cursorVisible = true;
    bool    m_cursorEnabled = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ShellTerminal  —  header bar + TermView
// ─────────────────────────────────────────────────────────────────────────────
class ShellTerminal : public QFrame
{
    Q_OBJECT
public:
    explicit ShellTerminal(QWidget *parent = nullptr);

    void setActive(bool active);
    void processRawBytes(const QByteArray &bytes);
    void setTerminalFont(const QFont &font);
    void clear();           // full wipe (used on new session)
    void clearPrompt();     // wipe history, keep current prompt line + cursor

signals:
    void keyBytesReady(const QByteArray &bytes);

private:
    void updateHeaderState();

    QLabel      *m_titleLabel;
    QLabel      *m_stateLabel;
    QPushButton *m_clearBtn;
    QPushButton *m_stopBtn;
    TermView    *m_view;
    bool         m_active = false;
};
