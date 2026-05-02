#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

/**
 * @brief Syntax highlighter for INI configuration files (uIniParserEx format).
 *
 * Handles:
 *   [section]              section header
 *   key = value            key/value pair
 *   ${VAR}                 variable interpolation inside values
 *   ${section:key}         cross-section variable reference
 *   ${SECTION_NAME}        standalone section-include directive
 *   # comment              line comment (hash)
 *   ; comment              line comment (semicolon)
 *
 *  Colour table (Dracula-inspired, consistent with other highlighters):
 *  ──────────────────────────────────────────────────────────────────────
 *  Token                    Hex       Colour      Style
 *  ──────────────────────────────────────────────────────────────────────
 *  [section] brackets       #6272a4   slate
 *  section name             #ffb86c   amber       bold
 *  key name                 #8be9fd   cyan
 *  = separator              #ff79c6   pink
 *  ${…} interpolation       #bd93f9   purple
 *  standalone ${…} include  #50fa7b   green       bold
 *  quoted "value"           #f1fa8c   yellow
 *  # ; comment              #6272a4   slate       italic
 *  ──────────────────────────────────────────────────────────────────────
 */
class IniHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit IniHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    static QTextCharFormat fmt(const QString &hex,
                               bool bold   = false,
                               bool italic = false);

    // Section header:  [name]
    QRegularExpression m_reSection;
    QTextCharFormat    m_fmtBracket;
    QTextCharFormat    m_fmtSectionName;

    // Key = value
    QRegularExpression m_reKey;
    QTextCharFormat    m_fmtKey;
    QTextCharFormat    m_fmtEquals;

    // ${…} interpolation (inside values)
    QRegularExpression m_reInterp;
    QTextCharFormat    m_fmtInterp;        // plain ${VAR} — all purple

    // ${section:key} cross-section reference — sub-span formatting
    QRegularExpression m_reInterpXRef;
    QTextCharFormat    m_fmtInterpPunct;   // ${ : }  — purple
    QTextCharFormat    m_fmtInterpSect;    // section  — amber bold
    QTextCharFormat    m_fmtInterpKey;     // key      — cyan

    // Standalone ${SECTION} include directive (whole trimmed line is ${…})
    QRegularExpression m_reInclude;
    QTextCharFormat    m_fmtInclude;

    // Quoted string values
    QRegularExpression m_reQuoted;
    QTextCharFormat    m_fmtQuoted;

    // Comments  # …  and  ; …
    QRegularExpression m_reComment;
    QTextCharFormat    m_fmtComment;
};
