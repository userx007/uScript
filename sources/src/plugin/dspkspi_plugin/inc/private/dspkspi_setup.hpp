#ifndef DSPKSPI_SETUP_HPP
#define DSPKSPI_SETUP_HPP

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
  * \brief Parse the CONFIG argument string and call the appropriate setter on pOwner.
  *
  * Recognised keys (space-separated  key=value  tokens):
  *   vid=<hex>   – USB Vendor ID  (e.g. vid=16C0)
  *   pid=<hex>   – USB Product ID (e.g. pid=05DF)
  *   m=<0-3>     – SPI mode       (CPOL/CPHA, default 0)
  *   d=<0-3>     – Clock divider  (0=Div2 1=Div4 2=Div8 3=Div16, default 1)
  *   r=<ms>      – Read  timeout  [ms]
  *   w=<ms>      – Write timeout  [ms]
  *   s=<bytes>   – Read buffer size [bytes]
  *
  * \param[in] pOwner  Plugin instance that owns the setter methods
  * \param[in] input   Raw argument string from the CONFIG command
  * \return true if all recognised tokens were applied successfully
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool parseAndCallHandlers(const T *pOwner, const std::string& input)
{
    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    std::unordered_map<std::string, std::function<bool(const std::string&)>> handlers = {
        {"vid", [pOwner](const std::string& v) -> bool { return pOwner->setSpiVid(v);           }},
        {"pid", [pOwner](const std::string& v) -> bool { return pOwner->setSpiPid(v);           }},
        {"m",   [pOwner](const std::string& v) -> bool { return pOwner->setSpiMode(v);          }},
        {"d",   [pOwner](const std::string& v) -> bool { return pOwner->setSpiClockDiv(v);      }},
        {"r",   [pOwner](const std::string& v) -> bool { return pOwner->setSpiReadTimeout(v);   }},
        {"w",   [pOwner](const std::string& v) -> bool { return pOwner->setSpiWriteTimeout(v);  }},
        {"s",   [pOwner](const std::string& v) -> bool { return pOwner->setSpiReadBufferSize(v);}},
        {"raw", [pOwner](const std::string& v) -> bool { return pOwner->setRawResult(v); }},
        {"cached", [pOwner](const std::string& v) -> bool { return pOwner->setCyclicCached(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) { continue; }

        std::string key   = token.substr(0, delimiterPos);
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
  * \brief Generic CONFIG command helper for the SPI plugin.
  *
  * Validates that args are non-empty, short-circuits when the plugin is
  * not yet enabled (argument-validation-only mode), then delegates to
  * parseAndCallHandlers().
  *
  * \param[in] pOwner  Plugin instance
  * \param[in] args    Raw argument string
  * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_spi_set_params (const T *pOwner, const std::string &args)
{
    bool bRetVal = false;

    do {

        // no args provided
        if (true == args.empty())
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Missing args"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == pOwner->isEnabled())
        {
            bRetVal = true;
            break;
        }

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_spi_set_params() */


#endif // DSPKSPI_SETUP_HPP
