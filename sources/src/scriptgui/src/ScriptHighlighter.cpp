#include "ScriptHighlighter.hpp"

// ─── colour palette ───────────────────────────────────────────────────────────
static constexpr auto C_COMMENT   = "#6272a4";   // slate
static constexpr auto C_VARIABLE  = "#8be9fd";   // cyan
static constexpr auto C_KEYWORD   = "#ff79c6";   // pink  / magenta
static constexpr auto C_FUNC      = "#3a9fd9";   // blue
static constexpr auto C_DEBUG     = "#ff5555";   // red
static constexpr auto C_PLUGIN    = "#50fa7b";   // green (namespace)
static constexpr auto C_COMMAND   = "#ffb86c";   // amber (plugin method)
static constexpr auto C_STRING    = "#f1fa8c";   // yellow — ALL "..." content (plain + typed-token)
static constexpr auto C_NUMBER    = "#bd93f9";   // purple
static constexpr auto C_FORMAT    = "#ffb86c";   // amber (%N tokens)
static constexpr auto C_LABEL_KW  = "#ff79c6";   // pink  (LABEL keyword)
static constexpr auto C_LABEL_NM  = "#f1fa8c";   // yellow (label name)
static constexpr auto C_STORAGE   = "#8be9fd";   // cyan  (:NUM :STR …)
// ── typed-token decorator prefixes (shared palette with CommScriptHighlighter) ─
static constexpr auto C_HEX_PFX   = "#ff79c6";   // pink   — H / X prefix letter
static constexpr auto C_REGEX_PFX = "#ffb86c";   // amber  — R prefix letter
static constexpr auto C_TOKEN_PFX = "#8be9fd";   // cyan   — T / L prefix letter
static constexpr auto C_SIZE_PFX  = "#8be9fd";   // cyan   — S prefix letter
static constexpr auto C_FILE_PFX  = "#ffb86c";   // amber  — F prefix letter
// (All typed-token quoted content uses C_STRING — yellow, same as plain strings)

