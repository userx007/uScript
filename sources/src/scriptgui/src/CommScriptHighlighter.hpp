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
 *  Colour mapping (Dracula palette, distinct from main-script highlighter):
 *    >  direction             #50fa7b  green  bold
 *    <  direction             #ff79c6  pink   bold
 *    !  delay prefix          #ffb86c  amber  bold
 *    delay number             #bd93f9  purple
 *    delay unit (sec/ms/us)   #8be9fd  cyan
 *    |  pipe separator        #6272a4  slate
 *    H  X  prefix             #ff79c6  pink
 *    H"/X" hex content        #bd93f9  purple
 *    R  prefix                #ffb86c  amber
 *    R" regex content         #f1fa8c  yellow
 *    T  L  prefix             #8be9fd  cyan
 *    T"/L" token content      #f1fa8c  yellow
 *    S  prefix                #8be9fd  cyan
 *    S" size content          #bd93f9  purple
 *    F  prefix                #ffb86c  amber
 *    F" filename content      #f8f8f2  white
 *    "plain string"           #f1fa8c  yellow
 *    $VAR  macro variable     #8be9fd  cyan
 *    NAME :=  definition      #ff79c6  pink (+bold for name)
 *    # comment                #6272a4  slate
 *    --- / !-- delimiters     #6272a4  slate  italic
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
