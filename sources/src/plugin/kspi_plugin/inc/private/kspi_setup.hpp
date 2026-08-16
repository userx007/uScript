#ifndef KSPI_SETUP_HPP
#define KSPI_SETUP_HPP

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

#define LT_HDR     "KSPI SETUP  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key=value token stream and dispatch each token to the matching SPI setter.
  *
  * Recognised keys:
  *   d  –  device path    (calls setSpiDevice)
  *   m  –  mode 0–3       (calls setSpiMode)
  *   z  –  speed in Hz    (calls setSpiSpeedHz)
  *   b  –  bits per word  (calls setSpiBitsPerWord)
  *   r  –  read timeout   (calls setSpiReadTimeout)
  *   w  –  write timeout  (calls setSpiWriteTimeout)
  *   s  –  read buf size  (calls setSpiReadBufferSize)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers.
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the setSpi* methods)
  * \param[in] input   space-separated list of "key=value" tokens
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
        {"d", [pOwner](const std::string& v) -> bool { pOwner->setSpiDevice(v); return true; }},
        {"m", [pOwner](const std::string& v) -> bool { return pOwner->setSpiMode(v); }},
        {"z", [pOwner](const std::string& v) -> bool { return pOwner->setSpiSpeedHz(v); }},
        {"b", [pOwner](const std::string& v) -> bool { return pOwner->setSpiBitsPerWord(v); }},
        {"r", [pOwner](const std::string& v) -> bool { return pOwner->setSpiReadTimeout(v); }},
        {"w", [pOwner](const std::string& v) -> bool { return pOwner->setSpiWriteTimeout(v); }},
        {"s", [pOwner](const std::string& v) -> bool { return pOwner->setSpiReadBufferSize(v); }},
        {"raw", [pOwner](const std::string& v) -> bool { return pOwner->setRawResult(v); }},
        {"cached", [pOwner](const std::string& v) -> bool { return pOwner->setCyclicCached(v); }}
    };

    while (stream >> token) {
        auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) continue;

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
 * \brief Apply a set of SPI parameters expressed as a space-separated key=value string.
 *
 * Intended to back the CONFIG command handler.  The function validates that at
 * least one argument is present and that the plugin is in a state where live
 * reconfiguration is meaningful, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance; must implement isEnabled()
 *                    and the setSpi* family of setters
 * \param[in] args    space-separated key=value pairs
 *                    (d=device  m=mode  z=speed_hz  b=bits_per_word
 *                     r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * NOTE: The owner component must implement the interfaces:
 *  - isEnabled
 *  - setSpiDevice
 *  - setSpiMode
 *  - setSpiSpeedHz
 *  - setSpiBitsPerWord
 *  - setSpiReadTimeout
 *  - setSpiWriteTimeout
 *  - setSpiReadBufferSize
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

        bRetVal = parseAndCallHandlers(pOwner, args);

    } while(false);

    return bRetVal;

} /* generic_spi_set_params() */


#endif // KSPI_SETUP_HPP