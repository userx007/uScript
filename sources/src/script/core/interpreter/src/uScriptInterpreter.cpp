#include "uScriptInterpreter.hpp"
#include "uScriptCommandValidator.hpp"    
#include "uScriptDataTypes.hpp"        
#include "uStreamStatementParser.hpp"
#include "uString.hpp"
#include "uTimer.hpp"
#include "uLogger.hpp"
#include "uCalculator.hpp"
#include "uCheckContinue.hpp"
#include "uHexlify.hpp"
#include "uGuiNotify.hpp"
#include "uExecContext.hpp"

#include <regex>
#include <sstream>
#include <iomanip>
#include <set>
#include <utility>
#include <filesystem>
#include <algorithm>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CORE_SCR_I  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL CONSTANTS                                  //
/////////////////////////////////////////////////////////////////////////////////

static constexpr std::string_view kEvalPrefix  = "EVAL ";
static constexpr std::string_view kMathPrefix  = "MATH ";
static constexpr std::string_view kFmtPrefix   = "FORMAT ";
static constexpr std::string_view kPrintPrefix = "PRINT ";

/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::interpretScript(ScriptEntriesType& sScriptEntries, bool bRealExec)
{
    bool bRetVal = false;

    do {
        if(false == bRealExec) {

            m_sScriptEntries = &sScriptEntries;

            if (false == m_loadPlugins()) {
                break;
            }

            // Create a fresh plugin entry for each PLUGIN:N instance that is
            // referenced in commands but not yet explicitly declared.
            m_autoInstantiatePlugins();

            if (false == m_crossCheckCommands()) {
                break;
            }

            if (false == m_initPlugins()) {
                break;
            }

            // only validate commands (dry run)
            if (false == m_executeCommands(false)) {
                break;
            }

        } else {

            // if plugins argument validation passed then we enable the plugins for the real execution
            if (false == m_enablePlugins()) {
                break;
            }

            // execute commands
            if (false == m_executeCommands(true)) {
                break;
            }
        }

        bRetVal = true;

    } while(false);

    // Join all threads that are still running (covers both normal completion
    // and early exit via break).  m_joinAllThreads() signals stop_token on
    // every jthread so cooperative plugins can exit cleanly, then joins each
    // one unconditionally.  This is a no-op when no "&" commands were used.
    if (bRealExec) {
        m_joinAllThreads();
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Script execution"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* interpretScript()*/


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listMacrosPlugins()
{
    // Prints a header followed by every key:value pair in any string→string map.
    // Extracted to eliminate four structurally identical for_each blocks below.
    auto printKVMap = [](const auto& map, const std::string& header) {
        if (!map.empty()) {
            LOG_PRINT(LOG_EMPTY, LOG_STRING(header));
            for (const auto& entry : map) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING(entry.first); LOG_STRING(":"); LOG_STRING(entry.second));
            }
        }
    };

    printKVMap(m_sScriptEntries->mapMacros, LOG_HEADER_CMACROS);

    if (!m_sScriptEntries->mapArrayMacros.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING(LOG_HEADER_ARRAYS));
        std::for_each(m_sScriptEntries->mapArrayMacros.begin(), m_sScriptEntries->mapArrayMacros.end(),
            [](const auto& arr) {
                std::ostringstream oss;
                oss << arr.first << "[" << arr.second.size() << "]: ";
                for (size_t k = 0; k < arr.second.size(); ++k) {
                    if (k > 0) oss << ", ";
                    oss << "[" << k << "]=" << arr.second[k];
                }
                LOG_PRINT(LOG_EMPTY, LOG_STRING(oss.str()));
            });
    }

    // Show runtime variable macro values — these are the values most recently
    // written by executed MacroCommands, which is what the script actually sees.
    printKVMap(m_RuntimeVarMacros, LOG_HEADER_VMACROS);

    if (!m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING(LOG_HEADER_PLUGINS));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
            [&](auto& plugin) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING([&]{ std::ostringstream o; o << std::left << std::setw(12) << plugin.strPluginName; return o.str(); }()); 
                    LOG_STRING(plugin.sGetParams.strPluginVersion); 
                    LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands, ' ')));
            });
    }

    return true;

} /* listMacrosPlugins()*/


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listCommands()
{
    LOG_PRINT(LOG_EMPTY, LOG_STRING(LOG_HEADER_COMMANDS));
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&](const ScriptLine& data) {
            std::visit([&data](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, Command> || std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(command.strPlugin + "." + command.strCommand + " " + command.strParams));
                }
            }, data.command);
        }
    );

    return true;

} /* listCommands() */


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::loadPlugin(const std::string& strPluginName, bool bInitEnable)
{
    bool bRetVal = false;
    std::string strPluginNameUppecase = ustring::touppercase(strPluginName);

    if (!m_pluginIsLoaded(strPluginNameUppecase)) {
        PluginDataType command {
            strPluginNameUppecase,          // strPluginName
            "",                             // strPluginVersRule
            "",                             // strPluginVersRequested
            nullptr,                        // shptrPluginEntryPoint
            nullptr,                        // hLibHandle
            {},                             // sGetParams (empty PluginDataGet)
            {}                              // sSetParams (empty PluginDataSet)
        };

        if (true == (bRetVal = m_loadPlugin(command, bInitEnable))) {
            m_sScriptEntries->vPlugins.emplace_back(command);
        }
    }

    return bRetVal;

} /* loadPlugin() */


/*-------------------------------------------------------------------------------
  m_mirrorToShellVarMacros — copy a named runtime variable into the shell-scope
  map so that its value persists across executeCmd() calls.
  Called after every assignment-type token (VAR_MACRO_INIT, MATH_STMT,
  FORMAT_STMT, VARIABLE_MACRO) that runs through m_executeCommand.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_mirrorToShellVarMacros(const std::string& strName)
{
    auto [bFound, strValue] = m_getRuntimeVarMacro(strName);
    if (bFound) {
        m_ShellVarMacros[strName] = std::move(strValue);
    }
} /* m_mirrorToShellVarMacros() */


/*-------------------------------------------------------------------------------
  m_setRuntimeVarMacro() / m_getRuntimeVarMacro() - see declaration comment in
  uScriptInterpreter.hpp: every access to m_RuntimeVarMacros must go through
  these so that a threaded "VAL ?= PLUGIN.CMD args &" background writer can
  never race the main thread's reads/writes of the same unordered_map.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_setRuntimeVarMacro(const std::string& strName, std::string strValue)
{
    std::lock_guard<std::mutex> lock(m_runtimeVarMutex);
    m_RuntimeVarMacros[strName] = std::move(strValue);
} /* m_setRuntimeVarMacro() */

std::pair<bool, std::string> ScriptInterpreter::m_getRuntimeVarMacro(const std::string& strName)
{
    std::lock_guard<std::mutex> lock(m_runtimeVarMutex);
    auto it = m_RuntimeVarMacros.find(strName);
    if (it != m_RuntimeVarMacros.end()) {
        return {true, it->second};
    }
    return {false, {}};
} /* m_getRuntimeVarMacro() */


/*-------------------------------------------------------------------------------
  m_dispatchShellLine — wrap a pre-built command variant in a shell-origin
  ScriptLine (iLineNumber = 0) and execute it immediately (bRealExec = true).
  Centralises the boilerplate that every executeCmd() token case repeats.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_dispatchShellLine(decltype(ScriptLine::command) variant)
{
    ScriptLine data { 0, std::move(variant) };
    size_t szDummyIndex = 0;
    return m_executeCommand(data, true, szDummyIndex);
} /* m_dispatchShellLine() */


