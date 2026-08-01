#ifndef RAWETH_SETUP_HPP
#define RAWETH_SETUP_HPP

#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <sstream>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "RAWETH SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching RawEth setter.
  *
  * Recognised keys:
  *   i  –  interface name    (calls setIface)
  *   d  –  destination MAC   (calls setDestMac)
  *   t  –  EtherType         (calls setEtherType)
  *   x  –  promiscuous mode  (calls setPromiscuous)
  *   r  –  read timeout      (calls setReadTimeout)
  *   w  –  write timeout     (calls setWriteTimeout)
  *   s  –  read buf size     (calls setRawEthReadBufferSize)
  *
  * Unlike parseAndCallHandlers() in tcpip_setup.hpp, every RawEth setter
  * returns bool (including setIface — interface-name validation benefits
  * from a reportable failure the same way host/port validation does), so
  * the dispatch table needs no void-returning special case here.
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance
  * \param[in] input   space-separated list of "key:value" tokens
  * \return true if every recognised key was accepted by its setter, false on first failure
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallRawEthHandlers(const T *pOwner, const std::string& input)
{
    // Static table of (key, member-function-pointer) pairs.
    // Built once at program start; zero heap allocation per call.
    using Setter = bool (T::*)(const std::string&) const;
    struct Entry { const char *key; Setter setter; };

    static constexpr Entry table[] = {
        {"i", &T::setIface},
        {"d", &T::setDestMac},
        {"t", &T::setEtherType},
        {"x", &T::setPromiscuous},
        {"r", &T::setReadTimeout},
        {"w", &T::setWriteTimeout},
        {"s", &T::setRawEthReadBufferSize},
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // A value that still starts with '$' is an unexpanded "$macroname"
        // (or "$macroname.SIZE") reference — this call is happening during
        // script VALIDATION (a dry run), before the referenced variable
        // macro has a real value yet. Real execution always resolves every
        // $macro before the plugin ever sees the string (see
        // ScriptInterpreter::m_executeCommand()'s real-exec vs. dry-run
        // branches). Accept the key and defer the actual value/range check
        // to real execution.
        if (!value.empty() && value[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(value);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        for (const auto& entry : table) {
            if (key == entry.key) {
                if (false == (pOwner->*entry.setter)(value)) {
                    bRetVal = false;
                }
                break;
            }
        }

        if (!bRetVal) { break; }
    }
    return bRetVal;

} /* parseAndCallRawEthHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of RawEth parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler. The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallRawEthHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement the setRawEth
 *                    family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (i:iface  d:dest_mac  t:ethertype  x:promiscuous  r:read_tout  w:write_tout  s:recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setIface
 *  - setDestMac
 *  - setEtherType
 *  - setPromiscuous
 *  - setReadTimeout
 *  - setWriteTimeout
 *  - setRawEthReadBufferSize
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_raweth_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty())
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        bRetVal = parseAndCallRawEthHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_raweth_set_params() */


#endif // RAWETH_SETUP_HPP
