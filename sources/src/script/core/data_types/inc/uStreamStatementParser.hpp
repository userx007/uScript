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

// ---------------------------------------------------------------------------
// parseStreamValStatement — structural parser for one BITSTREAMVAL/
// BYTESTREAMVAL line.
//
//   name ?= <hex_source> | BITSTREAMVAL  <bit_offset>:<value_size>
//   name ?= <hex_source> | BYTESTREAMVAL <byte_offset>:<bit_offset>:<value_size>
//
// Shared by ScriptValidator::m_HandleBitstreamValStmt()/
// m_HandleBytestreamValStmt() (compiled .script files) and
// ScriptInterpreter::executeCmd() (interactive shell lines), same reason as
// parseStreamStatement() above: the two entry points can never drift apart.
//
// This is a STRUCTURAL parser only, with the same division of labour as
// parseStreamStatement(): it splits the line into a destination name, a
// source template, and the field's offset/size templates, and checks their
// *shape* only. $macro resolution, numeric parsing, and every range/fit
// check (does bit_offset fit the source buffer, does the field cross a
// byte boundary, does value_size exceed 64) are deferred to execution time
// — see ScriptInterpreter's BITSTREAMVAL/BYTESTREAMVAL execution and
// StreamValStatement's doc comment (uScriptDataTypes.hpp) for the exact
// algorithm.
//
// The source/keyword split point is the FIRST '|' in the RHS (mirroring
// FormatStatement's own "input | template" split — see
// ScriptValidator::m_HandleFormatStmt()) rather than the LAST, unlike
// parseStreamStatement()'s REVERSE_BIT/REVERSE_BYTE suffix: there is no
// second, later '|' that could occur here, since a StreamValStatement has
// exactly one field and no optional trailing modifier.
//
// This form is restricted to exactly one field by construction (there is no
// field-list loop below, unlike parseStreamStatement()) — a line with more
// than one field is already rejected earlier, at classification time, by
// usyntax::m_isBitstreamValStmt()/m_isBytestreamValStmt() (uScriptSyntax.hpp),
// whose pattern has no repetition group. For one-or-more fields extracted
// from the same source in a single statement, see parseStreamValArrayStatement()
// below (the "name [= ..." array form) instead.
//
// @param strKeyword  "BITSTREAMVAL" or "BYTESTREAMVAL" — selects which
//                     keyword must appear right after the '|', and which
//                     field shape to parse (see bByteMode).
// @param bByteMode    false: parse the field as "bit_offset:value_size"
//                     (BITSTREAMVAL). true: parse it as
//                     "byte_offset:bit_offset:value_size" (BYTESTREAMVAL) —
//                     three ':'-separated parts, same separator convention
//                     as every other field shape in this grammar.
// @param strLine      the full raw statement text, e.g.
//                      "v ?= $frame | BITSTREAMVAL 64:1"
//                     Already macro-expanded for CONSTANT ("name := value")
//                     macros by the caller, exactly as every other
//                     statement type expects.
// @param out          receives strName, strSourceTpl, strByteOffsetTpl
//                     (BYTESTREAMVAL only — left empty for BITSTREAMVAL),
//                     strBitOffsetTpl, strValueSizeTpl and bByteMode.
// @param strError     receives a human-readable reason on failure.
// @return false on any structural problem (missing '?=', missing '|',
//         missing/wrong keyword right after '|', empty source, or a field
//         that isn't exactly the shape bByteMode expects).
// ---------------------------------------------------------------------------
inline bool parseStreamValStatement(const std::string& strKeyword,
                                     bool                bByteMode,
                                     const std::string& strLine,
                                     StreamValStatement& out,
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

    // ── 2. Split the RHS at the first '|' into source and "KEYWORD field" ──
    std::string strRhs = trim(strLine.substr(assignPos + kAssign.size()));
    const auto pipePos = strRhs.find('|');
    if (pipePos == std::string::npos) {
        strError = strKeyword + ": missing '|' separator between the hex source and " + strKeyword;
        return false;
    }

    out.strSourceTpl = trim(strRhs.substr(0, pipePos));
    if (out.strSourceTpl.empty()) {
        strError = strKeyword + ": hex source is empty";
        return false;
    }

    std::string strAfterPipe = trim(strRhs.substr(pipePos + 1));

    // ── 3. Strip the keyword from what follows the '|' ──────────────────
    if (strAfterPipe.size() < strKeyword.size() ||
        strAfterPipe.compare(0, strKeyword.size(), strKeyword) != 0 ||
        // Reject e.g. "BITSTREAMVALX" being mistaken for the "BITSTREAMVAL" keyword.
        (strAfterPipe.size() > strKeyword.size() &&
         !std::isspace(static_cast<unsigned char>(strAfterPipe[strKeyword.size()])))) {
        strError = strKeyword + ": missing " + strKeyword + " keyword after '|'";
        return false;
    }
    const std::string strField = trim(strAfterPipe.substr(strKeyword.size()));
    if (strField.empty()) {
        strError = strKeyword + ": no field given";
        return false;
    }

    // ── 4. Parse the one field — shape depends on bByteMode ─────────────
    out.bByteMode = bByteMode;
    out.strByteOffsetTpl.clear();

    if (!bByteMode) {
        // BITSTREAMVAL: bit_offset:value_size
        const auto c1 = strField.find(':');
        if (c1 == std::string::npos) {
            strError = strKeyword + ": field [" + strField + "] is not bit_offset:value_size";
            return false;
        }
        if (strField.find(':', c1 + 1) != std::string::npos) {
            strError = strKeyword + ": field [" + strField + "] has more than 2 ':'-separated parts";
            return false;
        }
        out.strBitOffsetTpl = strField.substr(0, c1);
        out.strValueSizeTpl = strField.substr(c1 + 1);
        if (out.strBitOffsetTpl.empty() || out.strValueSizeTpl.empty()) {
            strError = strKeyword + ": field [" + strField + "] has an empty bit_offset/value_size part";
            return false;
        }
    } else {
        // BYTESTREAMVAL: byte_offset:bit_offset:value_size
        const auto c1 = strField.find(':');
        if (c1 == std::string::npos) {
            strError = strKeyword + ": field [" + strField + "] is not byte_offset:bit_offset:value_size";
            return false;
        }
        const auto c2 = strField.find(':', c1 + 1);
        if (c2 == std::string::npos) {
            strError = strKeyword + ": field [" + strField + "] is not byte_offset:bit_offset:value_size";
            return false;
        }
        if (strField.find(':', c2 + 1) != std::string::npos) {
            strError = strKeyword + ": field [" + strField + "] has more than 3 ':'-separated parts";
            return false;
        }
        out.strByteOffsetTpl = strField.substr(0, c1);
        out.strBitOffsetTpl  = strField.substr(c1 + 1, c2 - c1 - 1);
        out.strValueSizeTpl  = strField.substr(c2 + 1);
        if (out.strByteOffsetTpl.empty() || out.strBitOffsetTpl.empty() || out.strValueSizeTpl.empty()) {
            strError = strKeyword + ": field [" + strField + "] has an empty byte_offset/bit_offset/value_size part";
            return false;
        }
    }

    return true;

} // parseStreamValStatement()