/*-------------------------------------------------------------------------------
  m_buildStreamStatement — shared BITSTREAM/BYTESTREAM execution engine.

  See StreamStatement's doc comment (uScriptDataTypes.hpp) for the exact
  bit-numbering / field-anchoring convention this implements — in short:
  big-endian bit numbering across the whole buffer (bit 0 = MSB of byte 0);
  within one field, "offset" is the field's LAST (LSB) bit, and the field
  extends backward (toward lower indices) for "length" bits, MSB-first.
  BYTESTREAM is handled by translating byte_offset into the equivalent
  BITSTREAM offset (byte_offset*8 + 7) up front (step 1 below) and capping
  length at 8; everything after that is identical for both keywords.

  Steps:
   1. Expand $macros and parse offset/length/value for every field (as
      uint64_t — numeric::str2uint64 already rejects a leading '-' and any
      non-integer text, so a negative or floating-point token fails here
      with a clear message rather than being silently misinterpreted).
   2. Range-check each field: length must be 1..64 (1..8 for BYTESTREAM),
      value must fit in `length` bits, and the field must not reach below
      bit 0.
   3. Size the output buffer: (highest offset used) + 1 bits, rounded up to
      a whole byte — see the doc comment for why this is reliable
      regardless of any field's length.
   4. Pack every field's value into the buffer MSB-first, tracking which
      field owns each bit so two fields claiming the same bit is caught as
      an overlap error rather than silently OR'd/overwritten.
   5. Apply the optional REVERSE_BIT/REVERSE_BYTE post-processor.
   6. Hexlify the result.

  Returns false and logs a reason on any resolution/range/overlap error.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_buildStreamStatement(const StreamStatement& command, const std::string& lineNr,
                                                std::string& strResultHex) noexcept
{
    const char* pszKind = command.bByteMode ? "BYTESTREAM" : "BITSTREAM";

    struct ResolvedField { uint64_t offset; uint64_t length; uint64_t value; };
    std::vector<ResolvedField> vResolved;
    vResolved.reserve(command.vFields.size());

    // ── 1 & 2: resolve + range-check every field ──────────────────────────
    for (const auto& field : command.vFields) {

        auto resolveOne = [&](const std::string& strTpl, const char* pszWhich, uint64_t& out) -> bool {
            std::string strExpanded = strTpl;
            if (!m_replaceVariableMacros(strExpanded)) {
                return false; // fatal: constant array index out of range, already logged
            }
            if (!numeric::str2uint64(strExpanded, out)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(pszKind); LOG_STRING(": field ["); LOG_STRING(strTpl);
                          LOG_STRING("] -"); LOG_STRING(pszWhich); LOG_STRING("=[");
                          LOG_STRING(strExpanded); LOG_STRING("] is not a valid non-negative integer"));
                return false;
            }
            return true;
        };

        uint64_t rawOffset = 0, length = 0, value = 0;
        if (!resolveOne(field.strOffsetTpl, "offset", rawOffset)) return false;
        if (!resolveOne(field.strLengthTpl, "length", length))   return false;
        if (!resolveOne(field.strValueTpl,  "value",  value))    return false;

        if (length == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(pszKind); LOG_STRING(": length must be at least 1 bit (offset="); LOG_UINT64(rawOffset); LOG_STRING(")"));
            return false;
        }

        const uint64_t szMaxLength = command.bByteMode ? 8 : 64;
        if (length > szMaxLength) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(pszKind); LOG_STRING(": length"); LOG_UINT64(length);
                      LOG_STRING(command.bByteMode
                                  ? "exceeds 8 bits (a BYTESTREAM field cannot cross a byte boundary — use BITSTREAM for that)"
                                  : "exceeds 64 bits (maximum supported field width)"));
            return false;
        }

        // BYTESTREAM: "offset" names a BYTE index; translate it to the
        // equivalent BITSTREAM offset — the LAST (LSB) bit of that byte —
        // so everything below (fit/overlap/sizing/packing) is identical
        // for both keywords. See StreamStatement's doc comment.
        uint64_t offset = rawOffset;
        if (command.bByteMode) {
            if (rawOffset > (UINT64_MAX - 7) / 8) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(pszKind); LOG_STRING(": byte offset"); LOG_UINT64(rawOffset); LOG_STRING("is too large"));
                return false;
            }
            offset = rawOffset * 8 + 7;
        }

        const uint64_t szMaxValue = (length >= 64) ? UINT64_MAX : ((uint64_t(1) << length) - 1);
        if (value > szMaxValue) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(pszKind); LOG_STRING(": value"); LOG_UINT64(value);
                      LOG_STRING("cannot fit on"); LOG_UINT64(length); LOG_STRING("bits (max"); LOG_UINT64(szMaxValue); LOG_STRING(")"));
            return false;
        }

        if (offset + 1 < length) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(pszKind); LOG_STRING(": field with offset"); LOG_UINT64(rawOffset);
                      LOG_STRING("and length"); LOG_UINT64(length); LOG_STRING("would start before bit 0"));
            return false;
        }

        vResolved.push_back(ResolvedField{offset, length, value});
    }

    // ── 3. Size the output buffer ──────────────────────────────────────────
    uint64_t maxOffset = 0;
    for (const auto& f : vResolved) {
        maxOffset = std::max(maxOffset, f.offset);
    }
    const uint64_t szTotalBits  = maxOffset + 1;
    const size_t   szTotalBytes = static_cast<size_t>((szTotalBits + 7) / 8);

    static constexpr size_t kMaxStreamBytes = 65536; // sanity cap against a typo'd huge offset
    if (szTotalBytes > kMaxStreamBytes) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING(pszKind); LOG_STRING(": resulting stream ("); LOG_SIZET(szTotalBytes);
                  LOG_STRING("bytes) exceeds the"); LOG_SIZET(kMaxStreamBytes); LOG_STRING("byte sanity limit"));
        return false;
    }

    // ── 4. Pack every field, tracking per-bit ownership for overlap detection ──
    std::vector<uint8_t> vBytes(szTotalBytes, 0);
    std::vector<int64_t> vOwner(static_cast<size_t>(szTotalBits), -1); // -1 = unclaimed, else field index

    for (size_t idx = 0; idx < vResolved.size(); ++idx) {
        const auto&    f        = vResolved[idx];
        const uint64_t firstBit = f.offset - f.length + 1; // this field's MSB, globally

        for (uint64_t b = 0; b < f.length; ++b) {
            const uint64_t bitIndex = firstBit + b;

            if (vOwner[static_cast<size_t>(bitIndex)] != -1) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(pszKind); LOG_STRING(": field #"); LOG_SIZET(idx);
                          LOG_STRING("(offset"); LOG_UINT64(f.offset);
                          LOG_STRING(") overlaps field #"); LOG_INT64(vOwner[static_cast<size_t>(bitIndex)]);
                          LOG_STRING("at bit"); LOG_UINT64(bitIndex));
                return false;
            }
            vOwner[static_cast<size_t>(bitIndex)] = static_cast<int64_t>(idx);

            // value's bit (length-1-b) -> this global bit, MSB-first across the field.
            if ((f.value >> (f.length - 1 - b)) & 1ULL) {
                const size_t   szByteIdx  = static_cast<size_t>(bitIndex / 8);
                const unsigned uBitInByte = static_cast<unsigned>(bitIndex % 8); // 0 = MSB of the byte
                vBytes[szByteIdx] |= static_cast<uint8_t>(0x80u >> uBitInByte);
            }
        }
    }

    // ── 5. Optional REVERSE_BIT / REVERSE_BYTE post-processing ─────────────
    if (command.eReverse == StreamReverseMode::REVERSE_BYTE) {
        std::reverse(vBytes.begin(), vBytes.end());
    } else if (command.eReverse == StreamReverseMode::REVERSE_BIT) {
        std::reverse(vBytes.begin(), vBytes.end());
        for (auto& b : vBytes) {
            b = static_cast<uint8_t>(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
            b = static_cast<uint8_t>(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
            b = static_cast<uint8_t>(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
        }
    }

    // ── 6. Hexlify ───────────────────────────────────────────────────────
    strResultHex = hexutils::stringHexlify(vBytes);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING(pszKind); LOG_STRING("->"); LOG_SIZET(vBytes.size());
              LOG_STRING("bytes ["); LOG_STRING(strResultHex); LOG_STRING("]"));

    return true;

} // m_buildStreamStatement()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::executeCmd(const std::string& strCommand)
{
    bool bRetVal = true;

    std::string strCommandTemp(strCommand);

    ustring::replaceMacros(strCommandTemp, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);
    ustring::replaceMacros(strCommandTemp, m_ShellVarMacros, SCRIPT_MACRO_MARKER);

    Token token;
    ScriptCommandValidator validator;

    if (true == validator.validateCommand(0, strCommandTemp, token)) {
        switch(token) {

            case Token::CONSTANT_MACRO : {
                std::vector<std::string> vstrTokens;
                ustring::tokenize(strCommandTemp, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

                if (vstrTokens.size() == 2) {
                    // cmacroname := cmacroval                         | cmacroname |  cmacroval   |
                    auto aRetVal = m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);
                    if (false == aRetVal.second) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("cmacro already exists:"); LOG_STRING(vstrTokens[0]));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid cmacro"));
                    bRetVal = false;
                }
                break;
            }

            case Token::VARIABLE_MACRO : {
                std::vector<std::string> vstrDelimiters{SCRIPT_VARIABLE_MACRO_SEPARATOR, SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
                std::vector<std::string> vstrTokens;
                ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                const size_t szSize = vstrTokens.size();

                if ((szSize == 3) || (szSize == 4)) {
                    std::string strParams = (szSize == 4) ? vstrTokens[3] : "";
                    const bool bThreaded = extractIsThreaded(strParams);
                    bRetVal = m_dispatchShellLine(
                        MacroCommand{vstrTokens[1], vstrTokens[2], strParams, vstrTokens[0], bThreaded}
                    );
                    // For the sequential (non-threaded) case, m_executeCommand already
                    // wrote the result into m_RuntimeVarMacros synchronously by the time
                    // m_dispatchShellLine() returns; mirror it to m_ShellVarMacros so it
                    // persists across executeCmd calls.
                    // For the threaded case (?= ... &) there is nothing to mirror yet -
                    // the background thread keeps updating m_RuntimeVarMacros directly,
                    // which m_replaceVariableMacros() already consults ahead of
                    // m_ShellVarMacros, so later executeCmd() calls transparently see
                    // whatever value is current at the time they run.
                    if (!bThreaded && !vstrTokens[0].empty()) {
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    }
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid vmacro"));
                    bRetVal = false;
                }

                break;
            }

            case Token::COMMAND : {
                std::vector<std::string> vstrDelimiters{SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
                std::vector<std::string> vstrTokens;
                ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                if (vstrTokens.size() >= 2) {
                    std::string strParams = (vstrTokens.size() == 3) ? vstrTokens[2] : "";
                    const bool bThreaded = extractIsThreaded(strParams);
                    bRetVal = m_dispatchShellLine(
                        Command{vstrTokens[0], vstrTokens[1], strParams, bThreaded}
                    );
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid command"));
                    bRetVal = false;
                }
                break;
            }

            case Token::PRINT_STMT : {
                std::string strText = strCommandTemp;
                ustring::stripPrefix(strText, kPrintPrefix);
                bRetVal = m_dispatchShellLine(PrintStatement{ strText });
                break;
            }

            case Token::MATH_STMT : {
                // Format: <n> ?= MATH <expression>  ->  tokens: [ name, "MATH <expr>" ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        std::string strExpr = vstrTokens[1];
                        ustring::stripPrefix(strExpr, kMathPrefix);
                        bRetVal = m_dispatchShellLine(MathStatement{ vstrTokens[0], strExpr });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid MATH_STMT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::BITSTREAM_STMT :
            case Token::BYTESTREAM_STMT : {
                // parseStreamStatement() does the whole "<n> ?= KEYWORD ..." split
                // itself (name / fields / optional REVERSE_BIT|REVERSE_BYTE) — see
                // uStreamStatementParser.hpp, shared with ScriptValidator so this
                // interactive form can never drift from the compiled-script one.
                const bool        bByteMode = (token == Token::BYTESTREAM_STMT);
                const std::string strKeyword = bByteMode ? "BYTESTREAM" : "BITSTREAM";

                StreamStatement sStmt;
                std::string     strError;
                if (parseStreamStatement(strKeyword, strCommandTemp, sStmt, strError)) {
                    sStmt.bByteMode = bByteMode;
                    const std::string strName = sStmt.strName;
                    bRetVal = m_dispatchShellLine(std::move(sStmt));
                    m_mirrorToShellVarMacros(strName);
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strError));
                    bRetVal = false;
                }
                break;
            }

            case Token::VAR_MACRO_INIT : {
                // Format: <n> ?= <value template>  ->  tokens: [ name, valueTpl ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        bRetVal = m_dispatchShellLine(VarMacroInit{ vstrTokens[0], vstrTokens[1] });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid VAR_MACRO_INIT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::FORMAT_STMT : {
                // Format: <n> ?= FORMAT <input> | <pattern>  ->  tokens: [ name, "FORMAT <input>", pattern ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR, STRING_SEPARATOR_PIPE };
                    std::vector<std::string> vstrTokens;

                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FORMAT_STMT strCommandTemp=["); LOG_STRING(strCommandTemp); LOG_STRING("]"));

                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 3) {
                        std::string strInput = vstrTokens[1];
                        ustring::stripPrefix(strInput, kFmtPrefix);
                        bRetVal = m_dispatchShellLine(FormatStatement{ vstrTokens[0], strInput, vstrTokens[2] });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid FORMAT_STMT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::ARRAY_MACRO : {
                // Format validated by ScriptCommandValidator:
                //   <name> := [ val0, val1, ... ]
                // Tokenize on ':=' to get [ name, "[ val0, val1, ... ]" ]
                // then strip brackets and split on ',' to build the vector.
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_CONSTANT_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        const std::string& strName    = vstrTokens[0];
                        std::string        strContent = vstrTokens[1];

                        // Strip surrounding '[' ... ']' (validator guarantees they exist).
                        const auto szOpen  = strContent.find('[');
                        const auto szClose = strContent.rfind(']');
                        if (szOpen != std::string::npos && szClose != std::string::npos && szClose > szOpen) {
                            strContent = strContent.substr(szOpen + 1, szClose - szOpen - 1);
                        }

                        // Split on ',' to obtain individual element strings.
                        std::vector<std::string> vstrElements;
                        ustring::tokenize(strContent, ",", vstrElements);

                        // Trim whitespace from every element.
                        for (auto& elem : vstrElements) {
                            ustring::trim(elem);
                        }

                        // Register (or overwrite) the array in the shared map.
                        auto aRetVal = m_sScriptEntries->mapArrayMacros.emplace(strName, vstrElements);
                        if (false == aRetVal.second) {
                            // Array already exists — update its value in-place.
                            aRetVal.first->second = vstrElements;
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                      LOG_STRING("ARRAY_MACRO updated:"); LOG_STRING(strName);
                                      LOG_STRING("size="); LOG_STRING(std::to_string(vstrElements.size())));
                        } else {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                      LOG_STRING("ARRAY_MACRO created:"); LOG_STRING(strName);
                                      LOG_STRING("size="); LOG_STRING(std::to_string(vstrElements.size())));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid ARRAY_MACRO"));
                        bRetVal = false;
                    }
                }
                break;
            }

            default: {
                break;
            }
        };
    }

    return bRetVal;

} /* executeCmd() */


