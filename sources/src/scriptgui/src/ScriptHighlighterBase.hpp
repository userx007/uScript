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
 *      addXtraParamRules()       — ~ param / param2  (amber ~ · pink values · slate /)
 *      addNumericLiteralRule()   — standalone hex/bin/oct/dec integer literals
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
    // ── Shared separator colour ──────────────────────────────────────────
    // Every purely structural separator across both highlighters —
    // CommScriptHighlighter's | direction/param pipe, ScriptHighlighter's
    // REPEAT range "," and MATH|HEX "|", and the shared xtra_params "~" and
    // "/" — uses this single colour so separators read as one visual
    // category regardless of which symbol or which highlighter draws them.
    // Deliberately excluded: the ':' in a plugin:instance name (e.g.
    // UART:1) is never given a separate colour at all — it stays part of
    // the green/bold plugin-name token it's embedded in (see
    // ScriptHighlighter's LOAD_PLUGIN argument rule), because splitting
    // it out would visually sever the instance index from the plugin it
    // belongs to.
    static constexpr auto C_SEPARATOR = "#6272a4";   // slate

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

    /**
     * Adds the xtra_params extension:  ~ param1 / param2
     *   ~          slate  (unified separator colour — see C_SEPARATOR)
     *   param1     pink   (first token after ~)
     *   /          slate  (unified separator colour — see C_SEPARATOR)
     *   param2     pink   (first token after /)
     *
     * Must be called AFTER numeric-literal rules so that the capture-group
     * rules here (last-write-wins) paint param values pink even when they
     * look like hex or decimal literals (e.g. 0x125, 0x44).
     */
    void addXtraParamRules();

    /**
     * Adds the shared "standalone numeric literal" rule, recognising every
     * integer base handled by numeric::detect_sign_and_base() (uNumeric.hpp):
     *   [ '+' | '-' ]  ( "0x"|"0X" hex-digits | "0b"|"0B" binary-digits
     *                  | "0o"|"0O" octal-digits | decimal-digits )
     *
     * A single alternation (rather than separate hex/decimal rules) so a
     * prefixed literal like 0x1A or 0b101 is always consumed as one token —
     * the decimal alternative can never "split" it at an internal boundary.
     *
     * Excluded automatically (whole-match rule, so the usual protections in
     * highlightBlock() apply):
     *   - anything inside "..." (protects H"…", S"…", F"…", X"…" content,
     *     and plain quoted strings, exactly like other whole-match rules)
     *   - digits glued to an identifier on either side (e.g. PLUGIN123,
     *     COMMAND9 — a leading/trailing letter, digit, or '_' blocks the match)
     *   - digits right after ':'  (plugin instance index, e.g. UART:1)
     *   - digits right after '%'  (FORMAT token, e.g. %3)
     * The sign is only treated as part of the literal when it isn't glued to
     * a preceding value (so "5-3" highlights "5" and "3" separately as a
     * subtraction, while " -42" highlights the whole signed literal).
     *
     * Colour is supplied by the caller so each derived highlighter keeps its
     * own local palette entry — only the recognition grammar is shared.
     */
    void addNumericLiteralRule(const QTextCharFormat &format);

    // ── Core override — not to be re-overridden by derived classes ────────
    void highlightBlock(const QString &text) final;
};
