#include "ScriptHighlighterBase.hpp"

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
    // $ARRAY.$INDEX  (both segments cyan)
    addRule(R"(\$([A-Za-z_][A-Za-z0-9_]*)\.(\$?[A-Za-z_][A-Za-z0-9_]*))",
            fmt(C_VAR));
    // $VAR
    addRule(R"(\$[A-Za-z_][A-Za-z0-9_]*)", fmt(C_VAR));
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
