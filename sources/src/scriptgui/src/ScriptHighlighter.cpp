#include "ScriptHighlighter.hpp"

// ─── uscript-specific colour palette ─────────────────────────────────────────
// Colours shared with the base (STRING, DEF_NAME, DEF_OP, VAR, prefix letters)
// are defined in ScriptHighlighterBase.cpp and not repeated here.
static constexpr auto C_VARIABLE = "#8be9fd";   // cyan   — ?= / [= variable name
static constexpr auto C_KEYWORD  = "#ff79c6";   // pink   — operators, control, comparison
static constexpr auto C_FUNC     = "#50fa7b";   // green  — native functions
static constexpr auto C_DEBUG    = "#ff5555";   // red    — BREAKPOINT
static constexpr auto C_PLUGIN   = "#50fa7b";   // green  — PLUGIN. namespace
static constexpr auto C_COMMAND  = "#ffb86c";   // amber  — .COMMAND
static constexpr auto C_STRING   = "#f1fa8c";   // yellow — "..." and label names
static constexpr auto C_NUMBER   = "#bd93f9";   // purple — numeric / version literals
static constexpr auto C_FORMAT   = "#ffb86c";   // amber  — %N tokens
static constexpr auto C_LABEL_KW = "#ff79c6";   // pink   — LABEL keyword
static constexpr auto C_STORAGE  = "#8be9fd";   // cyan   — :NUM :STR …

// ─────────────────────────────────────────────────────────────────────────────
ScriptHighlighter::ScriptHighlighter(QTextDocument *parent)
    : ScriptHighlighterBase(parent)
{
    using RE = QRegularExpression;

    // ── 1. Macro definitions ──────────────────────────────────────────────
    //   NAME :=   (pink bold name + pink op)  — from base
    addMacroAssignRule();

    //   NAME ?=   (cyan name + pink op)
    {
        const QString pat = R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\?=))";
        Rule r2; r2.pattern = RE(pat); r2.format = fmt(C_KEYWORD);
                 r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(pat); r1.format = fmt(C_VARIABLE);
                 r1.captureGroup = 1; m_rules.append(r1);
    }
    //   NAME [=   (cyan name + pink op)
    {
        const QString pat = R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[=))";
        Rule r2; r2.pattern = RE(pat); r2.format = fmt(C_KEYWORD);
                 r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(pat); r1.format = fmt(C_VARIABLE);
                 r1.captureGroup = 1; m_rules.append(r1);
    }

    // ── 2. Macro variables  $VAR  $ARR.$IDX  — from base ─────────────────
    addMacroVariableRule();

    // ── 3. Plugin commands  PLUGIN.COMMAND ───────────────────────────────
    {
        const QString pat = R"(\b([A-Z][A-Z0-9_]*)\.([A-Z][A-Z0-9_]*)\b)";
        Rule rCmd;  rCmd.pattern  = RE(pat); rCmd.format  = fmt(C_COMMAND);
                    rCmd.captureGroup  = 2; m_rules.append(rCmd);
        Rule rPlug; rPlug.pattern = RE(pat); rPlug.format = fmt(C_PLUGIN, true);
                    rPlug.captureGroup = 1; m_rules.append(rPlug);
    }

    // ── 4. Control keywords ───────────────────────────────────────────────
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
    {
        const QString pat = R"(\b(LABEL)\s+([A-Za-z_][A-Za-z0-9_]*))";
        Rule rNm; rNm.pattern = RE(pat); rNm.format = fmt(C_STRING);
                  rNm.captureGroup = 2; m_rules.append(rNm);
        Rule rKw; rKw.pattern = RE(pat); rKw.format = fmt(C_LABEL_KW, true);
                  rKw.captureGroup = 1; m_rules.append(rKw);
    }

    // ── 6. Native functions ───────────────────────────────────────────────
    for (const QString &fn : { "PRINT", "DELAY", "FORMAT", "MATH", "EVAL" })
        addRule(QString(R"(\b%1\b)").arg(fn), fmt(C_FUNC));

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