/////////////////////////////////////////////////////////////////////////////////
//                       PRIVATE INTERFACES                                    //
/////////////////////////////////////////////////////////////////////////////////

/*-------------------------------------------------------------------------------
  m_evaluateCondition — unified condition evaluator.

  Dispatches to EvalExprEvaluator when the condition string starts with the
  "EVAL " prefix (case-sensitive), and to BoolExprEvaluator for everything
  else (plain TRUE / FALSE / || / && expressions as before).

  This is the single call-site that replaces all direct m_beEvaluator.evaluate()
  calls, so adding new evaluator back-ends in the future requires only changes
  here rather than throughout m_executeCommand / m_runEndRepeat.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_evaluateCondition(const std::string& strCondition, bool& result) noexcept
{
    std::string strExpr = strCondition;
    ustring::stripPrefix(strExpr, kEvalPrefix);

    if (strExpr.size() < strCondition.size()) {
        // Prefix was present — delegate to the typed evaluator.
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("EVAL expression:"); LOG_STRING(strExpr));
        return m_evalExprEvaluator.evaluate(strExpr, result);
    }

    // Plain boolean expression (TRUE / FALSE / && / ||).
    return m_beEvaluator.evaluate(strCondition, result);

} /* m_evaluateCondition() */


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugin(PluginDataType& command, bool bInitEnable)
{
    bool bRetVal = false;

    do {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Loading :"); LOG_STRING(command.strPluginName));
        
        auto [handle, error] = m_PluginLoader(command.strPluginName);
        if (!(handle.first && handle.second))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING("-> loading failed"));
            if (error) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(error.value().message));
            } else {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Reason unknown, error wasn't set"));
            }
            break; // Exit early on failure
        }

        // Transfer the pointers to the internal storage
        command.hLibHandle = std::move(handle.first);
        command.shptrPluginEntryPoint = std::move(handle.second);

        // Retrieve data from plugin
        command.shptrPluginEntryPoint->getParams(&command.sGetParams);

        if (m_IniCfgLoader.isLoaded()) {
            if (m_IniCfgLoader.sectionExists(command.strPluginName)) {
                if (false == m_IniCfgLoader.resolveSection(command.strPluginName, command.sSetParams.mapSettings)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING(command.strPluginName);
                              LOG_STRING(": failed to load settings from .ini file"));
                    break;
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING(command.strPluginName);
                          LOG_STRING(": no settings in .ini file"));
            }
        }

        command.sSetParams.shpLogger = getLogger();

        // Ensure ARTEFACTS_PATH always resolves relative to the main script's
        // directory, not the process working directory.
        //
        // Strategy:
        //   - If the ini did not set ARTEFACTS_PATH at all → inject the script dir.
        //   - If the ini set a relative path (e.g. "." or "subdir") → replace it
        //     with the script dir joined with that relative path, so that relative
        //     ini overrides still work when the script is not in the CWD.
        //   - If the ini set an absolute path → leave it untouched (explicit
        //     absolute paths are intentional and always unambiguous).
        if (!m_strScriptDir.empty()) {
            auto it = command.sSetParams.mapSettings.find("ARTEFACTS_PATH");
            if (it == command.sSetParams.mapSettings.end()) {
                // Not in ini at all — use script directory
                command.sSetParams.mapSettings["ARTEFACTS_PATH"] = m_strScriptDir;
            } else if (!std::filesystem::path(it->second).is_absolute()) {
                // Relative ini value — resolve it against the script directory
                command.sSetParams.mapSettings["ARTEFACTS_PATH"] =
                    (std::filesystem::path(m_strScriptDir) / it->second).string();
            }
            // else: absolute path in ini — leave it as-is
        }

        // set parameters to plugin
        if (false == command.shptrPluginEntryPoint->setParams(&command.sSetParams)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING(": failed to set params loaded from .ini file"));
            break; // Exit early on failure
        }

        // Lambda to print plugin info
        auto printPluginInfo =  [](const std::string& name, const std::string& version, const std::vector<std::string>& vs) {
            std::ostringstream oss;
            oss << name << "| v" << version << " | ";
            for (const auto& cmd : vs) {
                oss << cmd << " ";
            }
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(oss.str()); LOG_STRING("| loaded"));
        };
        printPluginInfo(command.strPluginName, command.sGetParams.strPluginVersion, command.sGetParams.vstrPluginCommands);

        // if explicitly requested, perform also the plugin initialization and enabling 
        if (bInitEnable) {
            if (false == command.shptrPluginEntryPoint->doInit((true == command.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to initialize plugin:"); LOG_STRING(command.strPluginName));
                bRetVal = false;
            }
            if (!command.shptrPluginEntryPoint->doEnable()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to enable plugin:"); LOG_STRING(command.strPluginName));
                bRetVal = false;
            }
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING("initialized and enabled"));
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

} /* m_loadPlugin() */



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_pluginIsLoaded(const std::string& strPluginName) noexcept
{
    auto it = std::find_if(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&strPluginName](const PluginDataType& p) { return p.strPluginName == strPluginName; });

    bool bFound = (it != m_sScriptEntries->vPlugins.end());
    if (bFound) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(it->strPluginName); LOG_STRING("already loaded"));
    }

    return bFound;

} /* m_pluginIsLoaded() */



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugins() noexcept
{
    bool bRetVal = true;

    for (auto& command : m_sScriptEntries->vPlugins) {
        if (false == m_loadPlugin(command, false)) {
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Plugin loading"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* m_loadPlugins() */



/*-------------------------------------------------------------------------------
  Scan vCommands for instanced plugin references (PLUGIN:N) whose base PLUGIN
  is declared in vPlugins but the instance entry does not yet exist.
  For every such unique instance name a fresh PluginDataType is pushed into
  vPlugins and m_loadPlugin() is called so it gets its own .so handle,
  pluginEntry() object, and INI section (e.g. [UART:1]).
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_autoInstantiatePlugins() noexcept
{
    // Collect all unique instanced plugin names used by commands.
    std::set<std::string> usedInstances;
    for (const auto& line : m_sScriptEntries->vCommands) {
        std::visit([&usedInstances](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Command> || std::is_same_v<T, MacroCommand>) {
                const auto& name = item.strPlugin;
                if (name.find(':') != std::string::npos)
                    usedInstances.insert(name);
            }
        }, line.command);
    }

    for (const auto& instanceName : usedInstances) {
        // Skip if already registered (e.g. user wrote LOAD_PLUGIN UART:1 explicitly)
        if (m_pluginIsLoaded(instanceName)) continue;

        // Find the base plugin (e.g. "UART" for "UART:1")
        const std::string baseName = instanceName.substr(0, instanceName.find(':'));
        auto baseIt = std::find_if(
            m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
            [&baseName](const PluginDataType& p) { return p.strPluginName == baseName; });

        if (baseIt == m_sScriptEntries->vPlugins.end()) {
            // Base not loaded — m_validatePlugins already caught this; skip silently.
            continue;
        }

        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("Auto-instantiating"); LOG_STRING(instanceName);
                  LOG_STRING("from base"); LOG_STRING(baseName));

        // Snapshot the fields we need from baseIt BEFORE emplace_back.
        // emplace_back may reallocate vPlugins, which invalidates all iterators
        // and references into the vector — including baseIt itself.
        const std::string versRule      = baseIt->strPluginVersRule;
        const std::string versRequested = baseIt->strPluginVersRequested;

        // Create a new entry for the instance, inheriting the version rule.
        // Use the same 4-arg constructor form as the validator's m_HandleLoadPlugin.
        m_sScriptEntries->vPlugins.emplace_back(
            instanceName,
            versRule,        // copied before reallocation — baseIt is now potentially dangling
            versRequested,
            nullptr   // shptrPluginEntryPoint — filled by m_loadPlugin
        );

        // Load the instance (opens the .so again via dlopen and reads [INSTANCE] INI section).
        // NOTE: vPlugins may reallocate during the loop; always use the last element.
        m_loadPlugin(m_sScriptEntries->vPlugins.back(), false);
    }

} /* m_autoInstantiatePlugins() */

/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_crossCheckCommands () noexcept
{
    bool bRetVal = true;

    // Build the per-plugin command-set index once for this check.
    m_buildPluginCommandIndex();

    for (const auto& data : m_sScriptEntries->vCommands) {
        std::visit([this, &bRetVal, &data](const auto & command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                auto pluginIt = m_pluginCmdIndex.find(command.strPlugin);
                if (pluginIt != m_pluginCmdIndex.end()) {
                    if (pluginIt->second.count(command.strCommand) == 0) {
                        auto lineNr = ustring::fmtLineNr(data.iLineNumber);
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                                LOG_STRING("Command") 
                                LOG_STRING(command.strCommand);
                                LOG_STRING("unsupported by plugin"); 
                                LOG_STRING(command.strPlugin));
                        bRetVal = false;
                    }
                }
            }
        }, data.command);
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
            LOG_STRING("Commands availability"); 
            LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* m_crossCheckCommands() */



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_initPlugins () noexcept
{
    bool bRetVal = true;

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        if (false == plugin.shptrPluginEntryPoint->doInit((true == plugin.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to initialize plugin:"); 
                      LOG_STRING(plugin.strPluginName));
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
                LOG_STRING("Plugins initialization"); 
                LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* m_initPlugins() */



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_enablePlugins() noexcept
{
    for (auto& plugin : m_sScriptEntries->vPlugins) {
        if (!plugin.shptrPluginEntryPoint->doEnable()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to enable plugin:");
                      LOG_STRING(plugin.strPluginName));
            return false;
        }
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING(plugin.strPluginName);
                  LOG_STRING("enabled"));
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Plugins enabling ok"));
    return true;

} /* m_enablePlugins() */


/*-------------------------------------------------------------------------------
 * Traverse the command list in reverse to resolve macros using their most recently assigned values.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_replaceVariableMacros(std::string& input)
{
    /* 
    Extended pattern — four forms:
       $NAME.$indexmacro  → array element access, variable index   (groups 1=NAME  2=indexmacro)
       $NAME.N            → array element access, constant index   (groups 1=NAME  4=N)
       $NAME.SIZE         → array size access                      (groups 1=NAME  3="SIZE")
       $NAME              → regular macro lookup   (group  1=NAME, groups 2/3/4 empty)
    
     The \.\$ in the optional suffix means a literal dot followed by a literal
     dollar sign, ensuring that $NAME.$indexmacro is consumed as a single match
     rather than two consecutive matches. SIZE is matched as a literal
     keyword (case-sensitive, upper-case) alongside the $indexmacro alternative
     so that $NAME.SIZE is likewise consumed as a single match. The constant
     index alternative (group 4) is a plain run of decimal digits, guarded by
     the same kind of negative lookahead as SIZE so that "$NAME.12abc" is not
     mis-consumed as index "12abc".
    
     Returns false only when a CONSTANT array index (the $NAME.N form) is out
     of range: that is a script-authoring error the author could have caught
     before running (unlike a variable index, whose value is only known at
     runtime), so it is fatal — logged and execution is aborted. A variable
     index that is out of range remains non-fatal: it is logged and the
     macro reference is left unexpanded so execution can continue.
    */

    static const std::regex macroPattern(R"(\$([A-Za-z_][A-Za-z0-9_]*)(?:\.(?:\$([A-Za-z_][A-Za-z0-9_]*)|(SIZE)(?![A-Za-z0-9_])|([0-9]+)(?![A-Za-z0-9_])))?)");
    std::smatch match;

    /* Helper: resolve a single bare macro name through all scope tiers.
      Returns the resolved string, or an empty optional if not found. */

    auto resolveName = [&](const std::string& name) -> std::pair<bool, std::string> {

        // Loop-scoped macros — innermost first
        for (auto scopeIt = m_loopStateStack.rbegin();
             scopeIt != m_loopStateStack.rend(); ++scopeIt)
        {
            auto loopIt = scopeIt->mapLoopMacros.find(name);
            if (loopIt != scopeIt->mapLoopMacros.end()) {
                return {true, loopIt->second};
            }
        }

        // Script-level variable macros — O(1) runtime map.
        // Holds the value that was most recently EXECUTED, not the value that
        // appears last in the IR.  This is correct when the same name is used
        // on both sides of an assignment (e.g. score ?= CORE.MATH $score + 10).
        // Goes through m_getRuntimeVarMacro() because a threaded "VAL ?=
        // PLUGIN.CMD args &" background thread may be updating this same map
        // concurrently - see m_runtimeVarMutex.
        {
            auto [bFound, strValue] = m_getRuntimeVarMacro(name);
            if (bFound) {
                return {true, std::move(strValue)};
            }
        }

        // Shell macros
        {
            auto shellIt = m_ShellVarMacros.find(name);
            if (shellIt != m_ShellVarMacros.end()) {
                return {true, shellIt->second};
            }
        }
        return {false, {}};
    };

    bool replaced = true;
    while (replaced) {
        replaced = false;
        std::string result;
        result.reserve(input.size());
        std::string::const_iterator searchStart = input.cbegin();

        while (std::regex_search(searchStart, input.cend(), match, macroPattern)) {
            result.append(match.prefix());

            const std::string macroName    = match[1].str();
            const bool        hasIndex     = match[2].matched;
            const bool        hasSize      = match[3].matched;
            const bool        hasConstIndex = match[4].matched;
            const std::string indexName    = hasIndex ? match[2].str() : "";
            const std::string constIndex   = hasConstIndex ? match[4].str() : "";

            bool found = false;

            if (hasSize) {
                // Array size access: $macroName.SIZE ----
                auto arrIt = m_sScriptEntries->mapArrayMacros.find(macroName);
                if (arrIt != m_sScriptEntries->mapArrayMacros.end()) {
                    result.append(std::to_string(arrIt->second.size()));
                    found    = true;
                    replaced = true;
                } else {
                    // macroName is NOT an array — resolve it as a regular macro and
                    // re-emit the .SIZE suffix literally so it is not silently dropped.
                    auto [nameFound, nameVal] = resolveName(macroName);
                    if (nameFound) {
                        result.append(nameVal);
                        result.append(".SIZE");
                        found    = true;
                        replaced = true;
                    }
                    // else: leave the full $name.SIZE unexpanded
                }
            } else if (hasIndex) {
                // Array element access: $macroName.$indexName ----
                auto arrIt = m_sScriptEntries->mapArrayMacros.find(macroName);
                if (arrIt != m_sScriptEntries->mapArrayMacros.end()) {

                    // Resolve the index macro to a numeric string
                    auto [idxFound, idxVal] = resolveName(indexName);

                    if (idxFound) {
                        try {
                            size_t idx = static_cast<size_t>(std::stoull(idxVal));
                            if (idx < arrIt->second.size()) {
                                result.append(arrIt->second[idx]);
                                found    = true;
                                replaced = true;
                            } else {
                                LOG_PRINT(LOG_ERROR, LOG_HDR;
                                          LOG_STRING("Array ["); LOG_STRING(macroName);
                                          LOG_STRING("] index"); LOG_STRING(idxVal);
                                          LOG_STRING("out of range (size=");
                                          LOG_STRING(std::to_string(arrIt->second.size())); LOG_STRING(")"));
                                // Leave unexpanded — do not crash
                            }
                        } catch (...) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR;
                                      LOG_STRING("Array ["); LOG_STRING(macroName);
                                      LOG_STRING("] non-numeric index:"); LOG_STRING(idxVal));
                            // Leave unexpanded
                        }
                    }
                    // Index macro not yet resolved — leave unexpanded, next pass will retry
                } else {

                    // macroName is NOT an array — resolve it as a regular macro and
                    // re-emit the .$indexName suffix literally so it is not silently dropped.
                    auto [nameFound, nameVal] = resolveName(macroName);
                    if (nameFound) {
                        result.append(nameVal);
                        result.append(".$");
                        result.append(indexName);
                        found    = true;
                        replaced = true;
                    }
                    // else: leave the full $name.$index unexpanded
                }
            } else if (hasConstIndex) {
                // Array element access, CONSTANT index: $macroName.N ----
                auto arrIt = m_sScriptEntries->mapArrayMacros.find(macroName);
                if (arrIt != m_sScriptEntries->mapArrayMacros.end()) {

                    // constIndex is guaranteed all-digits by the regex, but a
                    // huge literal (more digits than size_t can hold) can still
                    // make stoull throw — treated the same as out-of-range.
                    try {
                        size_t idx = static_cast<size_t>(std::stoull(constIndex));
                        if (idx < arrIt->second.size()) {
                            result.append(arrIt->second[idx]);
                            found    = true;
                            replaced = true;
                        } else {
                            LOG_PRINT(LOG_ERROR, LOG_HDR;
                                      LOG_STRING("Array ["); LOG_STRING(macroName);
                                      LOG_STRING("] constant index"); LOG_STRING(constIndex);
                                      LOG_STRING("out of range (size=");
                                      LOG_STRING(std::to_string(arrIt->second.size())); LOG_STRING(")"));
                            // Fatal: a constant index is known at script-authoring
                            // time, so an out-of-range one is a script error —
                            // stop execution rather than silently continuing.
                            return false;
                        }
                    } catch (...) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("Array ["); LOG_STRING(macroName);
                                  LOG_STRING("] invalid constant index:"); LOG_STRING(constIndex));
                        return false;
                    }
                } else {

                    // macroName is NOT an array — resolve it as a regular macro and
                    // re-emit the .N suffix literally so it is not silently dropped.
                    auto [nameFound, nameVal] = resolveName(macroName);
                    if (nameFound) {
                        result.append(nameVal);
                        result.append(".");
                        result.append(constIndex);
                        found    = true;
                        replaced = true;
                    }
                    // else: leave the full $name.N unexpanded
                }
            } else {
                // Regular macro lookup 
                auto [nameFound, nameVal] = resolveName(macroName);
                if (nameFound) {
                    result.append(nameVal);
                    found    = true;
                    replaced = true;
                }
            }

            if (!found) {
                result.append(match[0]); // leave unexpanded
            }
            searchStart = match.suffix().first;
        }
        result.append(searchStart, input.cend());
        input = result;
    }

    return true;

} /* m_replaceVariableMacros() */



