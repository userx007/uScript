#include "ScriptHighlighter.hpp"
#include "uSharedScriptRegex.hpp"

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
//   periwinkle  #a5b4fc  — native functions (PRINT DELAY FORMAT MATH EVAL
//                          BITSTREAM BYTESTREAM)  ·  HEX / REVERSE_BIT /
//                          REVERSE_BYTE post-processor keywords
//   purple      #bd93f9  — label names
//                          (same as base C_DEF_NAME — values share the colour)
//   blue        #89a1ef  — numeric literals (hex/bin/oct/dec, signed) · version literals
//   yellow      #f1fa8c  — "..." string content  (RESERVED — defined in base)
//   teal        #62d6d6  — INCLUDE keyword  (distinct from PLUGIN green and control-flow pink)
//   sky         #87ceeb  — INCLUDE path string  (cooler than yellow, warmer than cyan)
//   slate       #6272a4  — every structural separator (comma, |, ~, /) uses the shared
//                          C_SEPARATOR from ScriptHighlighterBase — see that class doc
//                          (HEX keyword reuses C_FUNC periwinkle; LE/BE reuses C_STORAGE cyan;
//                          width digits get their own purple, same family as the S"n" size prefix)
static constexpr auto C_VAR_NAME     = "#8be9fd"; // cyan       — NAME in NAME ?=
static constexpr auto C_ARR_NAME     = "#ffb86c"; // amber      — NAME in NAME [=
static constexpr auto C_KEYWORD      = "#ff79c6"; // pink       — ?= / [= operators · control-flow
static constexpr auto C_FUNC         = "#a5b4fc"; // periwinkle — PRINT DELAY FORMAT MATH EVAL
                                                  //              BITSTREAM BYTESTREAM
static constexpr auto C_DEBUG        = "#ff5555"; // red        — BREAKPOINT
static constexpr auto C_PLUGIN       = "#20a39e"; // green      — PLUGIN. namespace
static constexpr auto C_COMMAND      = "#ff5555"; // red        — .COMMAND (green↔red complement)
static constexpr auto C_STRING       = "#f1fa8c"; // yellow     — "..." (plain strings)
static constexpr auto C_NUMBER       = "#89a1ef"; // blue       — numeric / version literals
static constexpr auto C_FORMAT       = "#ffb86c"; // amber      — %N format tokens
static constexpr auto C_LABEL_NAME   = "#bd93f9"; // purple     — label name after LABEL keyword
static constexpr auto C_STORAGE      = "#8be9fd"; // cyan       — :NUM :STR :VER :BOOL
static constexpr auto C_THREAD       = "#50fa7b"; // bright-green — & thread suffix ("go" signal)
static constexpr auto C_INCLUDE_KW   = "#62d6d6"; // teal      — INCLUDE keyword
static constexpr auto C_INCLUDE_PATH = "#87ceeb"; // sky-blue  — "path" argument
static constexpr auto C_HEX_WIDTH    = "#bd93f9"; // purple    — HEX width digits (8/16/32/64/128)
                                                  // (HEX keyword itself reuses C_FUNC; the
                                                  // LE/BE endian token reuses C_STORAGE)
static constexpr auto C_MAC_ADDR     = "#87ceeb"; // sky-blue  — MAC addresses (00:11:22:33:44:55)
static constexpr auto C_IP_ADDR      = "#87ceeb"; // sky-blue  — IPv4 addresses (192.168.1.1)
static constexpr auto C_LABEL_REF    = "#bd93f9"; // purple    — label name referenced by GOTO,
                                                  // REPEAT, or END_REPEAT
                                                  // (same colour as C_LABEL_NAME — a label
                                                  //  reference is the same identifier in a
                                                  //  different context, so it must read as
                                                  //  visually "the same thing")
static constexpr auto C_SCRIPT_NAME  = "#87ceeb"; // sky-blue  — comm-script filename argument
                                                  // after PLUGIN.SCRIPT / PLUGIN.COMMAND script
                                                  // (same family as C_INCLUDE_PATH — both name
                                                  //  another file this line will load)

