#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

/**
 * @brief Syntax highlighter for comm script files.
 *
 * Comm script syntax (from uCommScriptCommandValidator / uCommScriptDataTypes):
 *
 *  Line structure:
 *    > EXPR1 | EXPR2     SEND_RECV  (send EXPR1, optionally match EXPR2)
 *    < EXPR1 | EXPR2     RECV_SEND  (receive EXPR1, optionally send EXPR2)
 *    ! <number> <unit>   DELAY      (delay: sec / ms / us)
 *    NAME := value       macro definition  (same as main script)
 *    # ...               line comment      (same as main script)
 *    ---  …  !--         block comment     (same as main script)
 *
 *  Token decorators (prefix + quoted content):
 *    H"hex"      HEXSTREAM        hex byte sequence
 *    R"pattern"  REGEX            regular expression
 *    T"str"      TOKEN_STRING     string token to wait for
 *    X"hex"      TOKEN_HEXSTREAM  hex token to wait for
 *    L"str"      LINE             line-terminated read
 *    S"num"      SIZEOF           byte count
 *    F"file"     FILENAME         binary file path
 *    "str"       STRING_DELIMITED plain quoted string
 *    word        STRING_RAW       unquoted string
 *
 *  Colour mapping (Dracula palette — shared with ScriptHighlighter):
 *  ────────────────────────────────────────────────────────────────────
 *  >  direction prefix              #ff5555  red    + bold
 *  <  direction prefix              #50fa7b  green  + bold
 *  !  delay prefix                  #ffb86c  amber  + bold
 *  delay number                     #bd93f9  purple
 *  delay unit (sec / ms / us)       #8be9fd  cyan
 *  |  pipe separator                #6272a4  slate
 *  H  X  prefix letter              #ff79c6  pink   + bold
 *  R  prefix letter                 #ffb86c  amber  + bold
 *  T  L  prefix letter              #8be9fd  cyan   + bold
 *  S  prefix letter                 #8be9fd  cyan   + bold
 *  F  prefix letter                 #ffb86c  amber  + bold
 *  All typed-token "..." content    #f1fa8c  yellow  (= plain string colour)
 *  "plain string"                   #f1fa8c  yellow
 *  $VAR  macro variable             #8be9fd  cyan
 *  NAME  in  NAME :=                #ff79c6  pink   + bold
 *  :=  operator                     #ff79c6  pink
 *  # comment                        #6272a4  slate
 *  --- / !-- block delimiters       #6272a4  slate  + italic
 *
 *  Quote-region protection: whole-match rules (numeric literals, $VAR, …)
 *  are suppressed inside "..." regions so they never overwrite string content.
 *  Sub-match rules (captureGroup > 0) are exempt — they intentionally target
 *  prefix letters and content that sits inside or adjacent to quotes.
 */
class CommScriptHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit CommScriptHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
        int                captureGroup = 0;
    };
    QVector<Rule> m_rules;

    // Block comment state (reuses same --- / !-- delimiters as main script)
    QRegularExpression m_blockStart;   // ^---
    QRegularExpression m_blockEnd;     // ^!--
    QTextCharFormat    m_commentFmt;
    QTextCharFormat    m_delimFmt;

    static QTextCharFormat fmt(const QString &hex,
                               bool bold   = false,
                               bool italic = false);
};