/*-------------------------------------------------------------------------------
  m_initLoopIterIndex — write iteration counter "0" into the loop's own macro
  scope on first entry.  Called by both RepeatTimes and RepeatUntil handlers
  immediately after pushing a new LoopState onto the stack.
  No-op when strVarMacroName is empty (loop has no capture variable).
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_initLoopIterIndex(LoopState& state) noexcept
{
    if (!state.strVarMacroName.empty()) {
        const std::string strVal = state.bIsUntil
            ? "0"
            : (state.bRangeIsInteger ? std::to_string(state.llCurrent)
                                      : formatRepeatDouble(state.dCurrent));
        state.mapLoopMacros[state.strVarMacroName] = strVal;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT iter-index $"); LOG_STRING(state.strVarMacroName);
                  LOG_STRING("="); LOG_STRING(strVal));
    }
} /* m_initLoopIterIndex() */


/*-------------------------------------------------------------------------------
  m_advanceLoopIterIndex — increment the iteration counter and update the
  loop-scope macro.  Called by m_runEndRepeat on each loop-back.
  No-op when strVarMacroName is empty.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_advanceLoopIterIndex(LoopState& state) noexcept
{
    ++state.uIterationCount;

    std::string strVal;
    if (state.bIsUntil) {
        // REPEAT UNTIL has no range to walk — the capture macro is a plain
        // 0-based iteration counter, as before.
        strVal = std::to_string(state.uIterationCount);
    } else {
        if (state.bRangeIsInteger) { state.llCurrent += state.llStep; strVal = std::to_string(state.llCurrent); }
        else                       { state.dCurrent  += state.dStep;  strVal = formatRepeatDouble(state.dCurrent); }
    }

    if (!state.strVarMacroName.empty()) {
        state.mapLoopMacros[state.strVarMacroName] = strVal;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT iter-index $"); LOG_STRING(state.strVarMacroName);
                  LOG_STRING("="); LOG_STRING(strVal));
    }
} /* m_advanceLoopIterIndex() */


