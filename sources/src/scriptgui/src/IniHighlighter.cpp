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
    m_reInterp     = QRegularExpression(R"(\$\{[^}]+\})");
    m_fmtInterp    = fmt("#bd93f9");        // purple  (plain ${VAR})

    // Inside ${section:key} — the section-name part gets amber bold (same as [section])
    // Pattern captures: ${ (section) : (key) }
    m_reInterpXRef  = QRegularExpression(R"(\$\{([^}:]+):([^}]+)\})");
    m_fmtInterpPunct = fmt("#bd93f9");      // purple  — ${ : }
    m_fmtInterpSect  = fmt("#ffb86c", /*bold=*/true);  // amber bold — section name
    m_fmtInterpKey   = fmt("#8be9fd");      // cyan    — key name

    // Standalone section-include:  whole trimmed line is  ${SOMETHING}
    // (no '=' present anywhere on the line)
    m_reInclude  = QRegularExpression(R"(^\s*\$\{[^}]+\}\s*$)");
    m_fmtInclude = fmt("#50fa7b", /*bold=*/true);  // green bold

    // Quoted string value  "…"
    m_reQuoted  = QRegularExpression(R"("(?:[^"\\]|\\.)*")");
    m_fmtQuoted = fmt("#f1fa8c");        // yellow

    // Boolean literals  TRUE / FALSE  (case-insensitive, whole word)
    m_reBool    = QRegularExpression(R"((?<![A-Za-z0-9_])(TRUE|FALSE)(?![A-Za-z0-9_]))",
                                     QRegularExpression::CaseInsensitiveOption);
    m_fmtTrue   = fmt("#50fa7b", /*bold=*/false);   // green   — TRUE
    m_fmtFalse  = fmt("#ff5555", /*bold=*/false);   // red     — FALSE

    // Numeric literals:
    //   hex  0x[0-9A-Fa-f]+   or   0X…
    //   dec  optional sign, digits, optional decimal part
    // Whole-word boundaries prevent matching numbers inside identifiers.
    m_reHexNum  = QRegularExpression(R"((?<![A-Za-z0-9_])(0[xX][0-9A-Fa-f]+)(?![A-Za-z0-9_]))");
    m_reDecNum  = QRegularExpression(R"((?<![A-Za-z0-9_])([+-]?\d+(?:\.\d+)?)(?![A-Za-z0-9_.]))");
    m_fmtHexNum = fmt("#ff79c6");        // pink   — hex numbers
    m_fmtDecNum = fmt("#ff9580");        // peach/orange — decimal numbers

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

            // Boolean literals  TRUE / FALSE  (whole-word, case-insensitive)
            {
                auto it = m_reBool.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto bm = it.next();
                    const bool isTrue = bm.captured(1).compare("true", Qt::CaseInsensitive) == 0;
                    setFormat(bm.capturedStart(1), bm.capturedLength(1),
                              isTrue ? m_fmtTrue : m_fmtFalse);
                }
            }

            // Hex numbers  0x…  (must come before decimal so 0x1F isn't
            // partially matched as the decimal "0")
            {
                auto it = m_reHexNum.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto nm = it.next();
                    setFormat(nm.capturedStart(1), nm.capturedLength(1), m_fmtHexNum);
                }
            }

            // Decimal numbers
            {
                auto it = m_reDecNum.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto nm = it.next();
                    setFormat(nm.capturedStart(1), nm.capturedLength(1), m_fmtDecNum);
                }
            }

            // ${…} interpolation — applied last so it overlays string colour.
            // For cross-section references ${SECTION:KEY} the section name
            // receives amber+bold (same colour as [section] headers) while the
            // key and punctuation stay purple.
            {
                auto it = m_reInterp.globalMatch(text, valueStart);
                while (it.hasNext()) {
                    const auto im = it.next();
                    // Try cross-section match first
                    const auto xm = m_reInterpXRef.match(
                        text, im.capturedStart(),
                        QRegularExpression::NormalMatch,
                        QRegularExpression::AnchorAtOffsetMatchOption);
                    if (xm.hasMatch()) {
                        // ${ — purple
                        setFormat(xm.capturedStart(),    2,                  m_fmtInterpPunct);
                        // SECTION — amber bold
                        setFormat(xm.capturedStart(1),   xm.capturedLength(1), m_fmtInterpSect);
                        // : — purple
                        const int colonPos = xm.capturedStart(2) - 1;
                        setFormat(colonPos, 1,                               m_fmtInterpPunct);
                        // KEY — cyan
                        setFormat(xm.capturedStart(2),   xm.capturedLength(2), m_fmtInterpKey);
                        // } — purple
                        setFormat(xm.capturedEnd() - 1,  1,                  m_fmtInterpPunct);
                    } else {
                        // Plain ${VAR} — all purple
                        setFormat(im.capturedStart(), im.capturedLength(), m_fmtInterp);
                    }
                }
            }
        }
    }
}