// ─────────────────────────────────────────────────────────────────────────────
//  Static helper
// ─────────────────────────────────────────────────────────────────────────────
QTextCharFormat ScriptHighlighter::fmt(const QString &hex, bool bold, bool italic)
{
    QTextCharFormat f;
    f.setForeground(QColor(hex));
    if (bold)   f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — build all rules from the sublime-syntax definition
// ─────────────────────────────────────────────────────────────────────────────
ScriptHighlighter::ScriptHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    using RE = QRegularExpression;

    // ── Block comment delimiters (multi-line, handled in highlightBlock) ──
    m_blockCommentStart = RE("^---");
    m_blockCommentEnd   = RE("^!--");
    m_blockCommentFmt   = fmt(C_COMMENT);
    m_blockDelimFmt     = fmt(C_COMMENT, false, true);   // italic for delimiters

    // ── Single-line rule helper lambda ────────────────────────────────────
    auto add = [&](const QString &pattern, const QTextCharFormat &f, int cap = 0) {
        m_rules.append({ RE(pattern), f, cap });
    };

    // Rules are applied in order — put higher-priority rules first.

    // ── 1. Line comment:  # ... ──────────────────────────────────────────
    add(R"(#.*$)", fmt(C_COMMENT));

    // ── 2. Macro definitions ─────────────────────────────────────────────
    //   NAME :=   → entity.name.constant (NAME) + operator (:=)
    //   NAME ?=   → variable.other       (NAME) + operator (?=)
    //   NAME [=   → entity.name.type     (NAME) + operator ([=)
    // Capture group 1 = name, group 2 = operator
    {
        // Highlight the operator part (group 2) first
        Rule r2;
        r2.pattern      = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))");
        r2.format       = fmt(C_KEYWORD);
        r2.captureGroup = 2;
        m_rules.append(r2);
        // Highlight the name (group 1)
        Rule r1;
        r1.pattern      = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))");
        r1.format       = fmt(C_KEYWORD, true);
        r1.captureGroup = 1;
        m_rules.append(r1);
    }
    {
        Rule r2; r2.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\?=))");
        r2.format = fmt(C_KEYWORD); r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\?=))");
        r1.format = fmt(C_VARIABLE); r1.captureGroup = 1; m_rules.append(r1);
    }
    {
        Rule r2; r2.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[=))");
        r2.format = fmt(C_KEYWORD); r2.captureGroup = 2; m_rules.append(r2);
        Rule r1; r1.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[=))");
        r1.format = fmt(C_VARIABLE); r1.captureGroup = 1; m_rules.append(r1);
    }

    // ── 3. Macro usage ────────────────────────────────────────────────────
    //   $ARRAY.$INDEX  and  $VAR
    add(R"(\$([A-Za-z_][A-Za-z0-9_]*)\.(\$?[A-Za-z_][A-Za-z0-9_]*))",
        fmt(C_VARIABLE));   // whole match cyan (both parts same colour)
    add(R"(\$[A-Za-z_][A-Za-z0-9_]*)", fmt(C_VARIABLE));

    // ── 4. Plugin commands:  PLUGIN.COMMAND ──────────────────────────────
    //   group 1 = plugin namespace (green + bold)
    //   group 2 = command          (amber)
    {
        Rule rCmd;
        rCmd.pattern      = RE(R"(\b([A-Z][A-Z0-9_]*)\.([A-Z][A-Z0-9_]*)\b)");
        rCmd.format       = fmt(C_COMMAND);
        rCmd.captureGroup = 2;
        m_rules.append(rCmd);

        Rule rPlug;
        rPlug.pattern      = RE(R"(\b([A-Z][A-Z0-9_]*)\.([A-Z][A-Z0-9_]*)\b)");
        rPlug.format       = fmt(C_PLUGIN);
        rPlug.captureGroup = 1;
        m_rules.append(rPlug);
    }

    // ── 5. Control keywords ───────────────────────────────────────────────
    const QStringList controlKws = {
        "LOAD_PLUGIN", "IF", "GOTO", "REPEAT", "END_REPEAT", "UNTIL",
        "BREAK", "CONTINUE"
    };
    for (const QString &kw : controlKws)
        add(QString(R"(\b%1\b)").arg(kw), fmt(C_KEYWORD, true));

    // ── LOAD_PLUGIN argument — highlight the plugin name in green ─────────
    //   LOAD_PLUGIN  BUSPIRATE  →  keyword(pink+bold)  pluginName(green+bold)
    //   Group 2 = plugin name, same colour as PLUGIN. namespace
    {
        Rule rName;
        rName.pattern      = QRegularExpression(R"(\bLOAD_PLUGIN\s+([A-Za-z_][A-Za-z0-9_]*))");
        rName.format       = fmt(C_PLUGIN);   // green + bold, same as namespace
        rName.captureGroup = 1;
        m_rules.append(rName);
    }

    // ── 6. LABEL keyword + label name ─────────────────────────────────────
    {
        // Colour the name (group 2) first
        Rule rName;
        rName.pattern      = RE(R"(\b(LABEL)\s+([A-Za-z_][A-Za-z0-9_]*))");
        rName.format       = fmt(C_LABEL_NM);
        rName.captureGroup = 2;
        m_rules.append(rName);
        // Then colour the LABEL keyword (group 1)
        Rule rKw;
        rKw.pattern      = RE(R"(\b(LABEL)\s+([A-Za-z_][A-Za-z0-9_]*))");
        rKw.format       = fmt(C_LABEL_KW, true);
        rKw.captureGroup = 1;
        m_rules.append(rKw);
    }

    // ── 7. Native functions ───────────────────────────────────────────────
    const QStringList funcs = { "PRINT", "DELAY", "FORMAT", "MATH", "EVAL" };
    for (const QString &fn : funcs)
        add(QString(R"(\b%1\b)").arg(fn), fmt(C_FUNC, true));

    // ── 8. Debug ──────────────────────────────────────────────────────────
    add(R"(\bBREAKPOINT\b)", fmt(C_DEBUG, true));

    // ── 9. EVAL sub-context tokens ────────────────────────────────────────
    //   :NUM  :STR  :VER  :BOOL  → storage type (cyan)
    add(R"(:(NUM|STR|VER|BOOL)\b)", fmt(C_STORAGE));
    //   Comparison operators
    add(R"(==|!=|>=|<=|>|<)", fmt(C_KEYWORD));
    //   Logical operators
    add(R"(\b(AND|OR|NOT)\b)", fmt(C_KEYWORD));

    // ── 10. Typed token decorators ────────────────────────────────────────
    //  PREFIX"content"  —  the prefix letter is coloured bold; the quoted
    //  content (including the surrounding quotes) is always yellow (C_STRING),
    //  identical to a plain string.  Two rules per decorator:
    //    captureGroup=1  →  prefix letter only
    //    captureGroup=1  →  the "…" part (including quotes)
    //
    //  Identical patterns and colours as CommScriptHighlighter so both
    //  highlighters render these tokens the same way.

    // H"hex"  and  X"hex"  — hex data / hex token
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([HX])"[^"]*")re");
        rPfx.format = fmt(C_HEX_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[HX]("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // R"pattern"  — regex
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(R)"[^"]*")re");
        rPfx.format = fmt(C_REGEX_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])R("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // T"token"  and  L"line"
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])([TL])"[^"]*")re");
        rPfx.format = fmt(C_TOKEN_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])[TL]("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // S"size"  — byte count
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(S)"[^"]*")re");
        rPfx.format = fmt(C_SIZE_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])S("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // F"filename"  — binary file path
    {
        Rule rPfx; rPfx.pattern = RE(R"re((?<![A-Za-z0-9_])(F)"[^"]*")re");
        rPfx.format = fmt(C_FILE_PFX, true); rPfx.captureGroup = 1; m_rules.append(rPfx);
        Rule rVal; rVal.pattern = RE(R"re((?<![A-Za-z0-9_])F("[^"]*"))re");
        rVal.format = fmt(C_STRING); rVal.captureGroup = 1; m_rules.append(rVal);
    }

    // ── 11. Plain delimited string  "..."  ────────────────────────────────
    //  Must come AFTER typed-token rules so H"/R"/T" etc. are already
    //  coloured.  This rule re-affirms yellow on any remaining bare quoted
    //  string (and also on typed-token content — intentionally idempotent).
    add(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 12. Format tokens:  %0  %1  %2 … ────────────────────────────────
    add(R"(%\d+)", fmt(C_FORMAT));

    // ── 13. Version literals:  v1.2.3 ────────────────────────────────────
    add(R"(\bv\d+\.\d+(?:\.\d+)*\b)", fmt(C_NUMBER));

    // ── 14. Numeric literals ──────────────────────────────────────────────
    add(R"(\b\d+\b)", fmt(C_NUMBER));
}

// ─────────────────────────────────────────────────────────────────────────────
//  highlightBlock — called by Qt for every paragraph that needs recolouring
// ─────────────────────────────────────────────────────────────────────────────
void ScriptHighlighter::highlightBlock(const QString &text)
{
    // ── Multi-line block comment state machine ────────────────────────────
    // State -1 = default (outside block comment)
    // State  1 = inside  block comment
    const int NORMAL       = -1;
    const int IN_BLOCK_CMT =  1;

    int blockState = previousBlockState();

    if (blockState == IN_BLOCK_CMT) {
        // We were inside a block comment at the end of the previous block.
        // Check if this line ends the comment.
        auto endMatch = m_blockCommentEnd.match(text);
        if (endMatch.hasMatch()) {
            // This line is the closing delimiter (!--)
            setFormat(0, text.length(), m_blockDelimFmt);
            setCurrentBlockState(NORMAL);
        } else {
            // Entire line is comment body
            setFormat(0, text.length(), m_blockCommentFmt);
            setCurrentBlockState(IN_BLOCK_CMT);
        }
        // Skip all single-line rules for comment lines
        return;
    }

    if (m_blockCommentStart.match(text).hasMatch()) {
        setFormat(0, text.length(), m_blockDelimFmt);
        setCurrentBlockState(IN_BLOCK_CMT);
        return;
    }

    // Normal code line — run all single-line rules
    setCurrentBlockState(NORMAL);

    // ── Line comment early-exit  # ... ───────────────────────────────────
    {
        static const QRegularExpression lineCommentRe(R"(^\s*#)");
        if (lineCommentRe.match(text).hasMatch()) {
            setFormat(0, text.length(), m_blockCommentFmt);
            return;
        }
    }

    // ── Mid-line comment guard ────────────────────────────────────────────
    // Find the position of any '#' that is NOT inside a quoted string.
    // Matches landing at or after this position are suppressed below.
    int commentStart = -1;
    {
        bool inString = false;
        for (int i = 0; i < text.length(); ++i) {
            if (text[i] == QLatin1Char('"')) { inString = !inString; continue; }
            if (!inString && text[i] == QLatin1Char('#')) { commentStart = i; break; }
        }
    }
    if (commentStart >= 0)
        setFormat(commentStart, text.length() - commentStart, m_blockCommentFmt);

    // ── Build a map of quoted regions ─────────────────────────────────────
    // Rules with captureGroup > 0 intentionally target sub-expressions that
    // may be inside quotes (typed-token content, prefix letters) and are
    // exempted from this check.  Whole-match rules (captureGroup == 0) such
    // as numeric literals and $VAR are prevented from firing inside a string.
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

    // ── Apply all single-line rules in order ──────────────────────────────
    for (const Rule &rule : m_rules) {
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
            // Prevent whole-match rules (numbers, variables, …) from
            // overwriting content inside a quoted string.
            if (!isSubMatch && isInsideQuotes(start)) continue;
            setFormat(start, length, rule.format);
        }
    }
}
