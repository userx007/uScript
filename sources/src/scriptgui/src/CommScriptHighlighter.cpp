#include "CommScriptHighlighter.hpp"

// ─── colour palette ───────────────────────────────────────────────────────────
static constexpr auto C_COMMENT    = "#6272a4";   // slate  — comments + block delimiters
static constexpr auto C_SEND       = "#ff5555";   // red    — > direction
static constexpr auto C_RECV       = "#50fa7b";   // green  — < direction
static constexpr auto C_DELAY_PFX  = "#ffb86c";   // amber  — ! prefix
static constexpr auto C_DELAY_NUM  = "#bd93f9";   // purple — delay number
static constexpr auto C_DELAY_UNIT = "#8be9fd";   // cyan   — sec / ms / us
static constexpr auto C_PIPE       = "#6272a4";   // slate  — | separator
static constexpr auto C_HEX_PFX    = "#ff79c6";   // pink   — H / X prefix letter
static constexpr auto C_HEX_VAL    = "#f1fa8c";   // yellow — hex content
static constexpr auto C_REGEX_PFX  = "#ffb86c";   // amber  — R prefix letter
static constexpr auto C_REGEX_VAL  = "#f1fa8c";   // yellow — regex pattern
static constexpr auto C_TOKEN_PFX  = "#8be9fd";   // cyan   — T / L prefix letter
static constexpr auto C_TOKEN_VAL  = "#f1fa8c";   // yellow — token / line content
static constexpr auto C_SIZE_PFX   = "#8be9fd";   // cyan   — S prefix
static constexpr auto C_SIZE_VAL   = "#f1fa8c";   // yellow — size number
static constexpr auto C_FILE_PFX   = "#ffb86c";   // amber  — F prefix
static constexpr auto C_FILE_VAL   = "#f1fa8c";   // yellow — filename
static constexpr auto C_STRING     = "#f1fa8c";   // yellow — "plain quoted string"
static constexpr auto C_VAR        = "#8be9fd";   // cyan   — $VAR
static constexpr auto C_DEF_NAME   = "#ff79c6";   // pink   — macro name in NAME :=
static constexpr auto C_DEF_OP     = "#ff79c6";   // pink   — := operator

