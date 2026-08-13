#ifndef U_PLUGIN_SETUP_HPP
#define U_PLUGIN_SETUP_HPP

#include "uSharedConfig.hpp"
#include "uLogger.hpp"

#include <cstddef>
#include <sstream>
#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

/**
  * \brief Shared CONFIG-string parser for the network-client plugins
  *        (LAN8720NET, W5500NET, ENC28J60NET, TCPIP, UDP, ...).
  *
  *        All of these plugins expose the same CONFIG grammar - a space
  *        separated list of "key=value" tokens - but not the same set of
  *        keys or setter names (e.g. TCPIP/UDP add a "c" connect-timeout
  *        key and call setTcpHost()/setUdpHost() instead of setServerIp()).
  *        Rather than hard-coding one fixed set of keys, each plugin's
  *        *_setup.hpp supplies its own small table of {key, setter} pairs
  *        (see KVSetterEntry) and this header does the generic work:
  *        tokenizing, splitting on '=', looking the key up in the table,
  *        and calling the matching setter - once, instead of once per
  *        plugin.
*/

///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////

/**
  * \brief One CONFIG key's binding to a setter on plugin type T.
  *
  *        Exactly one of boolSetter / voidSetter should be non-null:
  *          - boolSetter: validated setter (e.g. setServerPort) - a false
  *            return aborts the parse and fails the CONFIG command.
  *          - voidSetter: unconditional setter (e.g. setServerIp/setTcpHost) -
  *            plain assignment, nothing to validate, always accepted.
*/
template <typename T>
struct KVSetterEntry
{
    const char *key;
    bool (T::*boolSetter)(const std::string&) const = nullptr;
    void (T::*voidSetter)(const std::string&) const = nullptr;
};

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a "key=value" token stream and dispatch each token to the matching entry in table.
  *
  * Malformed tokens (no '=', or an empty key) and keys not present in table
  * are logged as warnings rather than being silently swallowed, so a typo
  * in a CONFIG command is visible in the log instead of just quietly not
  * taking effect. A value of the form "$name" or "$name.SIZE" is accepted
  * without being parsed/range-checked at all — see the "$" guard below for
  * why (in short: during script validation the real value isn't known yet).
  *
  * \param[in] pOwner     pointer to the plugin instance
  * \param[in] input      space-separated list of "key=value" tokens
  * \param[in] table      plugin-supplied array of recognised {key, setter} entries
  * \param[in] pszLogHdr  log header literal used to prefix warnings, e.g. "TCPIP SETUP |"
  *
  * \return true if every recognised key was accepted by its setter, false on the first setter failure
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T, std::size_t N>
bool parseAndCallSetupHandlers(const T *pOwner, const std::string& input,
                                const KVSetterEntry<T> (&table)[N], const char *pszLogHdr)
{
    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos || delimiterPos == 0) {
            LOG_PRINT(LOG_WARNING, LOG_STRING(pszLogHdr); LOG_STRING("Ignoring malformed token:"); LOG_STRING(token));
            continue;
        }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // A value that still starts with '$' is an unexpanded "$macroname"
        // (or "$macroname.SIZE") reference — this call is happening during
        // script VALIDATION (a dry run), before the referenced variable
        // macro has a real value yet. Real execution always resolves every
        // $macro (ScriptInterpreter::m_replaceVariableMacros()) before the
        // plugin ever sees strParams — see ScriptInterpreter::m_executeCommand()'s
        // real-exec vs. dry-run branches — so this text can only appear here
        // during the dry run. Accept the key and defer the actual
        // value/range check to real execution, when this setter will see
        // the already-resolved literal instead of "$...".
        if (!value.empty() && value[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_STRING(pszLogHdr); LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(value);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        bool bMatched = false;
        for (const auto& entry : table) {
            if (key == entry.key) {
                bMatched = true;
                if (entry.voidSetter != nullptr) {
                    (pOwner->*entry.voidSetter)(value);
                } else if (entry.boolSetter != nullptr) {
                    if (false == (pOwner->*entry.boolSetter)(value)) {
                        bRetVal = false;
                    }
                }
                break;
            }
        }

        if (!bMatched) {
            LOG_PRINT(LOG_WARNING, LOG_STRING(pszLogHdr); LOG_STRING("Unrecognized key:"); LOG_STRING(key));
        }

        if (!bRetVal) { break; }
    }
    return bRetVal;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Apply a set of "key=value" parameters to a network-client plugin's CONFIG setters.
  *
  * \param[in] pOwner     pointer to the plugin instance
  * \param[in] args       space-separated key=value pairs, see table for the recognised keys
  * \param[in] table      plugin-supplied array of recognised {key, setter} entries
  * \param[in] pszLogHdr  log header literal used to prefix warnings
  *
  * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T, std::size_t N>
bool generic_setup_params (const T *pOwner, const std::string &args,
                                 const KVSetterEntry<T> (&table)[N], const char *pszLogHdr)
{
    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_STRING(pszLogHdr); LOG_STRING("Missing args"));
        return false;
    }

    return parseAndCallSetupHandlers(pOwner, args, table, pszLogHdr);
}

#endif // U_PLUGIN_SETUP_HPP
