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
static constexpr auto C_XTRA_SEP   = "#ffb86c";   // amber  — ~ xtra_params separator (same family as ! modifier sigil)
static constexpr auto C_XTRA_PARAM = "#ff79c6";   // pink   — xtra_param values (same family as := and F; all denote addressing/resources)
static constexpr auto C_XTRA_SLASH = "#6272a4";   // slate  — / per-op param separator (same as |; both are structural separators)

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

    // ── 4. xtra_params extension  ~ param  /  ~ param1 / param2 ─────────
    //
    // The ~ separator is only valid after the expression(s) on a comm line,
    // so we anchor all three rules to require ~ to appear somewhere on the
    // line before them.  Rules are applied after the block/line-comment and
    // quoted-region guards in highlightBlock(), so we don't need to worry
    // about ~ inside comments or strings.
    //
    // Rule order here is significant: the slash rule is added first so that
    // the param-value rule (which uses a greedy [^\s/]+) can paint over the
    // slash region cleanly if needed.  In practice they target different
    // character positions, but the explicit order documents intent.

    // 4a. ~ separator — amber, bold  (same family as ! modifier sigil)
    addRule(R"(~)", fmt(C_XTRA_SEP, /*bold=*/true));

    // 4b. / per-op param separator — slate  (same as |; structural separator)
    //     Only matches a bare / that is preceded by non-whitespace and
    //     followed by optional whitespace + a param, preventing false
    //     positives on e.g. file paths inside F"..." (those are inside
    //     quoted regions and are guarded by highlightBlock anyway).
    //     We use a lookahead to require at least one non-space char after
    //     the slash so an isolated trailing / is still flagged as an error
    //     visually (it gets no colour, staying as default text).
    addRule(R"((?<=\S)\s*(/)\s*(?=\S))", fmt(C_XTRA_SLASH), /*cap=*/1);

    // 4c. xtra_param values — pink  (same family as := and F prefix;
    //     both denote addressing / resource identifiers)
    //     Matches one or two whitespace-separated tokens that follow ~.
    //     Each token is [^\s/|#]+ — anything that is not a structural
    //     separator, pipe, or comment character.
    //     Two separate rules are used (one per token) because captureGroup
    //     rules in highlightBlock() each target a single capture group, and
    //     we want both param1 and param2 painted independently without a
    //     single complex regex spanning the whole suffix.
    //
    //     param1: the first non-space token after ~
    addRule(R"(~\s*([^\s/|#]+))", fmt(C_XTRA_PARAM), /*cap=*/1);
    //     param2: the first non-space token after the / (when present)
    addRule(R"(~[^/\n]*/\s*([^\s|#]+))", fmt(C_XTRA_PARAM), /*cap=*/1);

    // ── 5. Direction prefixes  >  <  ! ───────────────────────────────────
    //  > send → red  ·  < recv → green  (red↔green complement pair)
    //  ! delay → amber
    //  Block comment delimiters (--- / !--) are caught in highlightBlock
    //  before rules run, so ! here only matches the delay prefix.
    addRule(R"(^\s*(>))",       fmt(C_SEND,      true), 1);
    addRule(R"(^\s*(<))",       fmt(C_RECV,      true), 1);
    addRule(R"(^\s*(!)(?!--))", fmt(C_DELAY_PFX, true), 1);

    // ── 6. Delay value and unit ───────────────────────────────────────────
    //  number → purple  ·  unit → cyan  (warm › cool progression: amber ! → purple N → cyan unit)
    //  Units recognised: sec  ms  us
    addRule(R"(^\s*!\s*(\d+))",    fmt(C_DELAY_NUM),  1);
    addRule(R"(\b(sec|ms|us)\b)",  fmt(C_DELAY_UNIT));

    // ── 7. Typed-token decorators  H/X/R/T/L/S/F"…"  — from base ────────
    //  H X → red  ·  R → amber  ·  T L → cyan  ·  S → purple  ·  F → pink
    addTypedTokenDecorators();

    // ── 8. Plain string  "..."  ───────────────────────────────────────────
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 9. Numeric literals ───────────────────────────────────────────────
    //  Whole-match rule — suppressed inside quoted regions by highlightBlock.
    addRule(R"(\b\d+\b)", fmt(C_DELAY_NUM));
}
