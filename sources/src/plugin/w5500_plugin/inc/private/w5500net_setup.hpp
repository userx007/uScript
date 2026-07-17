#ifndef W5500NET_SETUP_HPP
#define W5500NET_SETUP_HPP

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

#define LT_HDR     "W5500NET SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key:value token stream and dispatch each token to the matching W5500Net setter.
  *
  * Recognised keys:
  *   i  –  server IP      (calls setServerIp)
  *   p  –  server port    (calls setServerPort)
  *   r  –  read timeout   (calls setReadTimeout)
  *   w  –  write timeout  (calls setWriteTimeout)
  *   s  –  read buf size  (calls setReadBufferSize)
  *
  * \param[in] pOwner  pointer to the plugin instance
  * \param[in] input   space-separated list of "key:value" tokens
  * \return true if every recognised key was accepted by its setter, false on first failure
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    using Setter = bool (T::*)(const std::string&) const;
    struct Entry { const char *key; Setter setter; };

    static constexpr Entry table[] = {
        {"p", &T::setServerPort,           },
        {"r", &T::setReadTimeout,         },
        {"w", &T::setWriteTimeout,        },
        {"s", &T::setReadBufferSize,      },
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find('='); // CHAR_SEPARATOR_EQUAL is usually '='
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // "i" key calls a void setter — handle separately
        if (key == "i") {
            pOwner->setServerIp(value);
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
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of W5500Net parameters expressed as a space-separated key:value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key:value pairs
 *                    (i:ip  p:port  r:read_tout  w:write_tout  s:bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_w5500net_set_params (const T *pOwner, const std::string &args)
{
    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
        return false;
    }

    return parseAndCallHandlers(pOwner, args);
}

#endif // W5500NET_SETUP_HPP
