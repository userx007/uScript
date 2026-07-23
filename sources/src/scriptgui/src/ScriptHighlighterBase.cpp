#include "ScriptHighlighterBase.hpp"
#include "uSharedScriptRegex.hpp"

// ─── shared colour palette ────────────────────────────────────────────────────
// Colours used by rules that live in the base class.
// Derived-class-specific colours are defined in their own .cpp files.
//
// Colour ownership (Dracula-inspired palette):
//   purple  #bd93f9  — constant name (:=) · S prefix · numbers
//   cyan    #8be9fd  — $VAR · T/L prefixes
//   pink    #ff79c6  — := operator · F prefix
//   red     #ff5555  — H/X prefixes
//   amber   #ffb86c  — R prefix
//   yellow  #f1fa8c  — ALL "..." string content  (reserved — never reuse)
//   slate   #6272a4  — comments · block-comment delimiters
static constexpr auto C_COMMENT   = "#6272a4";   // slate  — comments + delimiters
static constexpr auto C_STRING    = "#f1fa8c";   // yellow — ALL "..." content (reserved)
static constexpr auto C_DEF_NAME  = "#bd93f9";   // purple — NAME in  NAME :=
static constexpr auto C_DEF_OP    = "#ff79c6";   // pink   — := operator (same family as ?= and [=)
static constexpr auto C_VAR       = "#8be9fd";   // cyan   — $VAR / $ARR.$IDX
// ── typed-token prefix letters ────────────────────────────────────────────────
static constexpr auto C_HEX_PFX   = "#ff5555";   // red    — H / X  (raw bytes)
static constexpr auto C_REGEX_PFX = "#ffb86c";   // amber  — R  (pattern / regex)
static constexpr auto C_TOKEN_PFX = "#8be9fd";   // cyan   — T / L  (stream tokens)
static constexpr auto C_SIZE_PFX  = "#bd93f9";   // purple — S  (numeric size)
static constexpr auto C_FILE_PFX  = "#ff79c6";   // pink   — F  (file resource)
// ── xtra_params  ~ param / param2  ───────────────────────────────────────────
// ~ and / both use the shared C_SEPARATOR (declared in ScriptHighlighterBase.hpp)
static constexpr auto C_XTRA_PARAM = "#ff79c6";  // pink   — param values (same family as := / F)

// ─────────────────────────────────────────────────────────────────────────────
ScriptHighlighterBase::ScriptHighlighterBase(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    m_blockStart = QRegularExpression("^---");
    m_blockEnd   = QRegularExpression("^!--");
    m_commentFmt = fmt(C_COMMENT);
    m_delimFmt   = fmt(C_COMMENT, false, true);   // italic for delimiters
}

