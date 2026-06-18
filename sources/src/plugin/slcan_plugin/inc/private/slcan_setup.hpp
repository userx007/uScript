#ifndef SLCAN_SETUP_HPP
#define SLCAN_SETUP_HPP

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

#define LT_HDR     "SLCAN_SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching SLCAN setter.
  *
  * Recognised keys:
  *   i  –  UART device path      (calls setDevice)
  *   p  –  UART baud rate        (calls setUartBaud)
  *   b  –  CAN bit rate preset   (calls setCanBitrate)     [0-13, i.e. S0-SD]
  *   y  –  CAN-FD data rate      (calls setCanFdDataRate)  [1-5,  i.e. Y1-Y5]
  *   m  –  bus mode              (calls setCanMode)        [0=normal,1=silent]
  *   a  –  auto-retransmission   (calls setCanAutoRetx)    [0=off,1=on]
  *   z  –  CAN-FD BRS            (calls setCanFdBrs)       [0=off,1=on]
  *   x  –  TX frame ID           (calls setCanTxId)
  *   r  –  read timeout          (calls setCanReadTimeout)
  *   w  –  write timeout         (calls setCanWriteTimeout)
  *   s  –  read buf size         (calls setCanReadBufferSize)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setDevice/setCan* methods)
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
    struct Entry { const char *key; Setter setter; };

    // "i" (setDevice) returns void — handled as a special case below so the
    // table itself only needs to hold the uniform bool-returning setters.
    static constexpr Entry table[] = {
        {"p", &T::setUartBaud},
        {"b", &T::setCanBitrate},
        {"y", &T::setCanFdDataRate},
        {"m", &T::setCanMode},
        {"a", &T::setCanAutoRetx},
        {"z", &T::setCanFdBrs},
        {"x", &T::setCanTxId},
        {"r", &T::setCanReadTimeout},
        {"w", &T::setCanWriteTimeout},
        {"s", &T::setCanReadBufferSize},
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(':');
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // "i" key calls a void setter — handle separately
        if (key == "i") {
            pOwner->setDevice(value);
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
 * \brief Apply a set of SLCAN parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement the
 *                    setDevice/setCan* family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (i:device  p:uart_baud  b:bitrate  y:fd_rate  m:mode
 *                     a:auto_retx  z:fd_brs  x:tx_id  r:read_tout  w:write_tout  s:recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - setDevice
 *  - setUartBaud
 *  - setCanBitrate
 *  - setCanFdDataRate
 *  - setCanMode
 *  - setCanAutoRetx
 *  - setCanFdBrs
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


#endif // SLCAN_SETUP_HPP
