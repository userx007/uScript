#ifndef FT245_SETUP_HPP
#define FT245_SETUP_HPP

#include "uSharedConfig.hpp"
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

#define LT_HDR     "FT245 SETUP |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PRIVATE INTERFACES DEFINITIONS                //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a key=value token stream and dispatch each token to the matching FT245 setter.
  *
  * Recognised keys:
  *   x    –  u8DeviceIndex        (calls setDeviceIndex)
  *   v    –  eDefaultVariant      (calls setDefaultVariant)
  *   fm   –  eDefaultFifoMode     (calls setDefaultFifoMode)
  *   r    –  u32ReadTimeout       (calls setReadTimeout)
  *   sd   –  u32ScriptDelay       (calls setScriptDelay)
  *
  * Unknown keys are silently skipped so that callers adding future keys stay
  * forward-compatible with older setup headers. A value of the form "$name"
  * or "$name.SIZE" is likewise accepted without being parsed/range-checked —
  * see the "$" guard below for why (in short: during script validation the
  * real value isn't known yet).
  *
  * \param[in] pOwner  pointer to the plugin instance (provides the set* methods)
  * \param[in] input   space-separated list of "key=value" tokens
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

    static constexpr Entry table[] = {
        {"x", &T::setDeviceIndex},
        {"v", &T::setDefaultVariant},
        {"fm", &T::setDefaultFifoMode},
        {"r", &T::setReadTimeout},
        {"sd", &T::setScriptDelay},
    };

    std::istringstream stream(input);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        const auto delimiterPos = token.find(CHAR_SEPARATOR_EQUAL);
        if (delimiterPos == std::string::npos) { continue; }

        const std::string key   = token.substr(0, delimiterPos);
        const std::string value = token.substr(delimiterPos + 1);

        // A value that still starts with '$' is an unexpanded "$macroname"
        // (or "$macroname.SIZE") reference — this call is happening during
        // script VALIDATION (a dry run), before the referenced variable
        // macro has a real value yet. Accept the key and defer the actual
        // value/range check to real execution.
        if (!value.empty() && value[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("="); LOG_STRING(value);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
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
 * \brief Apply a set of FT245 parameters expressed as a space-separated key=value string.
 *
 * Intended to back the CONFIG command handler. The function validates that at
 * least one argument is present, then delegates token parsing to
 * parseAndCallHandlers().
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_ft245_set_params (const T *pOwner, const std::string &args)
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

} /* generic_ft245_set_params() */


#endif // FT245_SETUP_HPP