// ─────────────────────────────────────────────────────────────────────────────
QTextCharFormat CommScriptHighlighter::fmt(const QString &hex, bool bold, bool italic)
{
    QTextCharFormat f;
    f.setForeground(QColor(hex));
    if (bold)   f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
CommScriptHighlighter::CommScriptHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    using RE = QRegularExpression;

    m_blockStart  = RE("^---");
    m_blockEnd    = RE("^!--");
    m_commentFmt  = fmt(C_COMMENT);
    m_delimFmt    = fmt(C_COMMENT, false, true);

    auto add = [&](const QString &pat, const QTextCharFormat &f, int cap = 0) {
        m_rules.append({ RE(pat), f, cap });
    };

    // ── 1. Line comment  # ... ────────────────────────────────────────────
    // (handled in highlightBlock early-exit, not as a rule — same as ScriptHighlighter)

    // ── 2. Macro definition  NAME := value ───────────────────────────────
    //  group 1 = name (pink bold), group 2 = := (pink)
    {
        Rule r2; r2.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))");
        r2.format = fmt(C_DEF_OP); r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))");
        r1.format = fmt(C_DEF_NAME, true); r1.captureGroup = 1; m_rules.append(r1);
    }

    // ── 3. Macro variable  $NAME  or  $ARR.$IDX ──────────────────────────
    add(R"(\$[A-Za-z_][A-Za-z0-9_]*(?:\.\$?[A-Za-z_][A-Za-z0-9_]*)?)",
        fmt(C_VAR));

    // ── 4. Pipe separator  | ─────────────────────────────────────────────
    //  Only colour a bare | that is not inside quotes (approximate — QSyntaxHighlighter
    //  processes plain text; we skip quoted regions via the direction-line guard below)
    add(R"(\|)", fmt(C_PIPE));

    // ── 5. Direction prefix  >  <  ! (first non-space char of a command line) ──
    //  These three rules match a single character at the start of the line
    //  (after optional whitespace).  Block comment delimiters (--- / !--)
    //  are handled in highlightBlock before these rules run.
    add(R"(^\s*(>))", fmt(C_SEND, true), 1);
    add(R"(^\s*(<))", fmt(C_RECV, true), 1);
    // ! as delay prefix only — not !-- (block comment end handled before rules)
    add(R"(^\s*(!)(?!--))", fmt(C_DELAY_PFX, true), 1);

    // ── 6. Delay value and unit ───────────────────────────────────────────
    //  !  <number>  <unit>
    //  Recognised units: sec, ms, us  (from TIME_SECONDS / TIME_MILISECONDS / TIME_MICROSECONDS)
    add(R"(^\s*!\s*(\d+))", fmt(C_DELAY_NUM), 1);
    add(R"(\b(sec|ms|us)\b)", fmt(C_DELAY_UNIT));

    // ── 7. Typed token decorators ─────────────────────────────────────────
    //  Each decorator: PREFIX " content "
    //  We colour the prefix letter separately from the quoted content so the
    //  user can instantly recognise the token type.

    // H"hex"  and  X"hex"  — hex data / hex token
    //  group 1 = H or X prefix,  group 2 = quoted content (including quotes)
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([HX])"[^"]*")re");
        rPfx.format = fmt(C_HEX_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[HX]("[^"]*"))re");
        rVal.format = fmt(C_HEX_VAL); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // R"pattern"  — regex
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(R)"[^"]*")re");
        rPfx.format = fmt(C_REGEX_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])R("[^"]*"))re");
        rVal.format = fmt(C_REGEX_VAL); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // T"token"  and  L"line"  — token / line
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([TL])"[^"]*")re");
        rPfx.format = fmt(C_TOKEN_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[TL]("[^"]*"))re");
        rVal.format = fmt(C_TOKEN_VAL); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // S"size"  — byte count
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(S)"[^"]*")re");
        rPfx.format = fmt(C_SIZE_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])S("[^"]*"))re");
        rVal.format = fmt(C_SIZE_VAL); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // F"filename"  — binary file path
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(F)"[^"]*")re");
        rPfx.format = fmt(C_FILE_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])F("[^"]*"))re");
        rVal.format = fmt(C_FILE_VAL); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // ── 8. Plain delimited string  "..."  ────────────────────────────────
    //  Must come AFTER the typed-token rules so H"/R"/T" etc. are already
    //  coloured and this rule only catches bare quoted strings.
    add(R"re("(?:[^"\]|\.)*")re", fmt(C_STRING));

    // ── 9. Numeric literals  (standalone numbers in delay or size context) ─
    add(R"(\b\d+\b)", fmt(C_DELAY_NUM));
}

// ─────────────────────────────────────────────────────────────────────────────
void CommScriptHighlighter::highlightBlock(const QString &text)
{
    const int NORMAL       = -1;
    const int IN_BLOCK_CMT =  1;

    int blockState = previousBlockState();

    // ── Block comment continuation ────────────────────────────────────────
    if (blockState == IN_BLOCK_CMT) {
        auto endMatch = m_blockEnd.match(text);
        if (endMatch.hasMatch()) {
            setFormat(0, text.length(), m_delimFmt);
            setCurrentBlockState(NORMAL);
        } else {
            setFormat(0, text.length(), m_commentFmt);
            setCurrentBlockState(IN_BLOCK_CMT);
        }
        return;
    }

    // ── Block comment open  --- ───────────────────────────────────────────
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
    int commentStart = -1;
    {
        bool inString = false;
        for (int i = 0; i < text.length(); ++i) {
            if (text[i] == QLatin1Char('"')) { inString = !inString; continue; }
            if (!inString && text[i] == QLatin1Char('#')) { commentStart = i; break; }
        }
    }
    if (commentStart >= 0)
        setFormat(commentStart, text.length() - commentStart, m_commentFmt);

    // ── Build a map of quoted regions so rules can skip inside them ─────
    // Each entry is [open_quote_pos, close_quote_pos].
    // Typed-token rules (H/R/T/L/S/F/X + captureGroup=1) are exempted from
    // this check — they intentionally target the content inside quotes.
    QVector<QPair<int,int>> quotedRegions;
    {
        bool inQ = false;
        int  openPos = -1;
        for (int i = 0; i < text.length(); ++i) {
            if (text[i] == QLatin1Char('"')) {
                if (!inQ) { inQ = true;  openPos = i; }
                else      { inQ = false; quotedRegions.append({openPos, i}); }
            }
        }
    }

    auto isInsideQuotes = [&](int pos) -> bool {
        for (const auto &r : quotedRegions)
            if (pos > r.first && pos < r.second) return true;
        return false;
    };

    // ── Apply all single-line rules ───────────────────────────────────────
    for (const Rule &rule : m_rules) {
        // Rules with captureGroup > 0 target a sub-match (e.g. the content
        // inside a typed token) — they are intentionally inside quotes.
        const bool isSubMatch = (rule.captureGroup > 0);

        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int start  = isSubMatch
                               ? m.capturedStart(rule.captureGroup)
                               : m.capturedStart();
            const int length = isSubMatch
                               ? m.capturedLength(rule.captureGroup)
                               : m.capturedLength();
            if (length <= 0) continue;
            if (commentStart >= 0 && start >= commentStart) continue;
            // Skip whole-match rules that land inside a quoted region
            // (prevents numeric/variable rules from overwriting token content)
            if (!isSubMatch && isInsideQuotes(start)) continue;
            setFormat(start, length, rule.format);
        }
    }
}