/*-------------------------------------------------------------------------------
  m_runEndRepeat — shared END_REPEAT logic.
  Called from the normal END_REPEAT path and from the CONTINUE_LOOP path.
  Assumes m_loopStateStack.back() is the loop being ended.
  May modify iIndex (loop-back) or pop the stack (loop done).
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_runEndRepeat(size_t& iIndex, bool& bRetVal) noexcept
{
    LoopState& state = m_loopStateStack.back();

    // Save values used in post-pop log lines before any pop_back().
    const std::string strLabel = state.strLabel;

    if (!state.bIsUntil) {

        // REPEAT range — loop back only if the *next* value (current + step)
        // would still satisfy the range predicate implied by step's sign.
        const bool bHasNext = state.bRangeIsInteger
            ? (state.llStep > 0 ? (state.llCurrent + state.llStep <  state.llEnd)
                                 : (state.llCurrent + state.llStep >  state.llEnd))
            : (state.dStep  > 0 ? (state.dCurrent  + state.dStep  <  state.dEnd)
                                 : (state.dCurrent  + state.dStep  >  state.dEnd));

        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT"); LOG_STRING(strLabel);
                  LOG_STRING(bHasNext ? "looping" : "done"));

        if (bHasNext) {
            m_advanceLoopIterIndex(state);
            iIndex = state.szBeginIndex; // caller does ++iIndex → szBeginIndex+1
        } else {
            m_loopStateStack.pop_back(); // destroys mapLoopMacros — state ref is now dangling
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT done:"); LOG_STRING(strLabel));
        }
    } else {

        // REPEAT UNTIL 
        // Copy the condition template before any macro expansion (do not mutate it).
        std::string strCondExpanded = state.strCondition;
        if (!m_replaceVariableMacros(strCondExpanded)) {
            bRetVal = false; // fatal: constant array index out of range, already logged
            return;
        }

        bool bCondResult = false;
        if (true == m_evaluateCondition(strCondExpanded, bCondResult)) {
            if (!bCondResult) {
                m_advanceLoopIterIndex(state);
                iIndex = state.szBeginIndex;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT UNTIL looping:"); LOG_STRING(strLabel));
            } else {
                m_loopStateStack.pop_back(); // destroys mapLoopMacros — state ref is now dangling
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT UNTIL done:"); LOG_STRING(strLabel));
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("REPEAT UNTIL: failed to evaluate condition:"); LOG_STRING(strCondExpanded));
            bRetVal = false;
        }
    }

} /* m_runEndRepeat() */


/*-------------------------------------------------------------------------------
  m_harvestFinishedThreads — erase ThreadEntry objects whose "done" flag is
  true (the thread lambda has already returned).

  Must be called with m_threadsMutex already held.  Frees jthread objects for
  threads that have completed, keeping m_threads compact.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_harvestFinishedThreads() noexcept
{
    m_threads.erase(
        std::remove_if(m_threads.begin(), m_threads.end(),
            [](const ThreadEntry& e) {
                return e.done->load(std::memory_order_acquire);
            }),
        m_threads.end());

} /* m_harvestFinishedThreads() */


/*-------------------------------------------------------------------------------
  m_joinAllThreads — signal stop on all active threads then join each one.

  request_stop() is called on every jthread first so that plugins polling
  stop_token can begin winding down in parallel before the sequential join
  loop starts.  join() then blocks until each thread returns naturally —
  there is no timeout because jthread provides no timed join, and abandoning
  a thread is never safe.

  Called automatically at the end of interpretScript() after the last script
  command has executed, ensuring all threads are joined before the application
  continues past the script execution boundary.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_joinAllThreads() noexcept
{
    std::vector<ThreadEntry> toJoin;

    {
        std::lock_guard<std::mutex> lock(m_threadsMutex);

        // Signal stop on all threads before joining any, so they can begin
        // winding down cooperatively in parallel.
        for (auto& entry : m_threads) {
            entry.thread.request_stop();
        }

        toJoin = std::move(m_threads);
        m_busyPlugins.clear();
    }

    for (auto& entry : toJoin) {
        if (entry.thread.joinable()) {
            entry.thread.join();
        }
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("All threads joined."));

} /* m_joinAllThreads() */


