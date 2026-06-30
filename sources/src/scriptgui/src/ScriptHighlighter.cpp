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
//   teal        #62d6d6  — INCLUDE keyword  (distinct from PLUGIN green and control-flow pink)
//   sky         #87ceeb  — INCLUDE path string  (cooler than yellow, warmer than cyan)
//   slate       #6272a4  — | separator before MATH's HEX post-processor (reuses comment colour)
//                          (HEX keyword reuses C_FUNC periwinkle; LE/BE reuses C_STORAGE cyan;
//                          width digits get their own purple, same family as the S"n" size prefix)
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
static constexpr auto C_THREAD    = "#50fa7b";   // bright-green — & thread suffix ("go" signal)
static constexpr auto C_INCLUDE_KW   = "#62d6d6"; // teal      — INCLUDE keyword
static constexpr auto C_INCLUDE_PATH = "#87ceeb";  // sky-blue  — "path" argument
static constexpr auto C_HEX_PIPE     = "#6272a4"; // slate     — | before MATH's HEX post-processor
static constexpr auto C_HEX_WIDTH    = "#bd93f9"; // purple    — HEX width digits (8/16/32/64/128)
                                                    //  (HEX keyword itself reuses C_FUNC; the
                                                    //   LE/BE endian token reuses C_STORAGE)

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

    // ── 3. INCLUDE "path"  ────────────────────────────────────────────────
    //  Standalone directive at the start of the line:
    //    INCLUDE "relative/or/absolute/path"
    //
    //  Two rules on the same pattern:
    //    group 1 — the INCLUDE keyword  → teal + bold
    //    group 2 — the "path" (with quotes) → sky-blue
    //
    //  The keyword string is taken from SCRIPT_INCLUDE_KEYWORD (uSharedConfig.hpp)
    //  so it tracks any future rename without touching this file.
    //
    //  Rule ordering: path rule first so it paints under the keyword rule;
    //  both are whole-match sub-captures so they don't interfere with each
    //  other in practice — but explicit ordering documents intent.
    {
        const QString kw   = QString::fromLatin1("INCLUDE");
        const QString pat  = QString(R"re(^\s*(%1)\s+("(?:[^"\\]|\\.)*"))re").arg(kw);
        Rule rPath; rPath.pattern = RE(pat); rPath.format = fmt(C_INCLUDE_PATH);
                    rPath.captureGroup = 2; m_rules.append(rPath);
        Rule rKw;   rKw.pattern   = RE(pat); rKw.format   = fmt(C_INCLUDE_KW, true);
                    rKw.captureGroup = 1; m_rules.append(rKw);
    }

    // ── 4. Plugin commands  PLUGIN.COMMAND  and  PLUGIN:N.COMMAND ────────
    //  PLUGIN[:N]. namespace → green  ·  .COMMAND → red
    {
        const QString pat = R"(\b([A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?)\.([A-Z][A-Z0-9_]*)\b)";
        Rule rCmd;  rCmd.pattern  = RE(pat); rCmd.format  = fmt(C_COMMAND, true);
                    rCmd.captureGroup  = 2; m_rules.append(rCmd);
        Rule rPlug; rPlug.pattern = RE(pat); rPlug.format = fmt(C_PLUGIN, true);
                    rPlug.captureGroup = 1; m_rules.append(rPlug);
    }

    // ── 5. Control keywords ───────────────────────────────────────────────
    //  All control-flow keywords share pink (same as ?= / [= operators).
    for (const QString &kw : {
            "LOAD_PLUGIN", "IF", "GOTO", "REPEAT", "END_REPEAT",
            "UNTIL", "BREAK", "CONTINUE" })
        addRule(QString(R"(\b%1\b)").arg(kw), fmt(C_KEYWORD, true));

    // LOAD_PLUGIN argument — full instance name (UART or UART:1) in green + bold
    {
        Rule r; r.pattern = RE(R"(\bLOAD_PLUGIN\s+([A-Za-z_][A-Za-z0-9_]*(?::[1-9][0-9]*)?))");
                r.format  = fmt(C_PLUGIN, true); r.captureGroup = 1;
        m_rules.append(r);
    }

    // ── 6. LABEL keyword + label name ─────────────────────────────────────
    //  LABEL keyword → pink (same as all other control-flow keywords)
    //  label name    → purple (same family as constant names and numbers)
    {
        const QString pat = R"(\b(LABEL)\s+([A-Za-z_][A-Za-z0-9_]*))";
        Rule rNm; rNm.pattern = RE(pat); rNm.format = fmt(C_LABEL_NAME);
                  rNm.captureGroup = 2; m_rules.append(rNm);
        Rule rKw; rKw.pattern = RE(pat); rKw.format = fmt(C_KEYWORD, true);
                  rKw.captureGroup = 1; m_rules.append(rKw);
    }

    // ── 7. Native functions ───────────────────────────────────────────────
    //  Periwinkle — distinct from pink control-flow and green plugin namespace.
    for (const QString &fn : { "PRINT", "DELAY", "FORMAT", "MATH", "EVAL" })
        addRule(QString(R"(\b%1\b)").arg(fn), fmt(C_FUNC, true));

    // ── 8. Debug ──────────────────────────────────────────────────────────
    addRule(R"(\bBREAKPOINT\b)", fmt(C_DEBUG, true));

    // ── 9. EVAL sub-context ───────────────────────────────────────────────
    addRule(R"(:(NUM|STR|VER|BOOL)\b)", fmt(C_STORAGE));
    addRule(R"(==|!=|>=|<=|>|<)",       fmt(C_KEYWORD));
    addRule(R"(\b(AND|OR|NOT)\b)",      fmt(C_KEYWORD));

    // ── 10. Typed-token decorators  H/X/R/T/L/S/F"…"  — from base ────────
    addTypedTokenDecorators();

    // ── 11. Plain string  "..."  ──────────────────────────────────────────
    //  Applied AFTER the INCLUDE rule (step 3) so the INCLUDE path string gets
    //  sky-blue (from the capture-group rule) and only unadorned strings get
    //  yellow here.  In practice highlightBlock applies all rules regardless of
    //  order — the last rule to touch a range wins.  The INCLUDE capture-group
    //  rules (captureGroup > 0) are exempt from the quoted-region guard, so
    //  they correctly repaint the path even after this whole-match string rule
    //  has painted it yellow.  Putting this rule after step 3 is therefore
    //  irrelevant to correctness, but the comment documents the intent.
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 12. Format tokens  %0 %1 … ───────────────────────────────────────
    addRule(R"(%\d+)", fmt(C_FORMAT));

    // ── 13. Version literals  v1.2.3 ─────────────────────────────────────
    addRule(R"(\bv\d+\.\d+(?:\.\d+)*\b)", fmt(C_NUMBER));

    // ── 14. Numeric literals ──────────────────────────────────────────────
    // (?<!:) prevents the instance index in instanced plugin names
    // (e.g. the "1" in UART:1) from being recoloured over the plugin green.
    addRule(R"((?<!:)\b\d+\b)", fmt(C_NUMBER));

    // ── 14b. MATH | HEX post-processor ────────────────────────────────────
    //  Optional trailing post-processor on a MATH expression:
    //    name ?= MATH expr | HEX[_<width>][_<endian>]
    //  Mirrors the validator's matcher (uScriptValidator.cpp):
    //    \|\s*HEX(?:_(8|16|32|64|128))?(?:_(LE|BE))?\s*$
    //
    //  group 1 — |        structural separator → slate
    //                      (same family as the comm-script pipe and the
    //                      xtra_params '/' — both are structural)
    //  group 2 — HEX      keyword               → periwinkle, bold
    //                      (same family as the other native functions:
    //                      PRINT/DELAY/FORMAT/MATH/EVAL — HEX is a
    //                      post-processing function applied to the result)
    //  group 3 — width digits (8/16/32/64/128)   → purple
    //                      (same family as the S"n" byte-count prefix in
    //                      addTypedTokenDecorators() — both describe a size)
    //  group 4 — endian (LE/BE)                  → cyan
    //                      (same family as :NUM/:STR/:VER/:BOOL — both
    //                      describe how a value is represented)
    //
    //  Anchored with \s*$ so this only matches the trailing post-processor,
    //  not an unrelated '|' that might appear earlier in a MATH expression
    //  (e.g. bitwise OR), and must come after step 14 (numeric literals) so
    //  this capture-group rule (last-write-wins) repaints the width digits
    //  purple instead of leaving them coloured by the generic \d+ rule.
    {
        const QString pat =
            R"((\|)\s*(HEX)(?:_(8|16|32|64|128))?(?:_(LE|BE))?\s*$)";
        Rule rPipe; rPipe.pattern = RE(pat); rPipe.format = fmt(C_HEX_PIPE);
                    rPipe.captureGroup = 1; m_rules.append(rPipe);
        Rule rKw;   rKw.pattern   = RE(pat); rKw.format   = fmt(C_FUNC, true);
                    rKw.captureGroup = 2; m_rules.append(rKw);
        Rule rWid;  rWid.pattern  = RE(pat); rWid.format  = fmt(C_HEX_WIDTH);
                    rWid.captureGroup = 3; m_rules.append(rWid);
        Rule rEnd;  rEnd.pattern  = RE(pat); rEnd.format  = fmt(C_STORAGE);
                    rEnd.captureGroup = 4; m_rules.append(rEnd);
    }

    // ── 15. xtra_params  ~ param / param2  ───────────────────────────────
    //  Comm-script xtra_params syntax can appear on main-script lines too
    //  (e.g.  KVCAN.CMD > H"AA" | H"BB" ~ 0x125 / 0x44).
    //  Must be last so capture-group rules paint over any numeric colour.
    addXtraParamRules();

    // ── 16. Thread suffix  &  ─────────────────────────────────────────────
    // Matches a standalone ' &' at the very end of the line (after optional
    // whitespace).  The negative lookbehind (?<!&) prevents matching '&&'.
    // Applied last so it paints over any token colour that might land on '&',
    // and the bright-green stands out clearly as a launch modifier.
    addRule(R"( (?<!&)&(?!&)\s*$)", fmt(C_THREAD, true));
}
