#ifndef PCAN_SETUP_HPP
#define PCAN_SETUP_HPP

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

#define LT_HDR     "PCAN_SETUP  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching PCAN setter.
  *
  * Recognised keys (compatible with kvcan_setup and slcan_setup key naming conventions):
  *   i  –  PCAN channel handle   (calls setPcanChannel)   e.g. "0x51" = PCAN_USBBUS1
  *   b  –  CAN bitrate in bps    (calls setPcanBitrate)   e.g. "500000"
  *   x  –  TX frame ID           (calls setCanTxId)       e.g. "0x7FF" or "0x18DAF100"
  *   r  –  read timeout (ms)     (calls setCanReadTimeout)
  *   w  –  write timeout (ms)    (calls setCanWriteTimeout)
  *   s  –  read buf size         (calls setCanReadBufferSize)
  *   e  –  force extended IDs    (calls setPcanExtended)  [0=auto, 1=force 29-bit]
  *   f  –  CAN FD mode           (calls setPcanFd)        [0=classic, 1=FD]
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setPcan setCanXxx methods)
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

    // "i" (setPcanChannel) returns void — handled as a special case below so
    // the table itself only needs to hold the uniform bool-returning setters.
    static constexpr Entry table[] = {
        {"b", &T::setPcanBitrate},
        {"x", &T::setCanTxId},
        {"r", &T::setCanReadTimeout},
        {"w", &T::setCanWriteTimeout},
        {"s", &T::setCanReadBufferSize},
        {"e", &T::setPcanExtended},
        {"f", &T::setPcanFd},
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
            pOwner->setPcanChannel(value);
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
 * \brief Apply a set of PCAN parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement the
 *                    setPcanChannel/setPcanBitrate/setCanXxx family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (i:channel  b:bitrate  x:tx_id  r:read_tout  w:write_tout
 *                     s:recv_bufsize  e:extended  f:fd)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - setPcanChannel
 *  - setPcanBitrate
 *  - setPcanExtended
 *  - setPcanFd
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


#endif // PCAN_SETUP_HPP