/*-------------------------------------------------------------------------------
  Execute a single IR command.

  iIndex is the current position in vCommands (owned by the caller's loop).
  Loop end nodes (RepeatEnd) may set iIndex to (desired_next - 1) so that the
  caller's unconditional ++iIndex lands at the correct body-start address.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommand (ScriptLine& data, bool bRealExec, size_t& iIndex) noexcept
{
    bool bRetVal = true;
    bool bIsPluginCommand = false;
    auto lineNr = ustring::fmtLineNr(data.iLineNumber);
    const int  lineNo = data.iLineNumber;   // captured by value into the visit lambda below

    // Notify the GUI front-end which main-script line is about to execute.
    // In CLI mode g_gui_mode is false so this is a single branch-not-taken.
    if (bRealExec) {
		gui_notify_exec_main(data.iLineNumber);
	}

    std::visit([this, bRealExec, lineNo, &lineNr, &bIsPluginCommand, &bRetVal, &iIndex](auto& command) {
        using T = std::decay_t<decltype(command)>;

        /*-----------------------------------------------------------------
            Plugin commands (Command / MacroCommand)
        -----------------------------------------------------------------*/

        if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
            if (m_eSkipReason == SkipReason::NONE) {
                bIsPluginCommand = true;
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (command.strPlugin == plugin.strPluginName) {
                        if(bRealExec) { // real execution

                            // Expand macros onto a copy on the MAIN THREAD before any
                            // thread is created.  The thread receives only the already-
                            // expanded string and never touches interpreter state maps.
                            std::string strExpandedParams = command.strParams;
                            if (!m_replaceVariableMacros(strExpandedParams)) {
                                bRetVal = false; // fatal: constant array index out of range, already logged
                                return;
                            }

                            if (command.bThreaded) {
                                // ---- Threaded dispatch ----
                                // Guard: reject a second simultaneous thread for the
                                // same plugin instance (plugin is not required to be re-entrant).
                                {
                                    std::lock_guard<std::mutex> lock(m_threadsMutex);
                                    if (m_busyPlugins.count(command.strPlugin)) {
                                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                                            LOG_STRING("Cannot launch thread: plugin already has an active thread:");
                                            LOG_STRING(command.strPlugin));
                                        bRetVal = false;
                                        break;
                                    }
                                }

                                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(lineNr.data());
                                    LOG_STRING("Launching thread for:");
                                    LOG_STRING(command.strPlugin + "." + command.strCommand + " " + strExpandedParams));

                                // Shared done-flag: set by thread on exit; read by harvest/join.
                                auto doneFlag = std::make_shared<std::atomic<bool>>(false);

                                // Capture everything needed by VALUE — no references into
                                // interpreter state cross the thread boundary.
                                std::jthread t(
                                    [sPluginEntryPoint = plugin.shptrPluginEntryPoint,
                                     strCommand        = command.strCommand,
                                     strParams         = strExpandedParams,
                                     strPlugin         = command.strPlugin,
                                     strVarMacroName   = [&command]() -> std::string {
                                         if constexpr (std::is_same_v<T, MacroCommand>) {
                                             return command.strVarMacroName;
                                         } else {
                                             return std::string{};
                                         }
                                     }(),
                                     lineNo,
                                     doneFlag,
                                     this]
                                    (std::stop_token st) mutable
                                    {
                                        // Pass the stop_token into doDispatch so the plugin
                                        // can poll st.stop_requested() inside its own loop
                                        // and return early when cancellation is requested.
                                        // Sequential (non-threaded) calls use the default
                                        // token whose stop_requested() always returns false.
                                        //
                                        // Set the thread-local comm tid to lineNo so that any
                                        // CommScriptClient called from this thread emits the
                                        // threaded LOAD_COMM_T / EXEC_COMM_T / CLEAR_COMM_T
                                        // protocol messages instead of the non-threaded variants.
                                        // Reset to 0 after dispatch regardless of outcome.
                                        if (!st.stop_requested()) {
                                            set_gui_comm_tid(lineNo);

                                            if constexpr (std::is_same_v<T, MacroCommand>) {
                                                // ---- Variable-capture threaded loop ----
                                                // "VAL ?= PLUGIN.CMD args &": re-dispatch the
                                                // command forever (each call blocks internally
                                                // with its own read timeout - e.g. a "receive
                                                // whatever is sent" CMD - so this loop paces
                                                // itself and never busy-spins). Every successful
                                                // iteration atomically refreshes VAL with
                                                // whatever getData() produced for that dispatch,
                                                // so the rest of the script always sees the most
                                                // recently received value when it reads VAL.
                                                // Uses the plain (non-token) doDispatch overload:
                                                // cancellation is checked between iterations
                                                // instead of inside a single dispatch call.
                                                while (!st.stop_requested()) {
                                                    if (!sPluginEntryPoint->doDispatch(strCommand, strParams)) {
                                                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                                            LOG_STRING("Threaded var-capture command failed, stopping loop:");
                                                            LOG_STRING(strPlugin + "." + strCommand));
                                                        break;
                                                    }
                                                    const std::string strValue = sPluginEntryPoint->getData();
                                                    // A successful dispatch with nothing received (e.g. a
                                                    // "receive whatever is sent" CMD whose read timed out
                                                    // with 0 bytes) yields an empty result here - see
                                                    // receiveAndHexdump()'s READ_TIMEOUT handling. That is
                                                    // a normal idle tick, not new data, so it must NOT
                                                    // clobber VAL back to "": only overwrite the captured
                                                    // variable when something was actually received.
                                                    if (!strValue.empty()) {
                                                        m_setRuntimeVarMacro(strVarMacroName, strValue);
                                                        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                                            LOG_STRING("VAR["); LOG_STRING(strVarMacroName);
                                                            LOG_STRING("]->["); LOG_STRING(strValue); LOG_STRING("]"));
                                                    }
                                                    sPluginEntryPoint->resetData();
                                                }
                                            } else {
                                                sPluginEntryPoint->doDispatch(strCommand, strParams, st);
                                            }

                                            set_gui_comm_tid(0);
                                        }
                                        // Clear busy flag so the same plugin can be launched again.
                                        {
                                            std::lock_guard<std::mutex> lk(m_threadsMutex);
                                            m_busyPlugins.erase(strPlugin);
                                        }
                                        // Notify GUI that this thread's line is no longer active.
                                        gui_notify_thread_done(lineNo);
                                        // Signal harvest that this entry is reclaimable.
                                        doneFlag->store(true, std::memory_order_release);
                                    }
                                );

                                {
                                    std::lock_guard<std::mutex> lock(m_threadsMutex);
                                    m_harvestFinishedThreads();  // prune completed entries first
                                    m_busyPlugins.insert(command.strPlugin);
                                    m_threads.push_back(ThreadEntry{std::move(t), doneFlag});
                                }

                                // Notify GUI so it draws the thread-active rectangle on this line.
                                if (bRealExec) {
                                    gui_notify_thread_start(lineNo);
                                }

                                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(lineNr.data());
                                    LOG_STRING("Thread launched ok:"); LOG_STRING(command.strPlugin));

                            } else {
                                // ---- Sequential dispatch (bThreaded=false) ----
                                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(lineNr.data()); 
                                    LOG_STRING("Exec:"); 
                                    LOG_STRING(command.strPlugin + "." + command.strCommand + " " + strExpandedParams));
                                {
                                    utime::Timer timer(std::string(lineNr.data()) + " Command");
                                    if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, strExpandedParams)) {
                                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                            LOG_STRING("Failed executing"); 
                                            LOG_STRING(command.strPlugin + "." + command.strCommand + " " + strExpandedParams)); 
                                            bRetVal = false;
                                        break;
                                    } else { // execution succeeded, update the value of the associated macro if any
                                        if constexpr (std::is_same_v<T, MacroCommand>) {
                                            const std::string strValue = plugin.shptrPluginEntryPoint->getData();
                                            m_setRuntimeVarMacro(command.strVarMacroName, strValue);
                                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                                LOG_STRING("VAR["); LOG_STRING(command.strVarMacroName); 
                                                LOG_STRING("]->[") 
                                                LOG_STRING(strValue); 
                                                LOG_STRING("]"));
                                            plugin.shptrPluginEntryPoint->resetData();
                                        }
                                    }
                                }
                            }

                            utime::delay_ms(m_szDelay); /* delay between the commands execution */

                        } else { // only for validation purposes
                            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(lineNr.data()); 
                                    LOG_STRING("Validate:"); 
                                    LOG_STRING(command.strPlugin + "." + command.strCommand); 
                                    LOG_STRING(command.strParams));
                            // Tell any downstream plugin command (in particular a *_CMD
                            // handler's ucmdexec::generic_cmd() -> CommScriptCommandInterpreter)
                            // that this is a dry-run dispatch, so it validates argument
                            // syntax (and may open/configure its driver) but stops one
                            // step short of the actual send/receive interface - see
                            // uExecContext.hpp for why this needs a thread-local instead
                            // of a doDispatch() parameter.
                            uexec::DryRunScope dryRunScope(true);
                            if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, command.strParams)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                    LOG_STRING("Failed validating"); 
                                    LOG_STRING(command.strPlugin + "." + command.strCommand); 
                                    LOG_STRING(command.strParams));
                                bRetVal = false;
                                break;
                            }
                        }
                    }
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                    LOG_STRING("Skipped:"); LOG_STRING(command.strPlugin); 
                    LOG_STRING(command.strCommand); LOG_STRING("args["); 
                    LOG_STRING(command.strParams); LOG_STRING("]"));
            }

        /*-----------------------------------------------------------------
            IF/GOTO condition
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, Condition>) {
            if(bRealExec) {
                if(m_eSkipReason == SkipReason::NONE) {
                    // Expand variable macros on a copy — constant macros were already
                    // substituted at validation time, but $vmacros are only known at
                    // runtime and must be resolved here before the evaluator sees them.
                    std::string strCondExpanded = command.strCondition;
                    if (!m_replaceVariableMacros(strCondExpanded)) {
                        bRetVal = false; // fatal: constant array index out of range, already logged
                        return;
                    }

                    bool beResult = false;

                    if (true == m_evaluateCondition(strCondExpanded, beResult)) {
                        if (true == beResult) {
                            m_strSkipUntilLabel = command.strLabelName;
                            m_eSkipReason       = SkipReason::GOTO;
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                LOG_STRING("Start skipping to label:"); 
                                LOG_STRING(m_strSkipUntilLabel));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                            LOG_STRING("Failed to evaluate condition:"); 
                            LOG_STRING(strCondExpanded));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                        LOG_STRING("Skipped:"); 
                        LOG_STRING("[IF ..] GOTO:"); 
                        LOG_STRING(command.strLabelName));
                }
            }

        /*-----------------------------------------------------------------
            GOTO/IF target label
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, Label>) {
            if(bRealExec) {
                if (m_strSkipUntilLabel == command.strLabelName &&
                    m_eSkipReason       == SkipReason::GOTO) {
                    m_strSkipUntilLabel.clear();
                    m_eSkipReason = SkipReason::NONE;
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                        LOG_STRING("Stop skipping at label:"); 
                        LOG_STRING(command.strLabelName));
                }
            }

        /*-----------------------------------------------------------------
            REPEAT_TIMES — push loop state on first entry
         (on loop-back iterations the caller jumps to iIndex+1, i.e. the
         first body command, so this node is only executed once per loop)
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, RepeatTimes>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                ResolvedRepeatRange range{};
                if (!m_resolveRepeatRange(command, range)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("REPEAT: failed to resolve range for loop:");
                              LOG_STRING(command.strLabel));
                    bRetVal = false;
                    return;
                }

                // Does [begin, end) contain at least one value when stepping by step?
                const bool bHasIter = range.bIsInteger
                    ? (range.llStep > 0 ? (range.llBegin < range.llEnd) : (range.llBegin > range.llEnd))
                    : (range.dStep  > 0 ? (range.dBegin  < range.dEnd)  : (range.dBegin  > range.dEnd));

                if (!bHasIter) {
                    // Empty range — skip the whole loop body without pushing a
                    // LoopState, reusing END_REPEAT's transparent BREAK_LOOP
                    // unwind logic (it pops nothing since nothing was pushed,
                    // and passes through unrelated nested loops untouched).
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("REPEAT: empty range, skipping body:"); LOG_STRING(command.strLabel));
                    m_strSkipUntilLabel = command.strLabel;
                    m_eSkipReason       = SkipReason::BREAK_LOOP;
                    return;
                }

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("REPEAT start:");
                          LOG_STRING(command.strLabel));

                LoopState state{};
                state.strLabel        = command.strLabel;
                state.szBeginIndex    = iIndex;
                state.bIsUntil        = false;
                state.strVarMacroName = command.strVarMacroName;
                state.uIterationCount = 0U;
                state.bRangeIsInteger = range.bIsInteger;
                state.llCurrent = range.llBegin; state.llEnd = range.llEnd; state.llStep = range.llStep;
                state.dCurrent  = range.dBegin;  state.dEnd  = range.dEnd;  state.dStep  = range.dStep;

                m_loopStateStack.push_back(std::move(state));
                // Write the initial loop value (== begin) into the loop's own scope.
                m_initLoopIterIndex(m_loopStateStack.back());
            }

        /*-----------------------------------------------------------------
            REPEAT_UNTIL — push loop state on first entry
         -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, RepeatUntil>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("REPEAT UNTIL start:"); 
                          LOG_STRING(command.strLabel);
                          LOG_STRING("cond:"); 
                          LOG_STRING(command.strCondition));

                LoopState state{};
                state.strLabel        = command.strLabel;
                state.szBeginIndex    = iIndex;
                state.bIsUntil        = true;
                state.strCondition    = command.strCondition;
                state.strVarMacroName = command.strVarMacroName;
                state.uIterationCount = 0U;
                state.bRangeIsInteger = true; // unused for UNTIL loops

                m_loopStateStack.push_back(std::move(state));
                // Write the initial iteration index "0" into the loop's own scope.
                m_initLoopIterIndex(m_loopStateStack.back());
            }

        /*-----------------------------------------------------------------
            END_REPEAT
        
         Four cases depending on m_eSkipReason:
        
           NONE         — normal execution: call m_runEndRepeat.
        
           GOTO         — a GOTO skip is in flight toward a LABEL node;
                          this END_REPEAT is transparent (no state change).
        
           BREAK_LOOP   — unwinding toward the named target.
                          Always pop the innermost LoopState.
                          If this IS the target: clear skip, resume after node.
                          If this is NOT the target: keep skipping outward.
        
           CONTINUE_LOOP— same incremental unwind, but when the target is
                          reached: do NOT pop — call m_runEndRepeat instead
                          so the loop decides whether to loop-back or exit.
        
         During dry-run (bRealExec == false) the node is always a no-op.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, RepeatEnd>) {
            if (bRealExec) {

                if (m_eSkipReason == SkipReason::NONE) {
                    // ---- Normal execution path ----
                    if (m_loopStateStack.empty() || m_loopStateStack.back().strLabel != command.strLabel) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("END_REPEAT: unexpected label or empty stack:"); 
                                  LOG_STRING(command.strLabel));
                        bRetVal = false;
                        return;
                    }
                    m_runEndRepeat(iIndex, bRetVal);

                } else if (m_eSkipReason == SkipReason::BREAK_LOOP) {
                    // ---- BREAK unwind ----
                    // Only pop if the back of the stack matches this END_REPEAT label.
                    // If it does not match, the REPEAT for this label was itself skipped
                    // (never pushed), so there is nothing to pop.
                    if (!m_loopStateStack.empty() &&
                        m_loopStateStack.back().strLabel == command.strLabel) {
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("BREAK: unwinding loop:"); 
                                  LOG_STRING(command.strLabel));
                        m_loopStateStack.pop_back();
                    }
                    if (command.strLabel == m_strSkipUntilLabel) {
                        // Target reached — resume after this END_REPEAT with no loop-back.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("BREAK: exited loop:"); 
                                  LOG_STRING(command.strLabel));
                    }

                } else if (m_eSkipReason == SkipReason::CONTINUE_LOOP) {
                    // ---- CONTINUE unwind ----
                    if (command.strLabel != m_strSkipUntilLabel) {
                        // Not the target yet. Only pop if this loop was actually pushed —
                        // i.e. its label matches the current stack back.
                        if (!m_loopStateStack.empty() &&
                            m_loopStateStack.back().strLabel == command.strLabel) {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                      LOG_STRING("CONTINUE: unwinding inner loop:"); 
                                      LOG_STRING(command.strLabel));
                            m_loopStateStack.pop_back();
                        }
                    } else {
                        // Target reached — clear skip, keep LoopState alive, run loop logic.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("CONTINUE: resuming at END_REPEAT:"); 
                                  LOG_STRING(command.strLabel));
                        m_runEndRepeat(iIndex, bRetVal);
                    }
                }
                // SkipReason::GOTO — transparent, do nothing.
            }

        /*-----------------------------------------------------------------
            BREAK <loop-label>
         Skip forward to END_REPEAT of the named loop; all intermediate
         loops are unwound by the END_REPEAT handler above.
         -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, LoopBreak>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("BREAK:"); 
                          LOG_STRING(command.strLabel));
                m_strSkipUntilLabel = command.strLabel;
                m_eSkipReason       = SkipReason::BREAK_LOOP;
            }

        /*-----------------------------------------------------------------
            CONTINUE <loop-label>
         Skip forward to END_REPEAT of the named loop, which then runs its
         normal loop-back or exit logic.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, LoopContinue>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("CONTINUE:"); 
                          LOG_STRING(command.strLabel));
                m_strSkipUntilLabel = command.strLabel;
                m_eSkipReason       = SkipReason::CONTINUE_LOOP;
            }

        /*-----------------------------------------------------------------
            PRINT [text]
         Expand all $macros in the stored text template and emit one log
         line.  An empty template produces a blank line.
         No plugin is involved — this is a native interpreter statement.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, PrintStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                std::string strExpanded = command.strText;
                if (!m_replaceVariableMacros(strExpanded)) {
                    bRetVal = false; // fatal: constant array index out of range, already logged
                    return;
                }
                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(strExpanded));
            }

        /*-----------------------------------------------------------------
            DELAY <value> <unit>
         Pause execution for the requested duration.
         Value and unit are pre-resolved at validation time — no parsing
         needed here.  The dry-run pass silently skips DELAY nodes so that
         argument validation is not slowed down by actual sleeps.
         Skipped (GOTO / BREAK / CONTINUE) DELAY nodes are also no-ops.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, DelayStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                const std::string strUnit = (command.eUnit == DelayUnit::US)  ? "us"  :
                                            (command.eUnit == DelayUnit::MS)  ? "ms"  : "sec";
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("DELAY:"); 
                          LOG_STRING(std::to_string(command.szValue));
                          LOG_STRING(strUnit));
                switch (command.eUnit) {
                    case DelayUnit::US:  utime::delay_us(command.szValue);      break;
                    case DelayUnit::MS:  utime::delay_ms(command.szValue);      break;
                    case DelayUnit::SEC: utime::delay_seconds(command.szValue); break;
                }
            }

        /*-----------------------------------------------------------------
            name ?= <string value>
         Expand $macros in the value template and write the result into
         m_RuntimeVarMacros.  This makes the value immediately visible to
         all subsequent $macro lookups at tier 2 — exactly the same as a
         successful MacroCommand dispatch.
         During the dry-run pass the node is silently ignored (no expansion,
         no write), consistent with the two-pass model used by plugin commands.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, VarMacroInit>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                std::string strExpanded = command.strValueTpl;
                if (!m_replaceVariableMacros(strExpanded)) {
                    bRetVal = false; // fatal: constant array index out of range, already logged
                    return;
                }

                // If the expanded value starts with "EVAL " delegate to the
                // unified condition evaluator and store "TRUE" or "FALSE".
                std::string strEvalCheck = strExpanded;
                ustring::stripPrefix(strEvalCheck, kEvalPrefix);
                if (strEvalCheck.size() < strExpanded.size())
                {
                    bool bEvalResult = false;
                    if (m_evaluateCondition(strExpanded, bEvalResult)) {
                        strExpanded = bEvalResult ? "TRUE" : "FALSE";
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("EVAL result for VAR_INIT ["); 
                                  LOG_STRING(command.strName);
                                  LOG_STRING("] -> ["); 
                                  LOG_STRING(strExpanded); LOG_STRING("]"));
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                  LOG_STRING("EVAL failed for VAR_INIT ["); 
                                  LOG_STRING(command.strName);
                                  LOG_STRING("]"));
                        bRetVal = false;
                        return;
                    }
                }

                m_setRuntimeVarMacro(command.strName, strExpanded);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("VAR_INIT ["); LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strExpanded); LOG_STRING("]"));
            }

        /*-----------------------------------------------------------------
            name ?= FORMAT input | format_pattern
        
         1. Expand $macros in both input and format templates.
         2. Tokenise the expanded input by whitespace → items[0..N-1].
         3. Walk the format template character by character:
              - '%' followed by a decimal digit → substitute items[digit]
              - '%' at end of template          → error (caught at validation)
              - any other char                  → copy verbatim
         4. Store the assembled string in m_RuntimeVarMacros[name].
        
         Out-of-range index (digit >= number of input tokens) is a runtime
         error: logged and the command fails so the script is aborted.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, FormatStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // macro expansion 
                std::string strInput  = command.strInputTpl;
                std::string strFormat = command.strFormatTpl;
                if (!m_replaceVariableMacros(strInput) || !m_replaceVariableMacros(strFormat)) {
                    bRetVal = false; // fatal: constant array index out of range, already logged
                    return;
                }

                // tokenise input by whitespace
                std::vector<std::string> vItems;
                {
                    std::istringstream iss(strInput);
                    std::string token;
                    while (iss >> token) {
                        vItems.push_back(std::move(token));
                    }
                }
                const size_t szNrItems = vItems.size();

                if (szNrItems == 0) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                              LOG_STRING("FORMAT ["); LOG_STRING(command.strName);
                              LOG_STRING("]: input expanded to empty — no items to substitute"));
                    bRetVal = false;
                    return;
                }

                // build output by walking the format template
                std::string strResult;
                strResult.reserve(strFormat.size());

                for (size_t i = 0; i < strFormat.size(); ++i) {
                    const char c = strFormat[i];
                    if (c == '%') {
                        // Validator guarantees a digit follows, but guard anyway.
                        if (i + 1 >= strFormat.size()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: '%' at end of expanded format template"));
                            bRetVal = false;
                            return;
                        }
                        const char cIdx = strFormat[++i];
                        if (!std::isdigit(static_cast<unsigned char>(cIdx))) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: '%"); 
                                      LOG_STRING(std::string(1, cIdx));
                                      LOG_STRING("' — index character is not a digit"));
                            bRetVal = false;
                            return;
                        }
                        const size_t uiIndex = static_cast<size_t>(cIdx - '0');
                        if (uiIndex >= szNrItems) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: index %"); 
                                      LOG_STRING(std::string(1, cIdx));
                                      LOG_STRING("out of range (input has");
                                      LOG_SIZET(szNrItems); 
                                      LOG_STRING("items)"));
                            bRetVal = false;
                            return;
                        }
                        strResult += vItems[uiIndex];
                    } else {
                        strResult += c;
                    }
                }

                // store result
                m_setRuntimeVarMacro(command.strName, strResult);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("FORMAT ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strResult); 
                          LOG_STRING("]"));
            }

        /*-----------------------------------------------------------------
            name ?= MATH <expression> [| HEX[_<width>][_<endian>]]
        
         1. Expand $macros in the expression template.
         2. Feed the expanded string to Calculator::evaluate().
         3. Convert the returned double to a clean string:
              - Integer-valued results print without a decimal point (5, not 5.0)
              - Floating-point results use up to 15 significant digits with
                trailing zeros stripped (3.14159, not 3.141590000000000)
         4. If a "| HEX..." post-processor was requested, overwrite that string
            with a fixed-width, zero-padded hex rendering of the integer result
            instead (see HexOutputFormat in uScriptDataTypes.hpp).
         5. Store the final string result in m_RuntimeVarMacros[name].
        
         The Calculator variable map (m_mathVars) is persistent for the
         lifetime of this ScriptInterpreter instance, so intra-expression
         assignments  (e.g.  MATH x = 5 + 3)  survive across MATH statements
         and are accessible in later evaluations as plain identifiers.
        
         During the dry-run pass (bRealExec == false) the node is silently
         ignored — consistent with VarMacroInit and FormatStatement.
         -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, MathStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // macro expansion 
                std::string strExpr = command.strExprTpl;
                if (!m_replaceVariableMacros(strExpr)) {
                    bRetVal = false; // fatal: constant array index out of range, already logged
                    return;
                }

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("MATH ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("] expr=["); 
                          LOG_STRING(strExpr); 
                          LOG_STRING("]"));

                // evaluate 
                double dResult = 0.0;
                try {
                    Calculator calc(strExpr, m_mathVars);
                    dResult = calc.evaluate();
                } catch (const std::exception& ex) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                              LOG_STRING("MATH ["); 
                              LOG_STRING(command.strName);
                              LOG_STRING("]: evaluation failed:"); 
                              LOG_STRING(ex.what());
                              LOG_STRING("expr=["); 
                              LOG_STRING(strExpr); 
                              LOG_STRING("]"));
                    bRetVal = false;
                    return;
                }

                // double -> string 
                // Use defaultfloat + 15 significant digits so integer results
                // print cleanly (5, not 5.000000) and precision is preserved.
                std::string strResult;
                {
                    std::ostringstream oss;
                    oss << std::defaultfloat << std::setprecision(15) << dResult;
                    strResult = oss.str();
                }

                // | HEX post-processor: convert the result to a fixed-width hex string.
                // Integer widths (HEX_8/16/32/64/128) use intToHexStringFixed, which
                // truncates the double to a two's-complement integer first — this only
                // faithfully represents a negative (or large-magnitude) value if it
                // actually fits within the requested byte width; the caller is
                // responsible for choosing a wide-enough format.
                // FLOAT/DOUBLE instead render the double's own IEEE-754 bit pattern
                // (floatToHexStringFixed/doubleToHexStringFixed) — since the sign is
                // just one dedicated bit within that pattern, positive and negative
                // values are rendered identically correctly, with no analogous
                // "does it fit" concern.
                // e.g. HEX_8: 255 → "FF"   HEX_16_BE: 255 → "00FF"   HEX_16_LE: 255 → "FF00"
                //      HEX_FLOAT_BE: -1.0 → "BF800000"
                if (command.eHexFormat != HexOutputFormat::NONE) {
                    const hexutils::Endianness eEndian = isHexFormatBigEndian(command.eHexFormat)
                                                              ? hexutils::Endianness::Big
                                                              : hexutils::Endianness::Little;

                    if (isHexFormatFloatingPoint(command.eHexFormat)) {
                        strResult = isHexFormatSinglePrecision(command.eHexFormat)
                                        ? hexutils::floatToHexStringFixed(static_cast<float>(dResult), eEndian)
                                        : hexutils::doubleToHexStringFixed(dResult, eEndian);
                    } else {
                        const uint64_t uVal = static_cast<uint64_t>(static_cast<int64_t>(dResult));
                        const size_t szByteWidth = getHexFormatByteWidth(command.eHexFormat);
                        strResult = hexutils::intToHexStringFixed(uVal, szByteWidth, eEndian);
                    }

                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("MATH HEX ["); 
                              LOG_STRING(command.strName);
                              LOG_STRING("] format=[");
                              LOG_STRING(getHexFormatName(command.eHexFormat));
                              LOG_STRING("] -> [");
                              LOG_STRING(strResult); LOG_STRING("]"));
                }

                // store result
                m_setRuntimeVarMacro(command.strName, strResult);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("MATH ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strResult); 
                          LOG_STRING("]"));
            }

        /*-----------------------------------------------------------------
            name ?= BITSTREAM  offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]
            name ?= BYTESTREAM byte_offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]

         All the actual work (macro expansion, numeric resolution, range/
         overlap checking, packing, REVERSE_BIT/REVERSE_BYTE, hexlify) lives
         in m_buildStreamStatement() — see its doc comment and
         StreamStatement's doc comment in uScriptDataTypes.hpp for the exact
         algorithm and bit-numbering convention. Store the hexlified result
         in m_RuntimeVarMacros[name], same as every other "name ?= ..."
         built-in (MATH, FORMAT, VAR_INIT).

         Skipped during the dry-run validation pass (bRealExec == false) and
         inside any active GOTO/BREAK/CONTINUE skip region, consistent with
         MathStatement/FormatStatement.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, StreamStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                std::string strResultHex;
                if (!m_buildStreamStatement(command, lineNr.data(), strResultHex)) {
                    bRetVal = false;
                    return;
                }

                m_setRuntimeVarMacro(command.strName, strResultHex);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING(command.bByteMode ? "BYTESTREAM [" : " BITSTREAM [");
                          LOG_STRING(command.strName);
                          LOG_STRING("]->[");
                          LOG_STRING(strResultHex);
                          LOG_STRING("]"));
            }

        /*-----------------------------------------------------------------
            BREAKPOINT [label]
        
         Suspends script execution and waits for user input via CheckContinue.
        
           a/A  → confirm abort (y/Y) → bRetVal = false → script aborts
           Space → skip this breakpoint, continue normally
           other → continue normally
        
         The optional label template is $macro-expanded at runtime so that
         loop indices and variable values are reflected in the log output.
        
         Skipped silently during the dry-run validation pass (bRealExec == false)
         and inside any active GOTO / BREAK / CONTINUE skip region — exactly
         consistent with DELAY and PRINT behaviour.
        -----------------------------------------------------------------*/

        } else if constexpr (std::is_same_v<T, BreakpointStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // Expand $macros in the label so the user sees current values
                std::string strLabel = command.strLabelTpl;
                if (!m_replaceVariableMacros(strLabel)) {
                    bRetVal = false; // fatal: constant array index out of range, already logged
                    return;
                }

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("BREAKPOINT hit:");
                          LOG_STRING(strLabel.empty() ? "<no label>" : strLabel));

                CheckContinue checkContinue;
                const bool bOk = checkContinue(strLabel);

                if (!bOk) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                              LOG_STRING("BREAKPOINT: script aborted by user"));
                    bRetVal = false;
                }
                // Note: the skip path (Space key) is handled inside CheckContinue
                // by setting *pbSkip. BREAKPOINT does not propagate the skip to
                // surrounding script flow — it only skips THIS breakpoint, not
                // the next command. nullptr is passed for pbSkip intentionally:
                // the Space key simply acts as "continue" for BREAKPOINT.
            }
        }
    }, data.command);

    if (bRealExec && m_eSkipReason == SkipReason::NONE && bIsPluginCommand) {
        LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING(lineNr.data());
                LOG_STRING("Command execution");
                LOG_STRING(bRetVal ? "ok" : "failed"));
    }

    // Notify the GUI front-end when real execution fails on this line so it
    // can show a red error bar.  The guard intentionally mirrors the exec
    // notification above: only fire when something actually went wrong and
    // only during real execution (dry-run errors are reported by the validator).
    if (bRealExec && !bRetVal) {
        gui_notify_error_main(data.iLineNumber);
    }

    return bRetVal;

} /* m_executeCommand()*/



