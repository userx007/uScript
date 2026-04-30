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
 * Output handling (processLineBytes):
 *   - "last \\r wins" strategy: bytes after the final \\r in a line are the
 *     shell's last redraw of that line, so everything before it is discarded.
 *   - Cursor-movement CSI sequences (\\x1B[A-D, \\x1B[K) are stripped.
 *   - SGR colour sequences (\\x1B[…m) are kept and converted to HTML spans
 *     by the same ansiToHtml() function used in LogViewer.
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
     * Feed one raw line (everything between two \\n in the process output,
     * WITHOUT the trailing \\n itself) to the terminal display.
     */
    void processLineBytes(const QByteArray &lineWithoutNewline);

    void clear();
    void setTerminalFont(const QFont &font);

signals:
    /** Raw bytes to write directly to the interpreter's stdin. */
    void keyBytesReady(const QByteArray &bytes);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    // ── key translation ───────────────────────────────────────────────────
    QByteArray translateKey(QKeyEvent *ev) const;

    // ── display helpers ───────────────────────────────────────────────────
    void appendHtml(const QString &html);
    void updateHeaderState();

    // ── widgets ───────────────────────────────────────────────────────────
    QLabel      *m_titleLabel;
    QLabel      *m_stateLabel;   // "● ACTIVE" / "○ IDLE"
    QPushButton *m_clearBtn;
    QTextEdit   *m_display;

    bool  m_active    = false;
    int   m_lineCount = 0;
};
