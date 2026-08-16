#ifndef CH341_SETUP_HPP
#define CH341_SETUP_HPP

#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <regex>


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
  * \brief Check if a string represents a CH341 tty port
  * \param[in] pstrInput string to be evaluated
  * \return true if the string matches the regex, false otherwise
  * \note On Linux the ch341 kernel driver registers tty nodes named
  *       "/dev/ttyCH341USBx" (it shares the generic "ttyUSB"/"ttyACM"
  *       naming scheme on some distros too, so both are accepted).
*/
/*--------------------------------------------------------------------------------------------------------*/

bool isValidCh341Port (const std::string& input)
{
#ifndef _WIN32
    static const std::regex pattern("^/dev/(ttyCH341USB|ttyUSB|ttyACM)(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#else
    static const std::regex pattern("^COM(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#endif
    return std::regex_match(input, pattern);

} /* isValidCh341Port() */


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

#ifdef _WIN32
        bool bHasPrefix = false;

        std::string strPrefix("\\\\.\\");

        // check if it has already the prefix
        bHasPrefix = std::equal(strPrefix.begin(), strPrefix.end(), port.begin());
#endif

        // validate the CH341 port syntax
#ifdef _WIN32
        std::string strPort = ( false == bHasPrefix ) ? port : port.substr(strPrefix.size());
        if (false == isValidCh341Port(strPort) )
#else
        if (false == isValidCh341Port(port) )
#endif
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port syntax:"); LOG_STRING(port));
            break;
        }

        // assign the new value to the port
#ifdef _WIN32 //modify the format in order to support ports with number higher than 9
        std::string strCh341Port = (false == bHasPrefix) ? strPrefix + port : port;
        pOwner->setCh341Port(strCh341Port);
#else
        pOwner->setCh341Port(port);
#endif

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CH341 port changed to:"); LOG_STRING(pOwner->getCh341Port()));

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
        {"b", [pOwner](const std::string& v) -> bool { return pOwner->setCh341Baudrate(v); }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setCh341ReadTimeout(v); }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setCh341WriteTimeout(v); }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setCh341ReadBufferSize(v); }},
        {"raw", [pOwner](const std::string& v) -> bool { return pOwner->setRawResult(v); }},
        {"cached", [pOwner](const std::string& v) -> bool { return pOwner->setCyclicCached(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) continue;

        std::string key = token.substr(0, delimiterPos);
        std::string value = token.substr(delimiterPos + 1);

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
 *  - setCh341Port
 *  - getCh341Port
*/
/*--------------------------------------------------------------------------------------------------------*/


template <typename T>
bool generic_ch341_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty() )
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_ch341_set_params() */


#endif // CH341_SETUP_HPP
