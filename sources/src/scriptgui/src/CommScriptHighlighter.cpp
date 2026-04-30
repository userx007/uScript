#include "CommScriptHighlighter.hpp"

// ─── comm-script-specific colour palette ─────────────────────────────────────
// Colours shared with the base (STRING/yellow, DEF_NAME/purple, DEF_OP/pink,
// VAR/cyan, and all typed-token prefix letters) are defined in
// ScriptHighlighterBase.cpp and not repeated here.
static constexpr auto C_SEND       = "#ff5555";   // red    — > direction
static constexpr auto C_RECV       = "#50fa7b";   // green  — < direction
static constexpr auto C_DELAY_PFX  = "#ffb86c";   // amber  — ! prefix
static constexpr auto C_DELAY_NUM  = "#bd93f9";   // purple — delay / numeric literals
static constexpr auto C_DELAY_UNIT = "#8be9fd";   // cyan   — sec / ms / us
static constexpr auto C_PIPE       = "#6272a4";   // slate  — | separator
static constexpr auto C_STRING     = "#f1fa8c";   // yellow — "..." (plain strings)

// ─────────────────────────────────────────────────────────────────────────────
CommScriptHighlighter::CommScriptHighlighter(QTextDocument *parent)
    : ScriptHighlighterBase(parent)
{
    using RE = QRegularExpression;

    // ── 1. Macro definition  NAME :=  — from base ─────────────────────────
    //  NAME → purple + bold  ·  := → pink
    addMacroAssignRule();

    // ── 2. Macro variables  $VAR  $ARR.$IDX  — from base ─────────────────
    //  Both segments → cyan
    addMacroVariableRule();

    // ── 3. Pipe separator  | ─────────────────────────────────────────────
    addRule(R"(\|)", fmt(C_PIPE));

    // ── 4. Direction prefixes  >  <  ! ───────────────────────────────────
    //  > send → red  ·  < recv → green  (red↔green complement pair)
    //  ! delay → amber
    //  Block comment delimiters (--- / !--) are caught in highlightBlock
    //  before rules run, so ! here only matches the delay prefix.
    addRule(R"(^\s*(>))",       fmt(C_SEND,      true), 1);
    addRule(R"(^\s*(<))",       fmt(C_RECV,      true), 1);
    addRule(R"(^\s*(!)(?!--))", fmt(C_DELAY_PFX, true), 1);

    // ── 5. Delay value and unit ───────────────────────────────────────────
    //  number → purple  ·  unit → cyan  (warm › cool progression: amber ! → purple N → cyan unit)
    //  Units recognised: sec  ms  us
    addRule(R"(^\s*!\s*(\d+))",    fmt(C_DELAY_NUM),  1);
    addRule(R"(\b(sec|ms|us)\b)",  fmt(C_DELAY_UNIT));

    // ── 6. Typed-token decorators  H/X/R/T/L/S/F"…"  — from base ────────
    //  H X → red  ·  R → amber  ·  T L → cyan  ·  S → purple  ·  F → pink
    addTypedTokenDecorators();

    // ── 7. Plain string  "..."  ───────────────────────────────────────────
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 8. Numeric literals ───────────────────────────────────────────────
    //  Whole-match rule — suppressed inside quoted regions by highlightBlock.
    addRule(R"(\b\d+\b)", fmt(C_DELAY_NUM));
}
