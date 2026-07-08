#ifndef TCPIP_SETUP_HPP
#define TCPIP_SETUP_HPP

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

#define LT_HDR     "TCPIP SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching TCP setter.
  *
  * Recognised keys:
  *   h  –  remote host      (calls setTcpHost)
  *   p  –  remote port      (calls setTcpPort)
  *   c  –  connect timeout  (calls setConnectTimeout)
  *   r  –  read timeout     (calls setReadTimeout)
  *   w  –  write timeout    (calls setWriteTimeout)
  *   s  –  read buf size    (calls setTcpReadBufferSize)
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
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    // Static table of (key, member-function-pointer) pairs.
    // Built once at program start; zero heap allocation per call.
    using Setter = bool (T::*)(const std::string&) const;
    struct Entry { const char *key; Setter setter; bool voidReturn; };

    // "h" (setTcpHost) returns void — wrap the call so the table stays uniform.
    // We handle it as a special case flagged by voidReturn.
    static constexpr Entry table[] = {
        {"p", &T::setTcpPort,           false},
        {"c", &T::setConnectTimeout,    false},
        {"r", &T::setReadTimeout,       false},
        {"w", &T::setWriteTimeout,      false},
        {"s", &T::setTcpReadBufferSize, false},
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(':');
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // "h" key calls a void setter — handle separately
        if (key == "h") {
            pOwner->setTcpHost(value);
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

} /* parseAndCallHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of TCP parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present and that the plugin is in a state where live
 * reconfiguration is meaningful, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement isEnabled()
 *                    and the setTcp family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (h:host  p:port  c:connect_tout  r:read_tout  w:write_tout  s:recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setTcpHost
 *  - setTcpPort
 *  - setConnectTimeout
 *  - setReadTimeout
 *  - setWriteTimeout
 *  - setTcpReadBufferSize
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_tcp_set_params (const T *pOwner, const std::string &args)
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

} /* generic_tcp_set_params() */


#endif // TCPIP_SETUP_HPP
