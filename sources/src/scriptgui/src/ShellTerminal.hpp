#pragma once
#include <QFrame>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>

/**
 * @brief Minimal dumb-terminal panel (w4).
 *
 * Becomes active on GUI:SHELL_RUN and inactive on GUI:SHELL_EXIT.
 *
 * Output handling (processRawBytes):
 *   Called with every raw byte chunk from the interpreter stdout.
 *   Maintains a live-line state machine:
 *     - \r  → clear current line buffer ("line will be rewritten")
 *     - \n  → commit live line as a permanent block, start fresh
 *     - else→ accumulate in live buffer, re-render in-place immediately
 *   Non-SGR CSI sequences (cursor hide/show, movement, erase) are stripped
 *   before rendering.  SGR colour sequences are kept and converted to HTML
 *   spans by ansiToHtml(), so colours work exactly as in a real terminal.
 *
 * Key input:
 *   An event filter on the display QTextEdit captures every key press and
 *   translates it to the byte sequence expected by uShell (Linux/non-MinGW
 *   column of ushell_core_keys.h).  The resulting bytes are emitted via
 *   keyBytesReady() and must be connected to m_process->write() by the owner.
 */
class ShellTerminal : public QFrame
{
    Q_OBJECT
public:
    explicit ShellTerminal(QWidget *parent = nullptr);

    /** Called on GUI:SHELL_RUN (active=true) / GUI:SHELL_EXIT (active=false). */
    void setActive(bool active);

    /**
     * Feed raw bytes from the process stdout into the terminal.
     * May be called with any chunk size; the state machine handles
     * incomplete escape sequences across multiple calls.
     */
    void processRawBytes(const QByteArray &bytes);

    void clear();
    void setTerminalFont(const QFont &font);

signals:
    /** Raw bytes to write directly to the interpreter's stdin. */
    void keyBytesReady(const QByteArray &bytes);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    // ── live-line rendering ───────────────────────────────────────────────
    void renderLiveLine();
    void commitLiveLine();

    // ── key translation ───────────────────────────────────────────────────
    QByteArray translateKey(QKeyEvent *ev) const;

    // ── display helpers ───────────────────────────────────────────────────
    void updateHeaderState();

    // ── widgets ───────────────────────────────────────────────────────────
    QLabel      *m_titleLabel;
    QLabel      *m_stateLabel;
    QPushButton *m_clearBtn;
    QTextEdit   *m_display;

    // ── live-line state ───────────────────────────────────────────────────
    QByteArray  m_lineRawBuf;           // raw bytes of the line being built
    int         m_liveBlockNumber = -1; // QTextDocument block# of live line
                                        // (-1 = no live block yet)
    bool        m_active = false;
};
