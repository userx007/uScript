#ifndef DSPKI2C_SETUP_HPP
#define DSPKI2C_SETUP_HPP

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

#define LT_HDR     "PLUGSPECOPS |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PUBLIC INTERFACES DEFINITIONS                 //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse and dispatch key:value CONFIG arguments to their owner handlers.
  *
  * Supported keys:
  *   v  – USB VID  (hex, no prefix, e.g. "16C0")
  *   p  – USB PID  (hex, no prefix, e.g. "05DF")
  *   a  – default slave address (hex 7-bit, e.g. "48")
  *   r  – read  timeout [ms]
  *   w  – write timeout [ms]
  *   s  – receive buffer size [bytes]
  *
  * The template type T must expose:
  *   bool setVid(const std::string&)
  *   bool setPid(const std::string&)
  *   bool setSlaveAddr(const std::string&)
  *   bool setReadTimeout(const std::string&)
  *   bool setWriteTimeout(const std::string&)
  *   bool setReadBufferSize(const std::string&)
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    std::unordered_map<std::string, std::function<bool(const std::string&)>> handlers = {
        {"v", [pOwner](const std::string& v) -> bool { return pOwner->setVid(v);           }},
        {"p", [pOwner](const std::string& v) -> bool { return pOwner->setPid(v);           }},
        {"a", [pOwner](const std::string& v) -> bool { return pOwner->setSlaveAddr(v);     }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setReadTimeout(v);   }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setWriteTimeout(v);  }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setReadBufferSize(v);}},
    };

    while (stream >> token) {
        auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) continue;

        std::string key   = token.substr(0, delimiterPos);
        std::string value = token.substr(delimiterPos + 1);

        auto handler = handlers.find(key);
        if (handler != handlers.end()) {
            if (false == handler->second(value)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for key:"); LOG_STRING(key); LOG_STRING(value));
                bRetVal = false;
                break;
            }
        } else {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unknown CONFIG key (ignored):"); LOG_STRING(key));
        }
    }

    return bRetVal;

} /* parseAndCallHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Generic CONFIG handler for the Digispark I2C plugin.
  *
  * Validates that arguments are present and the plugin is enabled before
  * forwarding to parseAndCallHandlers().
  *
  * \param[in] pOwner pointer to the plugin instance
  * \param[in] args   space-separated key:value string from the dispatcher
  * \return true if all handlers succeeded, false otherwise
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

        // if plugin is not enabled, stop execution here and return true
        // as the argument(s) validation passed
        if (false == pOwner->isEnabled())
        {
            bRetVal = true;
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_i2c_set_params() */


#endif // DSPKI2C_SETUP_HPP
