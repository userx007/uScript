#ifndef PLUGIN_SPEC_OPERATIONS_HPP
#define PLUGIN_SPEC_OPERATIONS_HPP

#include "CommonSettings.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <sstream>

///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#undef  LOG_HDR
#define LOG_HDR     LOG_STRING("PLUGSPECOPS:");


///////////////////////////////////////////////////////////////////
//                 PUBLIC INTERFACES DEFINITIONS                 //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Check if a string represents an UART port
  * \param[in] pstrInput string to be evaluated
  * \return true if the string matches the regex, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool isValidUartPort (const std::string& input)
{
#ifndef _WIN32
    static const std::regex pattern("^/dev/(tnt|ttyACM|ttyUSB)(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#else
    static const std::regex pattern("^COM(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#endif
    return std::regex_match(input, pattern);

} /* isValidUartPort() */


/*--------------------------------------------------------------------------------------------------------*/
/**
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool handlePort (const T *pOwner, const std::string &port)
{
    bool bRetVal = false;

    do {

        // no new port provided, keep the old one
        if (true == port.empty() )
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing port"));
            bRetVal = false;
            break;
        }

        bool bHasPrefix = false;

#ifdef _WIN32
        std::string strPrefix("\\\\.\\");

        // check if it has already the prefix
        bHasPrefix = std::equal(strPrefix.begin(), strPrefix.end(), port.begin());
#endif

        // validate the UART port syntax
#ifdef _WIN32
        std::string strPort = ( false == bHasPrefix ) ? port : port.substr(strPrefix.size());
        if (false == isValidUartPort(strPort) )
#else
        if (false == isValidUartPort(port) )
#endif
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port syntax:"); LOG_STRING(port));
            break;
        }

        // assign the new value to the port
#ifdef _WIN32 //modify the format in order to support ports with number higher than 9
        std::string strUartPort = (false == bHasPrefix) ? strPrefix + port : port;
        pOwner->setUartPort(strUartPort);
#else
        pOwner->setUartPort(port);
#endif

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("UART port changed to:"); LOG_STRING(pOwner->getUartPort()));

        bRetVal = true;

    } while(false);

    return bRetVal;

} /* handlePort() */


/*--------------------------------------------------------------------------------------------------------*/
/**
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    std::unordered_map<std::string, std::function<bool(const std::string&)>> handlers = {
        {"p", [pOwner](const std::string& v) -> bool { return handlePort<T>(pOwner, v); }},
        {"b", [pOwner](const std::string& v) -> bool { return pOwner->setUartBaudrate(v); }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setUartReadTimeout(v); }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setUartWriteTimeout(v); }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setUartReadBufferSize(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(':');
        if (delimiterPos == std::string::npos) continue;

        std::string key = token.substr(0, delimiterPos);
        std::string value = token.substr(delimiterPos + 1);

        auto handler = handlers.find(key);
        if (handler != handlers.end()) {
            if(false == handler->second(value)) {
                bRetVal = false;
                break;
            }
        }
    }
    return bRetVal;

} /* parseAndCallHandlers() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief generic function used to change port in plugins
 * \param[in] pOwner pointer to the template type used to access the class private members
 * \param[in] args string containing the arguments list as space separated string
 * \return true if processing succeeded, false otherwise
 * NOTE: The user component must implement interfaces :
 *  - setUartPort
 *  - getUartPort
*/
/*--------------------------------------------------------------------------------------------------------*/


template <typename T>
bool generic_uart_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty() )
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == pOwner->isEnabled() )
        {
            bRetVal = true;
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_uart_set_params() */


#endif // PLUGIN_SPEC_OPERATIONS_HPP