// ─────────────────────────────────────────────────────────────────────────────

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
    //  NOTE: the keyword is hardcoded as "INCLUDE" here (and identically in
    //  ScriptViewer::checkCurrentLineForCommScript()) rather than sourced
    //  from a shared constant, so if it's ever renamed both copies need
    //  updating by hand.
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
        const QString pat = QString(
            "\\b(" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX ")\\.(" SCRIPT_RX_UPPER_IDENT ")\\b");
        Rule rCmd;  rCmd.pattern  = RE(pat); rCmd.format  = fmt(C_COMMAND, true);
                    rCmd.captureGroup  = 2; m_rules.append(rCmd);
        Rule rPlug; rPlug.pattern = RE(pat); rPlug.format = fmt(C_PLUGIN, true);
                    rPlug.captureGroup = 1; m_rules.append(rPlug);
    }

    // ── 4b. Array SIZE accessor  $NAME.SIZE  ──────────────────────────────
    //  $NAME.SIZE reads the element count of a declared array macro (see
    //  m_validateArraySizeUsage() in uScriptValidator.cpp — NAME must name
    //  an ARRAY_MACRO, unlike the generic $ARR.$IDX element lookup handled
    //  by addMacroVariableRule() above). SIZE is a fixed, case-sensitive
    //  reserved word here — not an arbitrary index — so it earns its own
    //  colour instead of the generic $ARR.$IDX cyan wash.
    //
    //  Must come AFTER step 4 (PLUGIN.COMMAND): an upper-case array name
    //  like $BUF.SIZE also matches \bBUF.SIZE\b under the plugin-command
    //  pattern (it has no opinion about the leading '$'), so without this
    //  ordering that rule would repaint BUF/SIZE as PLUGIN/COMMAND colours.
    //  Being last, this rule's capture groups win the last-write-wins race;
    //  the leading '$' and the '.' are left however step 2 or 4 painted them.
    //    group 1 — NAME  → amber, bold  (same colour as the array's own
    //                       "NAME [=" declaration, hinting this must
    //                       resolve to an array macro)
    //    group 2 — SIZE  → purple       (same family as the S"n" byte-count
    //                       prefix and the HEX width digits — all three
    //                       describe a size/count, not a value)
    //  The trailing negative lookahead mirrors the validator's own
    //  sizePattern so "SIZE" glued to more identifier chars (e.g. a macro
    //  literally named SIZEOF) is correctly left unmatched.
    {
        const QString pat = R"(\$([A-Za-z_][A-Za-z0-9_]*)\.(SIZE)(?![A-Za-z0-9_]))";
        Rule rNm; rNm.pattern = RE(pat); rNm.format = fmt(C_ARR_NAME, true);
                  rNm.captureGroup = 1; m_rules.append(rNm);
        Rule rKw; rKw.pattern = RE(pat); rKw.format = fmt(C_HEX_WIDTH);
                  rKw.captureGroup = 2; m_rules.append(rKw);
    }

    // ── 4c. Comm-script filename argument ─────────────────────────────────
    //  Mirrors the two clickable patterns CodeEditor::checkCurrentLineForCommScript()
    //  and MainWindow::autoLoadCommScriptForLine() recognise, so the same
    //  filename that's clickable is also visually called out:
    //    PLUGIN[:N].SCRIPT <file>                — "SCRIPT" uppercase
    //    PLUGIN[:N].COMMAND script <file>         — "script" lowercase
    //  Coloured the same sky-blue as the INCLUDE "path" argument (step 3) —
    //  both name another file this line will load — but unquoted here since
    //  comm-script filenames aren't wrapped in quotes.
    //  Must come after step 4 (PLUGIN.COMMAND) so this capture-group rule's
    //  filename colouring wins last-write-wins over any incidental overlap.
    addRule(QString("\\b" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX "\\.SCRIPT\\s+(\\S+)"),
            fmt(C_SCRIPT_NAME), 1);
    addRule(QString("\\b" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX "\\." SCRIPT_RX_UPPER_IDENT "\\s+script\\s+(\\S+)"),
            fmt(C_SCRIPT_NAME), 1);

    // ── 5. Control keywords ───────────────────────────────────────────────
    //  All control-flow keywords share pink (same as ?= / [= operators).
    for (const QString &kw : {
            "LOAD_PLUGIN", "IF", "GOTO", "REPEAT", "END_REPEAT",
            "UNTIL", "BREAK", "CONTINUE" })
        addRule(QString(R"(\b%1\b)").arg(kw), fmt(C_KEYWORD, true));

    // LOAD_PLUGIN argument — full instance name (UART or UART:1) in green + bold
    //  Uses SCRIPT_RX_PLUGIN_TYPE_NAME (not the generic SCRIPT_RX_IDENT) so a
    //  leading underscore is never highlighted here — LOAD_PLUGIN's plugin-type
    //  name grammar never allows one (see uSharedScriptRegex.hpp).
    {
        Rule r; r.pattern = RE(QString(
            "\\bLOAD_PLUGIN\\s+(" SCRIPT_RX_PLUGIN_TYPE_NAME SCRIPT_RX_INSTANCE_SUFFIX ")"));
                r.format  = fmt(C_PLUGIN, true); r.captureGroup = 1;
        m_rules.append(r);
    }

    // ── 6. LABEL keyword + label name ─────────────────────────────────────
    //  LABEL keyword → pink (same as all other control-flow keywords)
    //  label name    → purple (same family as constant names and numbers)
    {
        const QString pat = QString("\\b(LABEL)\\s+(" SCRIPT_RX_IDENT ")");
        Rule rNm; rNm.pattern = RE(pat); rNm.format = fmt(C_LABEL_NAME);
                  rNm.captureGroup = 2; m_rules.append(rNm);
        Rule rKw; rKw.pattern = RE(pat); rKw.format = fmt(C_KEYWORD, true);
                  rKw.captureGroup = 1; m_rules.append(rKw);
    }

    // ── 6a. Label references — GOTO, REPEAT, END_REPEAT ────────────────────
    //  The keywords themselves are already painted pink by the generic
    //  control-keyword loop in step 5; these rules additionally paint the
    //  label name that follows each of them in the same purple used for the
    //  LABEL declaration above (step 6), so every place a label is used
    //  reads as visually "the same identifier" as the place it's declared —
    //  highlighting the label in its usage context, not only where it's
    //  defined:
    //    GOTO <label>                 — jump target
    //    REPEAT <label> <range...>    — names this loop (so BREAK/CONTINUE/
    //                                   END_REPEAT can target it specifically)
    //    END_REPEAT <label>           — closes the loop of that name
    //  REPEAT's label sits between the keyword and the range-value list
    //  handled by step 6b below, so it's captured here rather than there.
    //  \bREPEAT won't false-match inside END_REPEAT: '_' is a word
    //  character, so there's no \b boundary between the '_' and the 'R'.

    addRule(QString("\\bGOTO\\s+(" SCRIPT_RX_IDENT ")"),       fmt(C_LABEL_REF), 1);
    addRule(QString("\\bREPEAT\\s+(" SCRIPT_RX_IDENT ")"),     fmt(C_LABEL_REF), 1);
    addRule(QString("\\bEND_REPEAT\\s+(" SCRIPT_RX_IDENT ")"), fmt(C_LABEL_REF), 1);

    // ── 6b. REPEAT range values  <begin>, <end>, <step>  ──────────────────
    //  Counted/ranged REPEAT accepts 1-3 comma-separated tokens after the
    //  label (see m_isRepeat() in uScriptSyntax.hpp and m_HandleRepeat() in
    //  uScriptValidator.cpp): a signed decimal/hex/binary/octal integer, a
    //  signed decimal float (with optional exponent), or a "$macro"
    //  reference resolved at runtime. The optional "varname ?=" capture
    //  prefix is allowed; the UNTIL <condition> form is excluded — this
    //  rule only matches the counted/ranged form.
    //
    //  Anchored to the whole line (mirrors m_HandleRepeat's own regex)
    //  because the range list is only ever this statement's trailing
    //  segment; an unanchored token pattern would also fire on unrelated
    //  numbers elsewhere on the line.
    //
    //  Written as three fixed-arity alternatives (1, 2, or 3 tokens) rather
    //  than one {0,2}-repeated group: QRegularExpression only keeps the
    //  *last* iteration of a repeated capture group, which is fine for the
    //  validator's shape-only check but not here, where every token needs
    //  its own colour.
    //
    //  Each token contributes two alternative capture groups (literal vs.
    //  macro) since only the alternative that actually matched is valid —
    //  the other reports capturedLength() <= 0 and highlightBlock() skips
    //  it automatically.
    //    literal token → blue  (C_NUMBER — same colour as any other numeric
    //                    literal; this rule's job is recognising the
    //                    float/exponent shape as a single span, which the
    //                    generic integer-only numeric rule can't do alone)
    //    macro token   → cyan  (C_VAR_NAME — same colour family as any
    //                    other $macro reference)
    //    comma         → slate (C_SEPARATOR — unified structural-separator
    //                    colour, same as | and / everywhere else)
    {
        const QString numTok = QString(SCRIPT_RX_NUMERIC_TOKEN);
        const QString macroTok = QString(SCRIPT_RX_MACRO_REF);
        // One range token → two alternative capture groups (literal | macro).
        const QString tok = QString(R"((%1)|(%2))").arg(numTok, macroTok);
        const QString prefix = QString(SCRIPT_RX_REPEAT_PREFIX);

        struct Arity {
            QString      suffix;
            QVector<int> literalGroups;
            QVector<int> macroGroups;
            QVector<int> commaGroups;
        };
        const QVector<Arity> arities = {
            // <end>
            { QString("(?:%1)$").arg(tok), {1}, {2}, {} },
            // <begin>, <end>
            { QString(R"((?:%1)\s*(,)\s*(?:%1)$)").arg(tok),
              {1, 4}, {2, 5}, {3} },
            // <begin>, <end>, <step>
            { QString(R"((?:%1)\s*(,)\s*(?:%1)\s*(,)\s*(?:%1)$)").arg(tok),
              {1, 4, 7}, {2, 5, 8}, {3, 6} },
        };

        for (const auto &a : arities) {
            const RE re(prefix + a.suffix);
            for (int g : a.literalGroups) {
                Rule r; r.pattern = re; r.format = fmt(C_NUMBER); r.captureGroup = g;
                m_rules.append(r);
            }
            for (int g : a.macroGroups) {
                Rule r; r.pattern = re; r.format = fmt(C_VAR_NAME); r.captureGroup = g;
                m_rules.append(r);
            }
            for (int g : a.commaGroups) {
                Rule r; r.pattern = re; r.format = fmt(C_SEPARATOR); r.captureGroup = g;
                m_rules.append(r);
            }
        }
    }

    // ── 6c. BITSTREAM/BYTESTREAM fields  offset:length:value  ────────────
    //  Matches every "offset:length:value" (BITSTREAM) / "byte_offset:
    //  length:value" (BYTESTREAM) field wherever it occurs on the line —
    //  see parseStreamStatement() in uStreamStatementParser.hpp for the
    //  grammar this mirrors. Unlike REPEAT's range values (6b above),
    //  which are anchored to the whole line because REPEAT's own grammar
    //  interleaves a label between the keyword and the range list, a
    //  BITSTREAM/BYTESTREAM field list is just N copies of the same shape
    //  separated by whitespace — an unanchored pattern matched via
    //  globalMatch() (see ScriptHighlighterBase::highlightBlock) naturally
    //  colours all of them regardless of count, so no REPEAT-style
    //  per-arity duplication is needed here.
    //
    //  Each of offset/length/value is either a literal (blue, C_NUMBER —
    //  same colour as any other numeric literal) or a "$macro"/"$arr.SIZE"
    //  reference (cyan, C_VAR_NAME — same colour as any other $macro) —
    //  same two-alternative-capture-group trick as 6b, since only the
    //  alternative that actually matched has a positive capturedLength().
    //  The two ':' separators get the same slate C_SEPARATOR as every
    //  other structural separator (comma, |, ~, /) elsewhere in this file.
    //
    //  Not restricted to lines starting with BITSTREAM/BYTESTREAM — same
    //  precedent as the MAC/IP-address rules (14c/14d below), which colour
    //  their shape wherever it appears rather than checking context.
    {
        const QString numTok   = QString(SCRIPT_RX_NUMERIC_TOKEN);
        const QString macroTok = QString(SCRIPT_RX_MACRO_REF);
        const QString tok      = QString(R"((%1)|(%2))").arg(numTok, macroTok);
        const RE      re(QString(R"(%1\s*(:)\s*%1\s*(:)\s*%1)").arg(tok));
        // Groups: 1,2 = offset (literal, macro) · 3 = ':' · 4,5 = length
        // (literal, macro) · 6 = ':' · 7,8 = value (literal, macro).
        for (int g : {1, 4, 7}) {
            Rule r; r.pattern = re; r.format = fmt(C_NUMBER); r.captureGroup = g;
            m_rules.append(r);
        }
        for (int g : {2, 5, 8}) {
            Rule r; r.pattern = re; r.format = fmt(C_VAR_NAME); r.captureGroup = g;
            m_rules.append(r);
        }
        for (int g : {3, 6}) {
            Rule r; r.pattern = re; r.format = fmt(C_SEPARATOR); r.captureGroup = g;
            m_rules.append(r);
        }
    }

    // ── 7. Native functions ───────────────────────────────────────────────
    //  Periwinkle — distinct from pink control-flow and green plugin namespace.
    //  BITSTREAM/BYTESTREAM added alongside MATH/FORMAT — same "native
    //  evaluator, no plugin required" family (see uScriptDataTypes.hpp's
    //  StreamStatement doc comment).
    for (const QString &fn : { "PRINT", "DELAY", "FORMAT", "MATH", "EVAL", "BITSTREAM", "BYTESTREAM" })
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
    //  The INCLUDE "path" (step 3) still ends up sky-blue rather than
    //  yellow here: its rules use captureGroup > 0, which is exempt from
    //  the quoted-region guard in highlightBlock(), so they repaint the
    //  path regardless of rule order.
    addRule(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 12. Format tokens  %0 %1 … ───────────────────────────────────────
    addRule(R"(%\d+)", fmt(C_FORMAT));

    // ── 13. Version literals  v1.2.3.4 ────────────────────────────────────
    //  Exactly the LOAD_PLUGIN comparator's version shape — see
    //  SCRIPT_RX_LOAD_PLUGIN_VERSION in uSharedScriptRegex.hpp.
    addRule(QString("\\b" SCRIPT_RX_LOAD_PLUGIN_VERSION "\\b"), fmt(C_NUMBER));

    // ── 14. Numeric literals ──────────────────────────────────────────────
    //  Standalone hex (0x…), binary (0b…), octal (0o…), and decimal
    //  literals — see addNumericLiteralRule() for the exact grammar and the
    //  identifier/':'/'%' exclusions that keep this from bleeding into
    //  UART:1 instance indices, %3 FORMAT tokens, or names like COMMAND9.
    addNumericLiteralRule(fmt(C_NUMBER));

    // ── 14b. MATH | HEX post-processor ────────────────────────────────────
    //  Optional trailing post-processor on a MATH expression:
    //    name ?= MATH expr | HEX[_<width>][_<endian>]
    //  Mirrors the validator's matcher (uScriptValidator.cpp):
    //    \|\s*HEX(?:_(8|16|32|64|128))?(?:_(LE|BE))?\s*$
    //
    //  group 1 — |        structural separator → slate (C_SEPARATOR —
    //                      unified with every other separator: comma, ~, /)
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
        Rule rPipe; rPipe.pattern = RE(pat); rPipe.format = fmt(C_SEPARATOR);
                    rPipe.captureGroup = 1; m_rules.append(rPipe);
        Rule rKw;   rKw.pattern   = RE(pat); rKw.format   = fmt(C_FUNC, true);
                    rKw.captureGroup = 2; m_rules.append(rKw);
        Rule rWid;  rWid.pattern  = RE(pat); rWid.format  = fmt(C_HEX_WIDTH);
                    rWid.captureGroup = 3; m_rules.append(rWid);
        Rule rEnd;  rEnd.pattern  = RE(pat); rEnd.format  = fmt(C_STORAGE);
                    rEnd.captureGroup = 4; m_rules.append(rEnd);
    }

    // ── 14c. MAC Addresses ──────────────────────────────────────────────────
    //  Matches standard 6-octet MAC addresses (e.g., 00:1A:2B:3C:4D:5E).
    //  Sky-blue — same family as C_IP_ADDR/C_INCLUDE_PATH/C_SCRIPT_NAME,
    //  distinct from the plain blue used for generic numeric literals.
    {
        const QString pat = R"(\b([0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5})\b)";
        Rule r; r.pattern = RE(pat); r.format = fmt(C_MAC_ADDR);
                 r.captureGroup = 1; m_rules.append(r);
    }

    // ── 14d. IPv4 Addresses ────────────────────────────────────────────────
    //  Matches dotted-decimal IP addresses (e.g., 192.168.1.1).
    //  Same sky-blue as the MAC-address rule above (C_MAC_ADDR == C_IP_ADDR)
    //  — both are network addresses, distinct from generic blue numbers.
    {
        const QString pat = R"(\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b)";
        Rule r; r.pattern = RE(pat); r.format = fmt(C_IP_ADDR);
                 r.captureGroup = 1; m_rules.append(r);
    }

    // ── 14e. BITSTREAM/BYTESTREAM | REVERSE_BIT|REVERSE_BYTE post-processor ─
    //  Optional trailing modifier on a BITSTREAM/BYTESTREAM statement:
    //    name ?= BITSTREAM  field...  | REVERSE_BIT
    //    name ?= BYTESTREAM field...  | REVERSE_BYTE
    //  Mirrors parseStreamStatement()'s own suffix search
    //  (uStreamStatementParser.hpp) and, structurally, the MATH | HEX
    //  post-processor rule above (14b): both are "| KEYWORD" modifiers
    //  applied to a statement's result.
    //
    //  group 1 — |                → slate    (C_SEPARATOR — same as every
    //                                other structural separator, including
    //                                MATH|HEX's own pipe)
    //  group 2 — REVERSE_BIT/BYTE → periwinkle, bold (C_FUNC — same family
    //                                as HEX: a post-processor applied to
    //                                the statement's result, not a value
    //                                or a control-flow keyword)
    //
    //  Anchored with \s*$ for the same reason as MATH|HEX: only the
    //  trailing modifier, not some unrelated '|' earlier on the line
    //  (there isn't one in valid BITSTREAM/BYTESTREAM syntax, but the
    //  anchor costs nothing and keeps the two rules visually consistent).
    //  Must come after step 6c (field values) so this last-write-wins
    //  capture-group rule doesn't get repainted by anything upstream —
    //  in practice it never overlaps with 6c anyway, since 6c's fields sit
    //  strictly before this trailing "| ..." suffix.
    {
        const QString pat = R"((\|)\s*(REVERSE_BIT|REVERSE_BYTE)\s*$)";
        Rule rPipe; rPipe.pattern = RE(pat); rPipe.format = fmt(C_SEPARATOR);
                    rPipe.captureGroup = 1; m_rules.append(rPipe);
        Rule rKw;   rKw.pattern   = RE(pat); rKw.format   = fmt(C_FUNC, true);
                    rKw.captureGroup = 2; m_rules.append(rKw);
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