// ─────────────────────────────────────────────────────────────────────────────
QTextCharFormat ScriptHighlighterBase::fmt(const QString &hex, bool bold, bool italic)
{
    QTextCharFormat f;
    f.setForeground(QColor(hex));
    if (bold)   f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addRule(const QString &pattern,
                                    const QTextCharFormat &f, int cap)
{
    m_rules.append({ QRegularExpression(pattern), f, cap });
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addMacroAssignRule()
{
    // NAME :=  — two rules for the same pattern so each capture group gets
    //            its own format without a multi-format rule.
    //   group 1 — constant name  (purple + bold)
    //   group 2 — := operator    (pink — unified with ?= and [= operators)
    const QRegularExpression re(QString("^\\s*(" SCRIPT_RX_IDENT ")\\s*(:=)"));
    Rule rOp;  rOp.pattern  = re;
               rOp.format   = fmt(C_DEF_OP);
               rOp.captureGroup = 2;
    m_rules.append(rOp);
    Rule rNm;  rNm.pattern  = re;
               rNm.format   = fmt(C_DEF_NAME, true);
               rNm.captureGroup = 1;
    m_rules.append(rNm);
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addMacroVariableRule()
{
    // $ARRAY.$INDEX  (both segments cyan)
    addRule(QString("\\$(" SCRIPT_RX_IDENT ")\\.(\\$?" SCRIPT_RX_IDENT ")"),
            fmt(C_VAR));
    // $VAR
    addRule(QString(SCRIPT_RX_MACRO_REF), fmt(C_VAR));
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addTypedTokenDecorators()
{
    using RE = QRegularExpression;

    // Each entry: the letter(s) used as prefix, and the prefix colour.
    // Two rules are generated per entry:
    //   Rule 1 (captureGroup=1) — prefix letter only     (bold + type colour)
    //   Rule 2 (captureGroup=1) — "…" including quotes   (string colour)
    // The lookbehind (?<![A-Za-z0-9_]) prevents matching letters that are
    // part of an identifier (e.g. the 'H' in "MATCH").
    struct Dec { const char *letters; const char *pfxColor; };
    static constexpr Dec decs[] = {
        { "HX", C_HEX_PFX   },    // H"hex"  X"hex"  — raw hex bytes
        { "R",  C_REGEX_PFX  },   // R"pat"          — regex pattern
        { "TL", C_TOKEN_PFX  },   // T"tok"  L"line" — stream tokens
        { "S",  C_SIZE_PFX   },   // S"n"            — byte count
        { "F",  C_FILE_PFX   },   // F"path"         — binary file path
    };

    for (const auto &d : decs) {
        const QString letters = QString::fromLatin1(d.letters);

        Rule rPfx;
        rPfx.pattern = RE(QString(R"re((?<![A-Za-z0-9_])([%1])"[^"]*")re").arg(letters));
        rPfx.format  = fmt(d.pfxColor, /*bold=*/true);
        rPfx.captureGroup = 1;
        m_rules.append(rPfx);

        Rule rVal;
        rVal.pattern = RE(QString(R"re((?<![A-Za-z0-9_])[%1]("[^"]*"))re").arg(letters));
        rVal.format  = fmt(C_STRING);
        rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addXtraParamRules()
{
    // Must be called AFTER any numeric-literal rules: because highlightBlock
    // applies rules in order and last-write-wins, these capture-group rules
    // must run last so they paint param values pink over any earlier numeric
    // colour (purple), regardless of whether the value is hex (0x125) or
    // decimal.

    // ~ separator — unified separator colour (was amber+bold; now matches
    // every other structural separator, per C_SEPARATOR)
    addRule(R"(~)", fmt(C_SEPARATOR));

    // / per-op param separator — unified separator colour (same as |; both
    // are purely structural)
    //   Requires a non-space char on both sides to avoid matching path
    //   separators inside F"..." tokens (those are inside quoted regions
    //   and suppressed by highlightBlock anyway, but the lookaround also
    //   prevents false positives on trailing slashes).
    addRule(R"((?<=\S)\s*(/)\s*(?=\S))", fmt(C_SEPARATOR), /*cap=*/1);

    // param1: first non-space token after ~  — pink
    addRule(R"(~\s*([^\s/|#]+))", fmt(C_XTRA_PARAM), /*cap=*/1);

    // param2: first non-space token after the /  — pink  (only when | present)
    addRule(R"(~[^/\n]*/\s*([^\s|#]+))", fmt(C_XTRA_PARAM), /*cap=*/1);
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addNumericLiteralRule(const QTextCharFormat &format)
{
    // Mirrors numeric::detect_sign_and_base() in uNumeric.hpp: an optional
    // sign, then an explicit 0x/0b/0o base prefix or plain decimal digits.
    // Hex/binary/octal alternatives are tried before decimal in the same
    // pattern, so a prefixed literal is always consumed whole - the decimal
    // alternative never gets a chance to split it at an internal boundary.
    //
    // Leading (?<![A-Za-z0-9_:%]) blocks the match when glued to an
    // identifier char (PLUGIN123), a plugin instance colon (UART:1), or a
    // FORMAT token percent (%3) - all of those already own their digits via
    // a dedicated rule elsewhere. It also means a '+'/'-' is only pulled in
    // as part of the literal when it isn't itself glued to a preceding
    // value, so "5-3" highlights "5" and "3" separately (subtraction) while
    // " -42" highlights the whole signed literal.
    //
    // Trailing (?![A-Za-z0-9_]) blocks the match when glued to more
    // identifier chars on the right (COMMAND9), so digits inside a name are
    // left untouched entirely rather than partially highlighted.
    //
    // This is a whole-match (captureGroup=0) rule, so highlightBlock()'s
    // isInsideQuotes() guard already keeps it out of "...", H"...", S"...",
    // F"...", X"..." content, and any other quoted region.
    addRule(R"((?<![A-Za-z0-9_:%])[+-]?(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|0[oO][0-7]+|\d+)(?![A-Za-z0-9_]))",
            format);
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::highlightBlock(const QString &text)
{
    const int NORMAL       = -1;
    const int IN_BLOCK_CMT =  1;

    // ── Block comment state machine ───────────────────────────────────────
    if (previousBlockState() == IN_BLOCK_CMT) {
        if (m_blockEnd.match(text).hasMatch()) {
            setFormat(0, text.length(), m_delimFmt);
            setCurrentBlockState(NORMAL);
        } else {
            setFormat(0, text.length(), m_commentFmt);
            setCurrentBlockState(IN_BLOCK_CMT);
        }
        return;
    }
    if (m_blockStart.match(text).hasMatch()) {
        setFormat(0, text.length(), m_delimFmt);
        setCurrentBlockState(IN_BLOCK_CMT);
        return;
    }
    setCurrentBlockState(NORMAL);

    // ── Line comment early-exit  # ... ───────────────────────────────────
    {
        static const QRegularExpression lineCommentRe(R"(^\s*#)");
        if (lineCommentRe.match(text).hasMatch()) {
            setFormat(0, text.length(), m_commentFmt);
            return;
        }
    }

    // ── Mid-line comment guard ────────────────────────────────────────────
    // Locate the first '#' outside a quoted string.
    // Matches at or after this position are suppressed in the rule loop.
    int commentStart = -1;
    {
        bool inStr = false;
        for (int i = 0; i < text.length(); ++i) {
            if (text[i] == QLatin1Char('"')) { inStr = !inStr; continue; }
            if (!inStr && text[i] == QLatin1Char('#')) { commentStart = i; break; }
        }
    }
    if (commentStart >= 0)
        setFormat(commentStart, text.length() - commentStart, m_commentFmt);

    // ── Build quoted-region map ───────────────────────────────────────────
    // Whole-match rules (captureGroup == 0) are skipped when their match
    // falls inside a "..." region, preventing them from overwriting string
    // content.  Sub-match rules (captureGroup > 0) are exempt: they
    // intentionally target prefix letters and token content near quotes.
    // Stored as a sorted list of open-positions so we can use binary search
    // (O(log n)) rather than a linear scan in isInsideQuotes.
    QVector<QPair<int,int>> quotedRegions;
    {
        bool inQ = false; int openPos = -1;
        for (int i = 0; i < text.length(); ++i) {
            if (text[i] == QLatin1Char('"')) {
                if (!inQ) { inQ = true;  openPos = i; }
                else      { inQ = false; quotedRegions.append({openPos, i}); }
            }
        }
    }
    // Binary search: find the last region whose open-pos is <= pos,
    // then check whether pos is strictly inside it.
    auto isInsideQuotes = [&](int pos) -> bool {
        // Lower-bound on the first element of each pair (open position)
        int lo = 0, hi = quotedRegions.size();
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (quotedRegions[mid].first < pos) lo = mid + 1;
            else                                hi = mid;
        }
        // lo now points to the first region with open >= pos.
        // The region that might contain pos is the one just before lo.
        if (lo == 0) return false;
        const auto &r = quotedRegions[lo - 1];
        return pos > r.first && pos < r.second;
    };

    // ── Apply rules ───────────────────────────────────────────────────────
    for (const Rule &rule : m_rules) {
        const bool isSubMatch = (rule.captureGroup > 0);
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const int start  = isSubMatch ? m.capturedStart(rule.captureGroup)
                                          : m.capturedStart();
            const int length = isSubMatch ? m.capturedLength(rule.captureGroup)
                                          : m.capturedLength();
            if (length <= 0) continue;
            if (commentStart >= 0 && start >= commentStart) continue;
            if (!isSubMatch && isInsideQuotes(start)) continue;
            setFormat(start, length, rule.format);
        }
    }
}
