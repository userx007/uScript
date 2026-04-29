#include "ScriptHighlighterBase.hpp"

// ─── shared colour palette ────────────────────────────────────────────────────
// Colours used by rules that live in the base class.
// Derived-class-specific colours are defined in their own .cpp files.
static constexpr auto C_COMMENT   = "#6272a4";   // slate  — comments + delimiters
static constexpr auto C_STRING    = "#f1fa8c";   // yellow — ALL "..." content
static constexpr auto C_DEF_NAME  = "#ff79c6";   // pink   — NAME in  NAME :=
static constexpr auto C_DEF_OP    = "#ff79c6";   // pink   — := operator
static constexpr auto C_VAR       = "#8be9fd";   // cyan   — $VAR
// ── typed-token prefix letters ────────────────────────────────────────────────
static constexpr auto C_HEX_PFX   = "#ff79c6";   // pink   — H / X
static constexpr auto C_REGEX_PFX = "#ffb86c";   // amber  — R
static constexpr auto C_TOKEN_PFX = "#8be9fd";   // cyan   — T / L
static constexpr auto C_SIZE_PFX  = "#8be9fd";   // cyan   — S
static constexpr auto C_FILE_PFX  = "#ffb86c";   // amber  — F

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
    const QString pat = R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))";
    Rule rOp;  rOp.pattern  = QRegularExpression(pat);
               rOp.format   = fmt(C_DEF_OP);
               rOp.captureGroup = 2;
    m_rules.append(rOp);
    Rule rNm;  rNm.pattern  = QRegularExpression(pat);
               rNm.format   = fmt(C_DEF_NAME, true);
               rNm.captureGroup = 1;
    m_rules.append(rNm);
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addMacroVariableRule()
{
    // $ARRAY.$INDEX  (both segments same cyan colour)
    addRule(R"(\$([A-Za-z_][A-Za-z0-9_]*)\.(\$?[A-Za-z_][A-Za-z0-9_]*))",
            fmt(C_VAR));
    // $VAR
    addRule(R"(\$[A-Za-z_][A-Za-z0-9_]*)", fmt(C_VAR));
}

// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighterBase::addTypedTokenDecorators()
{
    using RE = QRegularExpression;

    // Each block: two rules per decorator letter (or letter class).
    //   Rule 1 (rPfx) — captureGroup=1 → prefix letter only     (bold colour)
    //   Rule 2 (rVal) — captureGroup=1 → "…" including quotes   (yellow)
    //
    // The lookbehind (?<![A-Za-z0-9_]) prevents matching identifier suffixes
    // (e.g. the 'H' in "MATCH") as a typed-token prefix.

    // H"hex"  X"hex"  — hex data / hex token
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([HX])"[^"]*")re");
        rPfx.format = fmt(C_HEX_PFX, true); rPfx.captureGroup = 1;
        m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[HX]("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
    // R"pattern"  — regex
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(R)"[^"]*")re");
        rPfx.format = fmt(C_REGEX_PFX, true); rPfx.captureGroup = 1;
        m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])R("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
    // T"token"  L"line"
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([TL])"[^"]*")re");
        rPfx.format = fmt(C_TOKEN_PFX, true); rPfx.captureGroup = 1;
        m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[TL]("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
    // S"size"  — byte count
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(S)"[^"]*")re");
        rPfx.format = fmt(C_SIZE_PFX, true); rPfx.captureGroup = 1;
        m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])S("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
    // F"filename"  — binary file path
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(F)"[^"]*")re");
        rPfx.format = fmt(C_FILE_PFX, true); rPfx.captureGroup = 1;
        m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])F("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1;
        m_rules.append(rVal);
    }
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
    auto isInsideQuotes = [&](int pos) {
        for (const auto &r : quotedRegions)
            if (pos > r.first && pos < r.second) return true;
        return false;
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
