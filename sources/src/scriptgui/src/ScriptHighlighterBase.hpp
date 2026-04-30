#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

/**
 * @brief Shared base for ScriptHighlighter and CommScriptHighlighter.
 *
 * Owns:
 *  - The Rule struct and m_rules vector.
 *  - The fmt() helper.
 *  - The full highlightBlock() implementation (block comment state machine,
 *    line-comment early-exit, mid-line comment guard, quote-region protection,
 *    and rule-application loop).
 *  - Helper methods for rules that appear in both derived highlighters:
 *      addMacroAssignRule()      — NAME :=  (purple + bold name, pink op)
 *      addMacroVariableRule()    — $VAR / $ARR.$IDX  (cyan)
 *      addTypedTokenDecorators() — H/X/R/T/L/S/F PREFIX"content"
 *                                  (type-specific bold prefix + yellow content)
 *
 * Derived classes call these helpers from their constructors in addition to
 * appending their own highlighter-specific rules to m_rules.
 *
 *  Colour ownership — shared rules (Dracula-inspired palette):
 *  ──────────────────────────────────────────────────────────────────────
 *  Token / role                     Hex       Colour   Style
 *  ──────────────────────────────────────────────────────────────────────
 *  NAME in NAME :=                  #bd93f9   purple   bold
 *  := operator                      #ff79c6   pink     (unified with ?= and [=)
 *  $VAR / $ARR.$IDX                 #8be9fd   cyan
 *  H  X  prefix letter              #ff5555   red      bold   (raw bytes)
 *  R  prefix letter                 #ffb86c   amber    bold   (pattern / regex)
 *  T  L  prefix letter              #8be9fd   cyan     bold   (stream tokens)
 *  S  prefix letter                 #bd93f9   purple   bold   (numeric size)
 *  F  prefix letter                 #ff79c6   pink     bold   (file resource)
 *  All "..." content                #f1fa8c   yellow          (RESERVED — strings only)
 *  # comment                        #6272a4   slate
 *  --- / !-- delimiters             #6272a4   slate    italic
 *  ──────────────────────────────────────────────────────────────────────
 */
class ScriptHighlighterBase : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit ScriptHighlighterBase(QTextDocument *parent = nullptr);

protected:
    // ── Rule table ────────────────────────────────────────────────────────
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
        int                captureGroup = 0;   // 0 = whole match
    };
    QVector<Rule> m_rules;

    // ── Block comment state (--- … !--) ───────────────────────────────────
    QRegularExpression m_blockStart;   // ^---
    QRegularExpression m_blockEnd;     // ^!--
    QTextCharFormat    m_commentFmt;   // slate
    QTextCharFormat    m_delimFmt;     // slate + italic

    // ── Helpers ───────────────────────────────────────────────────────────
    static QTextCharFormat fmt(const QString &hex,
                               bool bold   = false,
                               bool italic = false);

    /** Appends a whole-match (captureGroup=0) rule. */
    void addRule(const QString &pattern, const QTextCharFormat &f, int cap = 0);

    /**
     * Adds the  NAME :=  macro-definition rule pair.
     *   group 1 — constant name  (purple + bold)
     *   group 2 — :=             (pink — same family as ?= and [=)
     */
    void addMacroAssignRule();

    /**
     * Adds the  $VAR  and  $ARR.$IDX  macro-variable rule (cyan).
     */
    void addMacroVariableRule();

    /**
     * Adds all seven typed-token decorator rule pairs:
     *   H"…"  X"…"  R"…"  T"…"  L"…"  S"…"  F"…"
     * Each decorator contributes two rules:
     *   - prefix letter  (bold, type-specific colour — see table in class doc)
     *   - "…" content    (yellow — same as a plain string)
     */
    void addTypedTokenDecorators();

    // ── Core override — not to be re-overridden by derived classes ────────
    void highlightBlock(const QString &text) final;
};
