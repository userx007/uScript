#include "IniHighlighter.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Helper
// ─────────────────────────────────────────────────────────────────────────────

QTextCharFormat IniHighlighter::fmt(const QString &hex, bool bold, bool italic)
{
    QTextCharFormat f;
    f.setForeground(QColor(hex));
    if (bold)   f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

IniHighlighter::IniHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // [section header]
    // Group 1 = opening '[',  group 2 = name,  group 3 = closing ']'
    m_reSection       = QRegularExpression(R"(^\s*(\[)([^\]]+)(\]))");
    m_fmtBracket      = fmt("#6272a4");                 // slate
    m_fmtSectionName  = fmt("#ffb86c", /*bold=*/true);  // amber bold

    // key = value  (only matches when there is an '=' on the line,
    //               and there is no '[' before it — so section headers
    //               are not accidentally caught)
    // Group 1 = key name,  group 2 = '='
    m_reKey    = QRegularExpression(R"(^\s*([A-Za-z_][\w.\-]*)\s*(=))");
    m_fmtKey   = fmt("#8be9fd");         // cyan
    m_fmtEquals = fmt("#ff79c6");        // pink

    // ${…} variable interpolation / cross-section reference
    m_reInterp  = QRegularExpression(R"(\$\{[^}]+\})");
    m_fmtInterp = fmt("#bd93f9");        // purple

    // Standalone section-include:  whole trimmed line is  ${SOMETHING}
    // (no '=' present anywhere on the line)
    m_reInclude  = QRegularExpression(R"(^\s*\$\{[^}]+\}\s*$)");
    m_fmtInclude = fmt("#50fa7b", /*bold=*/true);  // green bold

    // Quoted string value  "…"
    m_reQuoted  = QRegularExpression(R"("(?:[^"\\]|\\.)*")");
    m_fmtQuoted = fmt("#f1fa8c");        // yellow

    // Line comments:  # … or ; …
    m_reComment  = QRegularExpression(R"(^\s*[#;].*)");
    m_fmtComment = fmt("#6272a4", /*bold=*/false, /*italic=*/true);  // slate italic
}

// ─────────────────────────────────────────────────────────────────────────────
//  highlightBlock
// ─────────────────────────────────────────────────────────────────────────────

void IniHighlighter::highlightBlock(const QString &text)
{
    // ── 1. Comment — if the (trimmed) line starts with # or ; colour it all ─
    {
        const auto m = m_reComment.match(text);
        if (m.hasMatch()) {
            setFormat(m.capturedStart(), m.capturedLength(), m_fmtComment);
            return;   // nothing else on a comment line
        }
    }

    // ── 2. Section header  [name] ────────────────────────────────────────────
    {
        const auto m = m_reSection.match(text);
        if (m.hasMatch()) {
            setFormat(m.capturedStart(1), m.capturedLength(1), m_fmtBracket);     // [
            setFormat(m.capturedStart(2), m.capturedLength(2), m_fmtSectionName); // name
            setFormat(m.capturedStart(3), m.capturedLength(3), m_fmtBracket);     // ]
            return;   // nothing else on a section-header line
        }
    }

    // ── 3. Standalone section-include  ${SECTION} ────────────────────────────
    //    Must be checked before the interpolation rule so the whole token
    //    gets the stronger green-bold format.
    if (!text.contains('=')) {
        const auto m = m_reInclude.match(text);
        if (m.hasMatch()) {
            setFormat(m.capturedStart(), m.capturedLength(), m_fmtInclude);
            return;
        }
    }

    // ── 4. Key = value line ───────────────────────────────────────────────────
    {
        const auto m = m_reKey.match(text);
        if (m.hasMatch()) {
            setFormat(m.capturedStart(1), m.capturedLength(1), m_fmtKey);    // key
            setFormat(m.capturedStart(2), m.capturedLength(2), m_fmtEquals); // =

            // Apply value-side rules from the character after '=' onwards
            const int valueStart = m.capturedEnd(2);

            // Quoted strings
            {
                auto it = m_reQuoted.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto qm = it.next();
                    setFormat(qm.capturedStart(), qm.capturedLength(), m_fmtQuoted);
                }
            }

            // ${…} interpolation — applied last so it overlays string colour
            // (a reference inside quotes is more useful highlighted as purple)
            {
                auto it = m_reInterp.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto im = it.next();
                    setFormat(im.capturedStart(), im.capturedLength(), m_fmtInterp);
                }
            }
        }
    }
}
