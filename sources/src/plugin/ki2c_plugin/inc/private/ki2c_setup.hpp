#ifndef KI2C_SETUP_HPP
#define KI2C_SETUP_HPP

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

#define LT_HDR     "KI2C SETUP  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////\
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching I2C setter.
  *
  * Recognised keys:
  *   d  –  device path   (calls setI2CDevice)
  *   a  –  slave address (calls setI2CAddress)
  *   r  –  read timeout  (calls setI2CReadTimeout)
  *   w  –  write timeout (calls setI2CWriteTimeout)
  *   s  –  read buf size (calls setI2CReadBufferSize)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setI2C* methods)
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
        {"d", [pOwner](const std::string& v) -> bool { pOwner->setI2CDevice(v); return true; }},
        {"a", [pOwner](const std::string& v) -> bool { return pOwner->setI2CAddress(v); }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setI2CReadTimeout(v); }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setI2CWriteTimeout(v); }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setI2CReadBufferSize(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
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
 * \brief Apply a set of I2C parameters expressed as a space-separated key:value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present and that the plugin is in a state where live
 * reconfiguration is meaningful, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement isEnabled()
 *                    and the setI2C* family of setters
 * \param[in] args    space-separated key:value pairs
 *                    (d:device  a:address  r:read_tout  w:write_tout  s:recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setI2CDevice
 *  - setI2CAddress
 *  - setI2CReadTimeout
 *  - setI2CWriteTimeout
 *  - setI2CReadBufferSize
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_i2c_set_params (const T *pOwner, const std::string &args)
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

} /* generic_i2c_set_params() */


#endif // KI2C_SETUP_HPP