// ---------------------------------------------------------------------------
// parseStreamValArrayStatement — structural parser for one BITSTREAMVAL/
// BYTESTREAMVAL *array* line (the "name [= ..." form).
//
//   name [= <hex_source> | BITSTREAMVAL  <bit_offset1>:<value_size1> [<bit_offset2>:<value_size2> ...]
//   name [= <hex_source> | BYTESTREAMVAL <byte_offset1>:<bit_offset1>:<value_size1> [...]
//
// Shared by ScriptValidator::m_HandleBitstreamValArrayStmt()/
// m_HandleBytestreamValArrayStmt() (compiled .script files) and
// ScriptInterpreter::executeCmd() (interactive shell lines), same reason as
// parseStreamValStatement() above: the two entry points can never drift
// apart.
//
// This is the array counterpart of parseStreamValStatement(): same division
// of labour (STRUCTURAL parsing only — $macro resolution, numeric parsing,
// and every range/fit check are deferred to execution time), same per-field
// shape (bByteMode selects "bit_offset:value_size" vs
// "byte_offset:bit_offset:value_size", exactly like parseStreamValStatement()),
// but the field list after the keyword may hold one-or-more
// whitespace-separated fields instead of exactly one — the same
// whitespace-splitting loop parseStreamStatement() uses for its own
// "offset:length:value" field list.
//
// The name/source/keyword split mirrors parseStreamValStatement() exactly,
// except the destination name is split off at the FIRST '[=' rather than
// '?=' (see ScriptValidator::m_HandleArrayMacro()'s own "NAME [= ..." split)
// — this is what makes the array form lexically unambiguous from the scalar
// form without needing to count fields first.
//
// @param strKeyword  "BITSTREAMVAL" or "BYTESTREAMVAL" — selects which
//                     keyword must appear right after the '|', and which
//                     field shape each entry of the list must have (see
//                     bByteMode).
// @param bByteMode    false: parse each field as "bit_offset:value_size"
//                     (BITSTREAMVAL). true: parse each field as
//                     "byte_offset:bit_offset:value_size" (BYTESTREAMVAL).
// @param strLine      the full raw statement text, e.g.
//                      "v [= $frame | BITSTREAMVAL 64:1 34:2 19:3"
//                     Already macro-expanded for CONSTANT ("name := value")
//                     macros by the caller, exactly as every other
//                     statement type expects.
// @param out          receives strName, strSourceTpl, vFields (one
//                     StreamValField per whitespace-separated field, in
//                     order) and bByteMode.
// @param strError     receives a human-readable reason on failure.
// @return false on any structural problem (missing '[=', missing '|',
//         missing/wrong keyword right after '|', empty source, no fields
//         given, or a field that isn't exactly the shape bByteMode expects).
// ---------------------------------------------------------------------------
inline bool parseStreamValArrayStatement(const std::string&        strKeyword,
                                          bool                       bByteMode,
                                          const std::string&        strLine,
                                          StreamValArrayStatement&  out,
                                          std::string&              strError) noexcept
{
    auto trim = [](std::string s) -> std::string {
        const size_t fs = s.find_first_not_of(" \t");
        const size_t fe = s.find_last_not_of(" \t");
        return (fs == std::string::npos) ? std::string() : s.substr(fs, fe - fs + 1);
    };

    // ── 1. Split at first '[=' ──────────────────────────────────────────
    static const std::string kArrayAssign = "[=";
    const auto assignPos = strLine.find(kArrayAssign);
    if (assignPos == std::string::npos) {
        strError = strKeyword + ": missing '[='";
        return false;
    }

    out.strName = trim(strLine.substr(0, assignPos));
    if (out.strName.empty()) {
        strError = strKeyword + ": missing destination array macro name";
        return false;
    }

    // ── 2. Split the RHS at the first '|' into source and "KEYWORD fields" ──
    std::string strRhs = trim(strLine.substr(assignPos + kArrayAssign.size()));
    const auto pipePos = strRhs.find('|');
    if (pipePos == std::string::npos) {
        strError = strKeyword + ": missing '|' separator between the hex source and " + strKeyword;
        return false;
    }

    out.strSourceTpl = trim(strRhs.substr(0, pipePos));
    if (out.strSourceTpl.empty()) {
        strError = strKeyword + ": hex source is empty";
        return false;
    }

    std::string strAfterPipe = trim(strRhs.substr(pipePos + 1));

    // ── 3. Strip the keyword from what follows the '|' ──────────────────
    if (strAfterPipe.size() < strKeyword.size() ||
        strAfterPipe.compare(0, strKeyword.size(), strKeyword) != 0 ||
        // Reject e.g. "BITSTREAMVALX" being mistaken for the "BITSTREAMVAL" keyword.
        (strAfterPipe.size() > strKeyword.size() &&
         !std::isspace(static_cast<unsigned char>(strAfterPipe[strKeyword.size()])))) {
        strError = strKeyword + ": missing " + strKeyword + " keyword after '|'";
        return false;
    }
    std::string strFieldList = trim(strAfterPipe.substr(strKeyword.size()));
    if (strFieldList.empty()) {
        strError = strKeyword + ": no fields given";
        return false;
    }

    // ── 4. Split the field list on whitespace and parse each field ──────
    out.bByteMode = bByteMode;
    out.vFields.clear();
    {
        std::string::size_type pos = 0;
        while (pos < strFieldList.size()) {
            const auto spacePos = strFieldList.find_first_of(" \t", pos);
            const std::string strField = (spacePos == std::string::npos)
                                              ? strFieldList.substr(pos)
                                              : strFieldList.substr(pos, spacePos - pos);
            pos = (spacePos == std::string::npos) ? strFieldList.size() : strFieldList.find_first_not_of(" \t", spacePos);

            if (strField.empty()) {
                continue;
            }

            StreamValField sField;

            if (!bByteMode) {
                // BITSTREAMVAL: bit_offset:value_size
                const auto c1 = strField.find(':');
                if (c1 == std::string::npos) {
                    strError = strKeyword + ": field [" + strField + "] is not bit_offset:value_size";
                    return false;
                }
                if (strField.find(':', c1 + 1) != std::string::npos) {
                    strError = strKeyword + ": field [" + strField + "] has more than 2 ':'-separated parts";
                    return false;
                }
                sField.strBitOffsetTpl = strField.substr(0, c1);
                sField.strValueSizeTpl = strField.substr(c1 + 1);
                if (sField.strBitOffsetTpl.empty() || sField.strValueSizeTpl.empty()) {
                    strError = strKeyword + ": field [" + strField + "] has an empty bit_offset/value_size part";
                    return false;
                }
            } else {
                // BYTESTREAMVAL: byte_offset:bit_offset:value_size
                const auto c1 = strField.find(':');
                if (c1 == std::string::npos) {
                    strError = strKeyword + ": field [" + strField + "] is not byte_offset:bit_offset:value_size";
                    return false;
                }
                const auto c2 = strField.find(':', c1 + 1);
                if (c2 == std::string::npos) {
                    strError = strKeyword + ": field [" + strField + "] is not byte_offset:bit_offset:value_size";
                    return false;
                }
                if (strField.find(':', c2 + 1) != std::string::npos) {
                    strError = strKeyword + ": field [" + strField + "] has more than 3 ':'-separated parts";
                    return false;
                }
                sField.strByteOffsetTpl = strField.substr(0, c1);
                sField.strBitOffsetTpl  = strField.substr(c1 + 1, c2 - c1 - 1);
                sField.strValueSizeTpl  = strField.substr(c2 + 1);
                if (sField.strByteOffsetTpl.empty() || sField.strBitOffsetTpl.empty() || sField.strValueSizeTpl.empty()) {
                    strError = strKeyword + ": field [" + strField + "] has an empty byte_offset/bit_offset/value_size part";
                    return false;
                }
            }

            out.vFields.push_back(std::move(sField));
        }
    }

    if (out.vFields.empty()) {
        strError = strKeyword + ": no fields given";
        return false;
    }

    return true;

} // parseStreamValArrayStatement()

#endif // U_STREAM_STATEMENT_PARSER_HPP
