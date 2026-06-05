#ifndef KVCAN_SETUP_HPP
#define KVCAN_SETUP_HPP

#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <unordered_map>
#include <functional>
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

#define LT_HDR     "KVCAN SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching CAN setter.
  *
  * Recognised keys:
  *   i  –  interface name  (calls setCanIface)
  *   x  –  TX frame ID     (calls setCanTxId)
  *   r  –  read timeout    (calls setCanReadTimeout)
  *   w  –  write timeout   (calls setCanWriteTimeout)
  *   s  –  read buf size   (calls setCanReadBufferSize)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setCan* methods)
  * \param[in] input   space-separated list of "key:value" tokens
  * \return true if every recognised key was accepted by its setter, false on first failure
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    std::unordered_map<std::string, std::function<bool(const std::string&)>> handlers = {
        {"i", [pOwner](const std::string& v) -> bool { pOwner->setCanIface(v); return true; }},
        {"x", [pOwner](const std::string& v) -> bool { return pOwner->setCanTxId(v); }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setCanReadTimeout(v); }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setCanWriteTimeout(v); }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setCanReadBufferSize(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(':');
        if (delimiterPos == std::string::npos) continue;

        std::string key   = token.substr(0, delimiterPos);
        std::string value = token.substr(delimiterPos + 1);

        auto handler = handlers.find(key);
        if (handler != handlers.end()) {
            if (false == handler->second(value)) {
                bRetVal = false;
                break;
            }
        }
    }
    return bRetVal;

} /* parseAndCallHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CAN parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present and that the plugin is in a state where live
 * reconfiguration is meaningful, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement isEnabled()
 *                    and the setCan* family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (i:iface  x:tx_id  r:read_tout  w:write_tout  s:recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setCanIface
 *  - setCanTxId
 *  - setCanReadTimeout
 *  - setCanWriteTimeout
 *  - setCanReadBufferSize
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_can_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty())
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_can_set_params() */


#endif // KVCAN_SETUP_HPP