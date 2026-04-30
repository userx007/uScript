#include "ScriptHighlighter.hpp"

// ─── uscript-specific colour palette ─────────────────────────────────────────
// Colours shared with the base (STRING, DEF_NAME/purple, DEF_OP/pink, VAR/cyan,
// and typed-token prefix letters) are defined in ScriptHighlighterBase.cpp.
//
// Colour ownership for this file (Dracula-inspired palette):
//   cyan        #8be9fd  — NAME in NAME ?=  ·  :BOOL/:NUM/:STR/:VER storage types
//   amber       #ffb86c  — NAME in NAME [=  ·  %N format tokens
//   pink        #ff79c6  — ?= / [= operators (bold)  ·  all control-flow keywords
//                          (unified: := operator in base uses the same pink)
//   green       #20a39e  — PLUGIN. namespace  ·  LOAD_PLUGIN argument
//   red         #ff5555  — .COMMAND  ·  BREAKPOINT
//   periwinkle  #a5b4fc  — native functions (PRINT DELAY FORMAT MATH EVAL)
//   purple      #bd93f9  — label names  ·  numeric/version literals
//                          (same as base C_DEF_NAME — values share the colour)
//   yellow      #f1fa8c  — "..." string content  (RESERVED — defined in base)
static constexpr auto C_VAR_NAME  = "#8be9fd";   // cyan       — NAME in NAME ?=
static constexpr auto C_ARR_NAME  = "#ffb86c";   // amber      — NAME in NAME [=
static constexpr auto C_KEYWORD   = "#ff79c6";   // pink       — ?= / [= operators · control-flow
static constexpr auto C_FUNC      = "#a5b4fc";   // periwinkle — PRINT DELAY FORMAT MATH EVAL
static constexpr auto C_DEBUG     = "#ff5555";   // red        — BREAKPOINT
static constexpr auto C_PLUGIN    = "#20a39e";   // green      — PLUGIN. namespace
static constexpr auto C_COMMAND   = "#ff5555";   // red        — .COMMAND (green↔red complement)
static constexpr auto C_STRING    = "#f1fa8c";   // yellow     — "..." (plain strings)
static constexpr auto C_NUMBER    = "#89a1ef";   // blue       — numeric / version literals
static constexpr auto C_FORMAT    = "#ffb86c";   // amber      — %N format tokens
static constexpr auto C_LABEL_NAME= "#bd93f9";   // purple     — label name after LABEL keyword
static constexpr auto C_STORAGE   = "#8be9fd";   // cyan       — :NUM :STR :VER :BOOL

// ─────────────────────────────────────────────────────────────────────────────
ScriptHighlighter::ScriptHighlighter(QTextDocument *parent)
    : ScriptHighlighterBase(parent)
{
    using RE = QRegularExpression;

    // ── 1. Macro definitions ──────────────────────────────────────────────
    //   NAME :=   (purple + bold name, pink op)  — from base
    addMacroAssignRule();

    //   NAME ?=   (cyan + bold name, pink op)
    {
        const QString pat = R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\?=))";
        Rule r2; r2.pattern = RE(pat); r2.format = fmt(C_KEYWORD);
                 r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(pat); r1.format = fmt(C_VAR_NAME, true);
                 r1.captureGroup = 1; m_rules.append(r1);
    }
    //   NAME [=   (amber + bold name, pink op)
    {
        const QString pat = R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[=))";
        Rule r2; r2.pattern = RE(pat); r2.format = fmt(C_KEYWORD);
                 r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(pat); r1.format = fmt(C_ARR_NAME, true);
                 r1.captureGroup = 1; m_rules.append(r1);
    }

    // ── 2. Macro variables  $VAR  $ARR.$IDX  — from base ─────────────────
    addMacroVariableRule();

    // ── 3. Plugin commands  PLUGIN.COMMAND ───────────────────────────────
    //  PLUGIN. namespace → green  ·  .COMMAND → red  (green↔red complement)
    {
        const QString pat = R"(\b([A-Z][A-Z0-9_]*)\.([A-Z][A-Z0-9_]*)\b)";
        Rule rCmd;  rCmd.pattern  = RE(pat); rCmd.format  = fmt(C_COMMAND, true);
                    rCmd.captureGroup  = 2; m_rules.append(rCmd);
        Rule rPlug; rPlug.pattern = RE(pat); rPlug.format = fmt(C_PLUGIN, true);
                    rPlug.captureGroup = 1; m_rules.append(rPlug);
    }

    // ── 4. Control keywords ───────────────────────────────────────────────
    //  All control-flow keywords share pink (same as ?= / [= operators).
    for (const QString &kw : {
            "LOAD_PLUGIN", "IF", "GOTO", "REPEAT", "END_REPEAT",
            "UNTIL", "BREAK", "CONTINUE" })
        addRule(QString(R"(\b%1\b)").arg(kw), fmt(C_KEYWORD, true));

    // LOAD_PLUGIN argument — plugin name in green + bold
    {
        Rule r; r.pattern = RE(R"(\bLOAD_PLUGIN\s+([A-Za-z_][A-Za-z0-9_]*))");
                r.format  = fmt(C_PLUGIN, true); r.captureGroup = 1;
        m_rules.append(r);
    }

    // ── 5. LABEL keyword + label name ─────────────────────────────────────
    //  LABEL keyword → pink (same as all other control-flow keywords)
    //  label name    → purple (same family as constant names and numbers)
    {
        const QString pat = R"(\b(LABEL)\s+([A-Za-z_][A-Za-z0-9_]*))";
        Rule rNm; rNm.pattern = RE(pat); rNm.format = fmt(C_LABEL_NAME);
                  rNm.captureGroup = 2; m_rules.append(rNm);
        Rule rKw; rKw.pattern = RE(pat); rKw.format = fmt(C_KEYWORD, true);
                  rKw.captureGroup = 1; m_rules.append(rKw);
    }

    // ── 6. Native functions ───────────────────────────────────────────────
    //  Periwinkle — distinct from pink control-flow and green plugin namespace.
    for (const QString &fn : { "PRINT", "DELAY", "FORMAT", "MATH", "EVAL" })
        addRule(QString(R"(\b%1\b)").arg(fn), fmt(C_FUNC, true));

    // ── 7. Debug ──────────────────────────────────────────────────────────
    addRule(R"(\bBREAKPOINT\b)", fmt(C_DEBUG, true));

    // ── 8. EVAL sub-context ───────────────────────────────────────────────
    addRule(R"(:(NUM|STR|VER|BOOL)\b)", fmt(C_STORAGE));
    addRule(R"(==|!=|>=|<=|>|<)",       fmt(C_KEYWORD));
    addRule(R"(\b(AND|OR|NOT)\b)",      fmt(C_KEYWORD));

    // ── 9. Typed-token decorators  H/X/R/T/L/S/F"…"  — from base ────────
    addTypedTokenDecorators();

    // ── 10. Plain string  "..."  ──────────────────────────────────────────
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 11. Format tokens  %0 %1 … ───────────────────────────────────────
    addRule(R"(%\d+)", fmt(C_FORMAT));

    // ── 12. Version literals  v1.2.3 ─────────────────────────────────────
    addRule(R"(\bv\d+\.\d+(?:\.\d+)*\b)", fmt(C_NUMBER));

    // ── 13. Numeric literals ──────────────────────────────────────────────
    addRule(R"(\b\d+\b)", fmt(C_NUMBER));
}
