#include "ScriptHighlighter.hpp"

// ─── colour palette ───────────────────────────────────────────────────────────
static constexpr auto C_COMMENT   = "#6272a4";   // slate
static constexpr auto C_VARIABLE  = "#8be9fd";   // cyan
static constexpr auto C_KEYWORD   = "#ff79c6";   // pink  / magenta
static constexpr auto C_FUNC      = "#50fa7b";   // green
static constexpr auto C_DEBUG     = "#ff5555";   // red
static constexpr auto C_PLUGIN    = "#50fa7b";   // green (namespace)
static constexpr auto C_COMMAND   = "#ffb86c";   // amber (plugin method)
static constexpr auto C_STRING    = "#f1fa8c";   // yellow
static constexpr auto C_NUMBER    = "#bd93f9";   // purple
static constexpr auto C_FORMAT    = "#ffb86c";   // amber (%N tokens)
static constexpr auto C_LABEL_KW  = "#ff79c6";   // pink  (LABEL keyword)
static constexpr auto C_LABEL_NM  = "#f1fa8c";   // yellow (label name)
static constexpr auto C_STORAGE   = "#8be9fd";   // cyan  (:NUM :STR …)

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

    // ── Single-line rule helper lambdas ───────────────────────────────────
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
        Rule r;
        r.pattern = RE(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:=))");
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
        rPlug.format       = fmt(C_PLUGIN, true);
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
        rName.format       = fmt(C_PLUGIN, true);   // green + bold, same as namespace
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
        add(QString(R"(\b%1\b)").arg(fn), fmt(C_FUNC));

    // ── 8. Debug ──────────────────────────────────────────────────────────
    add(R"(\bBREAKPOINT\b)", fmt(C_DEBUG, true));

    // ── 9. EVAL sub-context tokens ────────────────────────────────────────
    //   :NUM  :STR  :VER  :BOOL  → storage type (cyan)
    add(R"(:(NUM|STR|VER|BOOL)\b)", fmt(C_STORAGE));
    //   Comparison operators
    add(R"(==|!=|>=|<=|>|<)", fmt(C_KEYWORD));
    //   Logical operators
    add(R"(\b(AND|OR|NOT)\b)", fmt(C_KEYWORD));

    // ── 10. Strings:  "..."  (single-line portion) ───────────────────────
    //   QSyntaxHighlighter handles multi-line strings poorly without state;
    //   uscript strings are always single-line so a greedy regex is fine.
    add(R"("(?:[^"\\]|\\.)*")", fmt(C_STRING));

    // ── 11. Format tokens:  %0  %1  %2 … ────────────────────────────────
    add(R"(%\d+)", fmt(C_FORMAT));

    // ── 12. Version literals:  v1.2.3 ────────────────────────────────────
    add(R"(\bv\d+\.\d+(?:\.\d+)*\b)", fmt(C_NUMBER));

    // ── 13. Numeric literals ─────────────────────────────────────────────
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

    // Check if this line opens a block comment (^---)
    auto startMatch = m_blockCommentStart.match(text);
    if (startMatch.hasMatch()) {
        setFormat(0, text.length(), m_blockDelimFmt);
        setCurrentBlockState(IN_BLOCK_CMT);
        // Skip single-line rules — entire line is the delimiter
        return;
    }

    // Normal code line — run all single-line rules
    setCurrentBlockState(NORMAL);

    // ── Line comment early-exit ───────────────────────────────────────────
    // If the line (after optional whitespace) starts with '#', colour the
    // entire line as a comment and return immediately so no subsequent rule
    // can overwrite positions inside the comment.
    {
        static const QRegularExpression lineCommentRe(R"(^\s*#)");
        if (lineCommentRe.match(text).hasMatch()) {
            setFormat(0, text.length(), m_blockCommentFmt);
            return;
        }
    }

    // ── Apply all single-line rules in order ──────────────────────────────
    // Rules that match inside the inline-comment portion of a line (text
    // after a '#' that appears mid-line) must also be suppressed.  We find
    // the position of any mid-line '#' that is NOT inside a string first.
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

    for (const Rule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int start  = (rule.captureGroup == 0)
                               ? m.capturedStart()
                               : m.capturedStart(rule.captureGroup);
            const int length = (rule.captureGroup == 0)
                               ? m.capturedLength()
                               : m.capturedLength(rule.captureGroup);
            if (length <= 0) continue;
            // Skip any match that falls inside the comment portion
            if (commentStart >= 0 && start >= commentStart) continue;
            setFormat(start, length, rule.format);
        }
    }
}
