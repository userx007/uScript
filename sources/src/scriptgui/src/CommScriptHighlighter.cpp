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
static constexpr auto C_PRINT      = "#a5b4fc";   // periwinkle — @ print directive
                                                   // (same family as core script's PRINT
                                                   //  native function - both are log statements)
// C_XTRA_SEP / C_XTRA_PARAM / C_XTRA_SLASH are defined in ScriptHighlighterBase.cpp

// ─────────────────────────────────────────────────────────────────────────────
CommScriptHighlighter::CommScriptHighlighter(QTextDocument *parent)
    : ScriptHighlighterBase(parent)
{
    using RE = QRegularExpression;

    // ── 1. Print message content  @ MESSAGE  — base coat ─────────────────
    //  Colours the whole message body periwinkle + italic (dimmer than the
    //  bold '@' prefix below - same visual relationship as m_commentFmt vs
    //  m_delimFmt in the base class: same hue, italic marks the "quieter"
    //  role). Deliberately added FIRST so it only lays down a base colour;
    //  later rules (macro variables, typed-token decorators, numeric
    //  literals) still win last-write-wins and highlight any $VAR, H"…",
    //  or number that happens to appear inside the printed message.
    //  Stops before an unquoted trailing '#' comment, mirroring the
    //  mid-line comment guard in highlightBlock() so the two never fight
    //  over the same characters.
    addRule(R"(^\s*@\s*(.*?)\s*(?:#.*)?$)", fmt(C_PRINT, false, true), 1);

    // ── 2. Macro definition  NAME :=  — from base ─────────────────────────
    //  NAME → purple + bold  ·  := → pink
    addMacroAssignRule();

    // ── 3. Macro variables  $VAR  $ARR.$IDX  — from base ─────────────────
    //  Both segments → cyan
    addMacroVariableRule();

    // ── 4. Pipe separator  | ─────────────────────────────────────────────
    addRule(R"(\|)", fmt(C_PIPE));

    // ── 5. Direction prefixes  >  <  !  @ ────────────────────────────────
    //  > send → red  ·  < recv → green  (red↔green complement pair)
    //  ! delay → amber  ·  @ print → periwinkle (same family as core
    //  script's PRINT native function - both are log statements)
    //  Block comment delimiters (--- / !--) are caught in highlightBlock
    //  before rules run, so ! here only matches the delay prefix.
    addRule(R"(^\s*(>))",       fmt(C_SEND,      true), 1);
    addRule(R"(^\s*(<))",       fmt(C_RECV,      true), 1);
    addRule(R"(^\s*(!)(?!--))", fmt(C_DELAY_PFX, true), 1);
    addRule(R"(^\s*(@))",       fmt(C_PRINT,     true), 1);

    // ── 6. Delay value and unit ───────────────────────────────────────────
    //  number → purple  ·  unit → cyan  (warm › cool progression: amber ! → purple N → cyan unit)
    //  Units recognised: sec  ms  us
    addRule(R"(^\s*!\s*(\d+))",   fmt(C_DELAY_NUM),  1);
    addRule(R"(\b(sec|ms|us)\b)", fmt(C_DELAY_UNIT));

    // ── 7. Typed-token decorators  H/X/R/T/L/S/F"…"  — from base ────────
    //  H X → red  ·  R → amber  ·  T L → cyan  ·  S → purple  ·  F → pink
    addTypedTokenDecorators();

    // ── 8. Plain string  "..."  ───────────────────────────────────────────
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 9. Numeric literals ───────────────────────────────────────────────
    //  Whole-match rules — suppressed inside quoted regions by highlightBlock.
    //  Hex (0x…) must come first so the full token is consumed before the
    //  decimal rule can split it at the 'x' word boundary.
    addRule(R"(\b0[xX][0-9A-Fa-f]+\b)", fmt(C_DELAY_NUM));   // hex literals
    addRule(R"(\b\d+\b)",               fmt(C_DELAY_NUM));    // decimal literals

    // ── 10. xtra_params extension  ~ param  /  ~ param1 / param2 ─────────
    //  Shared with ScriptHighlighter — rules live in the base so both
    //  highlighters cover this syntax regardless of context.
    //  Must be last: capture-group rules here paint over numeric colours
    //  (last-write-wins), so addXtraParamRules() must follow rule 9.
    addXtraParamRules();
}