/*-------------------------------------------------------------------------------
  Build a per-plugin command-name lookup used by m_crossCheckCommands.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_buildPluginCommandIndex() noexcept
{
    m_pluginCmdIndex.clear();

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        auto& cmdSet = m_pluginCmdIndex[plugin.strPluginName];
        for (const auto& cmd : plugin.sGetParams.vstrPluginCommands) {
            cmdSet.insert(cmd);
        }
    }

} /*m_buildPluginCommandIndex()*/


/*-------------------------------------------------------------------------------
  Index-based execution loop.
  Using an explicit index (instead of a range-for) allows RepeatEnd to set
  iIndex to (body_start - 1) so that the unconditional ++iIndex produces the
  correct next-iteration address.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommands (bool bRealExec) noexcept
{
    bool bRetVal = true;

    // Reset transient execution state before each pass.
    m_strSkipUntilLabel.clear();
    m_eSkipReason = SkipReason::NONE;
    m_loopStateStack.clear();
    {
        std::lock_guard<std::mutex> lock(m_runtimeVarMutex);
        m_RuntimeVarMacros.clear();
    }

    auto& vCommands = m_sScriptEntries->vCommands;
    size_t i = 0;

    while (i < vCommands.size()) {
        // Graceful-stop check: once per top-level loop iteration. Since
        // REPEAT/END_REPEAT are implemented as index-jumps within this very
        // loop rather than a separate nested one, this single check also
        // covers every REPEAT iteration - no extra plumbing needed there.
        // See uExecContext.hpp for why this is a polled flag-file rather than
        // a signal or stdin message. bRetVal=false here reaches the same
        // early-exit path as any other command failure, so the caller
        // (interpretScript()) still runs its usual m_joinAllThreads() cleanup,
        // which signals stop_token on every background command thread
        // (including an endless "VAL ?= PLUGIN.CMD ... &" capture loop) and
        // joins them before the script actually returns.
        if (uexec::isStopRequested()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Stop requested by user - aborting script"));
            bRetVal = false;
            break;
        }

        if (false == m_executeCommand(vCommands[i], bRealExec, i)) {
            bRetVal = false;
            break;
        }
        ++i;
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
        LOG_STRING("Commands"); 
        LOG_STRING(bRealExec ? "execution" : "validation"); 
        LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* m_executeCommands() */



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_resolveRepeatRange(const RepeatTimes& rep, ResolvedRepeatRange& out) noexcept
{
    // Resolve one bound: literal values were already parsed/typed at
    // validation time; "$macroname" bounds are expanded and (re-)parsed now.
    auto resolveOne = [&](const RepeatRangeValue& val, bool& bIsInt,
                           long long& llOut, double& dOut) -> bool {
        if (!val.bIsMacro) {
            bIsInt = val.bIsInteger;
            llOut  = val.llValue;
            dOut   = val.dValue;
            return true;
        }
        std::string strExpanded = val.strExpr;
        if (!m_replaceVariableMacros(strExpanded)) {
            return false; // fatal: constant array index out of range, already logged
        }
        if (!parseRepeatNumber(strExpanded, bIsInt, llOut, dOut)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("REPEAT: macro"); LOG_STRING(val.strExpr);
                      LOG_STRING("expanded to invalid number:"); LOG_STRING(strExpanded));
            return false;
        }
        return true;
    };

    bool      bBeginInt = true, bEndInt = true, bStepInt = true;
    long long llBegin = 0, llEnd = 0, llStep = 1;
    double    dBegin  = 0.0, dEnd = 0.0, dStep = 1.0;

    if (!resolveOne(rep.begin, bBeginInt, llBegin, dBegin)) { return false; }
    if (!resolveOne(rep.end,   bEndInt,   llEnd,   dEnd))   { return false; }
    if (!resolveOne(rep.step,  bStepInt,  llStep,  dStep))  { return false; }

    out.bIsInteger = bBeginInt && bEndInt && bStepInt;

    // Mirror both representations regardless of bIsInteger, using the exact
    // integer value where available so integer-only ranges keep full 64-bit
    // precision even though a double copy also exists.
    out.llBegin = llBegin; out.llEnd = llEnd; out.llStep = llStep;
    out.dBegin  = bBeginInt ? static_cast<double>(llBegin) : dBegin;
    out.dEnd    = bEndInt   ? static_cast<double>(llEnd)   : dEnd;
    out.dStep   = bStepInt  ? static_cast<double>(llStep)  : dStep;

    const bool bStepIsZero = out.bIsInteger ? (out.llStep == 0) : (out.dStep == 0.0);
    if (bStepIsZero) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("REPEAT: step must not be 0 for loop:"); LOG_STRING(rep.strLabel));
        return false;
    }
    return true;
}

/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

std::string ScriptInterpreter::executableDir()
{
#if defined(_WIN32)
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string();

#elif defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe")
               .parent_path().string();

#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    _NSGetExecutablePath(path, &size);
    return std::filesystem::canonical(path).parent_path().string();

#elif defined(__FreeBSD__)
    char path[PATH_MAX];
    size_t len = sizeof(path);
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    sysctl(mib, 4, path, &len, nullptr, 0);
    return std::filesystem::path(path).parent_path().string();

#else
    #error "Unsupported platform"
#endif
}