#pragma once
#include <QFrame>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

/**
 * @brief Log output panel (w3).
 *
 * Parses the interpreter's formatted log lines and colourises them by level:
 *   DEBUG  → dim grey
 *   INFO   → white
 *   WARN   → amber
 *   ERROR  → red
 *   bare   → light-grey (LOG_EMPTY lines from the interpreter)
 */
class LogViewer : public QFrame
{
    Q_OBJECT
public:
    explicit LogViewer(QWidget *parent = nullptr);

    // Append a raw GUI:LOG:<message> payload (the "GUI:LOG:" prefix stripped).
    void appendLine(const QString &line);

    // Append a plain status message (rendered in dim italic, not from interpreter).
    void appendStatus(const QString &msg);

    void clear();
    // Set the font used in the log text area (called by MainWindow for Ctrl+/-).
    void setLogFont(const QFont &font);

public slots:
    void setAutoScroll(bool on) { m_autoScroll = on; }

private:
    void appendHtml(const QString &html);

    QLabel      *m_titleLabel;
    QLabel      *m_countLabel;
    QTextEdit   *m_logEdit;
    QPushButton *m_clearBtn;
    QCheckBox   *m_autoScrollCb;
    bool         m_autoScroll = true;
    int          m_lineCount  = 0;
};
