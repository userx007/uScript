#ifndef U_STREAM_STATEMENT_PARSER_HPP
#define U_STREAM_STATEMENT_PARSER_HPP

#include "uScriptDataTypes.hpp"

#include <string>
#include <vector>
#include <cctype>

// ---------------------------------------------------------------------------
// parseStreamStatement — structural parser for one BITSTREAM/BYTESTREAM line.
//
//   name ?= BITSTREAM  offset:length:value [offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
//   name ?= BYTESTREAM byte_offset:length:value [byte_offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
//
// Shared by ScriptValidator::m_HandleBitstreamStmt()/m_HandleBytestreamStmt()
// (compiled .script files) and ScriptInterpreter::executeCmd() (interactive
// shell lines) so the two entry points can never drift apart.
//
// This is a STRUCTURAL parser only — it splits the line into a destination
// name and a list of StreamField{offset,length,value} templates, and checks
// the "offset:length:value" *shape* of each field. It deliberately does NOT:
//   - resolve $macros (constant macros are already substituted by the caller
//     before this runs, same as every other statement type; variable macros
//     are resolved later, at execution time, exactly like MathStatement's
//     expression template)
//   - parse offset/length/value as numbers, or check numeric constraints
//     (fits in `length` bits, no field overlap, REVERSE_BIT vs REVERSE_BYTE
//     exclusivity is already enforced by strKeyword's caller-visible shape
//     — see below)
// Both are deferred to execution time (ScriptInterpreter's BITSTREAM/
// BYTESTREAM execution), because offset/length/value may themselves be
// variable ("?=") macros not yet known when a line is merely being parsed.
//
// @param strKeyword  "BITSTREAM" or "BYTESTREAM" — selects which keyword
//                     must appear on the RHS; strKeyword itself is also
//                     copied nowhere — the caller already knows which one it
//                     asked for and sets StreamStatement::bByteMode itself.
// @param strLine      the full raw statement text, e.g.
//                      "cfg ?= BITSTREAM 64:1:1 34:4:7 | REVERSE_BIT"
//                     Already macro-expanded for CONSTANT ("name := value")
//                     macros by the caller, exactly as every other statement
//                     type expects (see ScriptValidator::m_validateScriptStatements
//                     / ScriptInterpreter::executeCmd).
// @param out          receives strName and vFields (eReverse and bByteMode
//                     are NOT touched here — bByteMode is the caller's own
//                     knowledge of which keyword it asked for; eReverse is
//                     parsed here and written into out.eReverse).
// @param strError     receives a human-readable reason on failure.
// @return false on any structural problem (missing '?=', wrong/missing
//         keyword, no fields, a field that isn't exactly "X:Y:Z", or both
//         REVERSE_BIT and REVERSE_BYTE / an unrecognised "| ..." suffix).
// ---------------------------------------------------------------------------
inline bool parseStreamStatement(const std::string& strKeyword,
                                  const std::string& strLine,
                                  StreamStatement&    out,
                                  std::string&        strError) noexcept
{
    auto trim = [](std::string s) -> std::string {
        const size_t fs = s.find_first_not_of(" \t");
        const size_t fe = s.find_last_not_of(" \t");
        return (fs == std::string::npos) ? std::string() : s.substr(fs, fe - fs + 1);
    };

    // ── 1. Split at first '?=' ──────────────────────────────────────────
    static const std::string kAssign = "?=";
    const auto assignPos = strLine.find(kAssign);
    if (assignPos == std::string::npos) {
        strError = strKeyword + ": missing '?='";
        return false;
    }

    out.strName = trim(strLine.substr(0, assignPos));
    if (out.strName.empty()) {
        strError = strKeyword + ": missing destination macro name";
        return false;
    }

    // ── 2. Strip the keyword from the RHS ───────────────────────────────
    std::string strRhs = trim(strLine.substr(assignPos + kAssign.size()));

    if (strRhs.size() < strKeyword.size() ||
        strRhs.compare(0, strKeyword.size(), strKeyword) != 0 ||
        // Reject e.g. "BITSTREAMFOO" being mistaken for the "BITSTREAM" keyword.
        (strRhs.size() > strKeyword.size() &&
         !std::isspace(static_cast<unsigned char>(strRhs[strKeyword.size()])))) {
        strError = strKeyword + ": missing " + strKeyword + " keyword in RHS";
        return false;
    }
    strRhs = trim(strRhs.substr(strKeyword.size()));

    if (strRhs.empty()) {
        strError = strKeyword + ": no offset:length:value fields given";
        return false;
    }

    // ── 3. Split off an optional trailing "| REVERSE_BIT" / "| REVERSE_BYTE" ──
    // No field (offset/length/value, whether literal or $macro) can contain
    // '|', so the LAST '|' in the line unambiguously marks this suffix, if
    // one is present at all.
    out.eReverse = StreamReverseMode::NONE;
    const auto pipePos = strRhs.rfind('|');
    if (pipePos != std::string::npos) {
        const std::string strSuffix = trim(strRhs.substr(pipePos + 1));
        if (strSuffix == "REVERSE_BIT") {
            out.eReverse = StreamReverseMode::REVERSE_BIT;
        } else if (strSuffix == "REVERSE_BYTE") {
            out.eReverse = StreamReverseMode::REVERSE_BYTE;
        } else {
            strError = strKeyword + ": unrecognised '| " + strSuffix +
                        "' — expected REVERSE_BIT or REVERSE_BYTE";
            return false;
        }
        strRhs = trim(strRhs.substr(0, pipePos));
        if (strRhs.empty()) {
            strError = strKeyword + ": no offset:length:value fields given before '|'";
            return false;
        }
    }

    // ── 4. Split the remainder on whitespace into "offset:length:value" fields ──
    out.vFields.clear();
    {
        std::string::size_type pos = 0;
        while (pos < strRhs.size()) {
            const auto spacePos = strRhs.find_first_of(" \t", pos);
            const std::string strField = (spacePos == std::string::npos)
                                              ? strRhs.substr(pos)
                                              : strRhs.substr(pos, spacePos - pos);
            pos = (spacePos == std::string::npos) ? strRhs.size() : strRhs.find_first_not_of(" \t", spacePos);

            if (strField.empty()) {
                continue;
            }

            const auto c1 = strField.find(':');
            if (c1 == std::string::npos) {
                strError = strKeyword + ": field [" + strField + "] is not offset:length:value";
                return false;
            }
            const auto c2 = strField.find(':', c1 + 1);
            if (c2 == std::string::npos) {
                strError = strKeyword + ": field [" + strField + "] is not offset:length:value";
                return false;
            }
            if (strField.find(':', c2 + 1) != std::string::npos) {
                strError = strKeyword + ": field [" + strField + "] has more than 3 ':'-separated parts";
                return false;
            }

            StreamField sField;
            sField.strOffsetTpl = strField.substr(0, c1);
            sField.strLengthTpl = strField.substr(c1 + 1, c2 - c1 - 1);
            sField.strValueTpl  = strField.substr(c2 + 1);

            if (sField.strOffsetTpl.empty() || sField.strLengthTpl.empty() || sField.strValueTpl.empty()) {
                strError = strKeyword + ": field [" + strField + "] has an empty offset/length/value part";
                return false;
            }

            out.vFields.push_back(std::move(sField));
        }
    }

    if (out.vFields.empty()) {
        strError = strKeyword + ": no offset:length:value fields given";
        return false;
    }

    return true;

} // parseStreamStatement()

#endif // U_STREAM_STATEMENT_PARSER_HPP
