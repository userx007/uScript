#ifndef LAN8720NET_SETUP_HPP
#define LAN8720NET_SETUP_HPP

#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <sstream>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "LAN8720NET SETUP |"
#define LOG_HDR  LOG_STRING(LT_HDR)

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
        const auto delimiterPos = token.find('=');
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

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

template <typename T>
bool generic_lan8720net_set_params (const T *pOwner, const std::string &args)
{
    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
        return false;
    }

    return parseAndCallHandlers(pOwner, args);
}

#endif // LAN8720NET_SETUP_HPP
