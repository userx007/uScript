#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

/**
 * @brief Syntax highlighter for the uscript language.
 *
 * Faithfully mirrors the rules in uscript.sublime-syntax:
 *
 *  Scope mapping → colour (Dracula palette on dark bg):
 *  ─────────────────────────────────────────────────────
 *  comment.line / comment.block          #6272a4  slate
 *  comment block delimiters (--- / !--)  #6272a4  slate  + italic
 *  entity.name.constant  (NAME :=)       #ff79c6  pink   + bold
 *  entity.name.type.array (NAME [=)      #8be9fd  cyan
 *  variable.other  ($VAR, $ARR.$IDX)     #8be9fd  cyan
 *  keyword.operator.assignment (:= ?= [=)#ff79c6  pink
 *  entity.name.namespace.plugin (PLUG.)  #50fa7b  green  + bold
 *  support.function.plugin (.CMD)        #ffb86c  amber
 *  keyword.control (LOAD_PLUGIN IF GOTO…)#ff79c6  pink   + bold
 *  support.function (PRINT DELAY MATH…)  #50fa7b  green
 *  support.function.debug (BREAKPOINT)   #ff5555  red    + bold
 *  keyword.other.eval (EVAL)             #ff79c6  pink
 *  storage.type.eval (:NUM :STR …)       #8be9fd  cyan
 *  keyword.operator.comparison (== != …) #ff79c6  pink
 *  keyword.operator.logical (AND OR NOT) #ff79c6  pink
 *  entity.name.label (the label name)    #f1fa8c  yellow
 *  constant.numeric (numbers, versions)  #bd93f9  purple
 *  string.quoted.double / token content  #f1fa8c  yellow  ← ALL "..." content
 *  variable.parameter.format (%N)        #ffb86c  amber
 *
 *  Typed-token decorators (shared palette with CommScriptHighlighter):
 *  ─────────────────────────────────────────────────────────────────────
 *  H  X  prefix letter                   #ff79c6  pink   + bold
 *  R  prefix letter                      #ffb86c  amber  + bold
 *  T  L  prefix letter                   #8be9fd  cyan   + bold
 *  S  prefix letter                      #8be9fd  cyan   + bold
 *  F  prefix letter                      #ffb86c  amber  + bold
 *  All typed-token quoted content        #f1fa8c  yellow  (= C_STRING)
 *
 *  Block comments (--- … !--) are multi-line and handled via
 *  previousBlockState / setCurrentBlockState.
 *
 *  Quote-region protection: whole-match rules (numeric literals, $VAR, …)
 *  are suppressed inside "..." regions so they never overwrite string content.
 *  Sub-match rules (captureGroup > 0) are exempt — they intentionally target
 *  prefix letters and content that sits inside or adjacent to quotes.
 */
class ScriptHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit ScriptHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    // ── Single-line rules applied in order ───────────────────────────────
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
        int                captureGroup = 0;  // 0 = whole match
    };
    QVector<Rule> m_rules;

    // ── Multi-line block comment state ────────────────────────────────────
    // State 1 = inside a block comment (between --- and !--)
    QRegularExpression m_blockCommentStart;   // ^---
    QRegularExpression m_blockCommentEnd;     // ^!--
    QTextCharFormat    m_blockCommentFmt;
    QTextCharFormat    m_blockDelimFmt;

    // ── Helpers ───────────────────────────────────────────────────────────
    static QTextCharFormat fmt(const QString &hex,
                               bool bold   = false,
                               bool italic = false);
};
