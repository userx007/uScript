#include "CommonSettings.hpp"
#include "PluginSpecOperations.hpp"

#include "utils_plugin.hpp"

#include "uTimer.hpp"
#include "uString.hpp"
#include "uNumeric.hpp"
#include "uEvaluator.hpp"
#include "uCheckContinue.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "UTILSPLUGIN:"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define COM_PORT    "COM_PORT"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
extern "C" 
{
    EXPORTED UtilsPlugin* pluginEntry()
    {
        return new UtilsPlugin();
    }

    EXPORTED void pluginExit( UtilsPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool UtilsPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;

    return m_bIsInitialized;

}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void UtilsPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;

}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       UTILS.INFO
  *
  * \param[in] args NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_INFO (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s). Abort!"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: helper commands"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("BREAKPOINT : stop execution and wait for the user decision continue/abort"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [message]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.BREAKPOINT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.BREAKPOINT message"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("DELAY : introduce a delay in script execution"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : delay"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.DELAY 2000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("EVALUATE : evaluate the given expression"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : op1, op2 -vector of numbers or strings or $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       rule numbers: < <= == != >= > "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       rule strings case sensitive: EQ NE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       rule strings case insensitive: eq ne"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RESULT ?= UTILS.EVALUATE \"1 2 3 4\" == \"1 2 3 4\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RESULT ?= UTILS.EVALUATE $MACRO1 =! $MACRO2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RESULT ?= UTILS.EVALUATE $MACRO1 EQ \"TRUE\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : TRUE or FALSE (as string)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("EVALUATE_BOOL_ARRAY : evaluate an array of boolean values"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : op1 op2 .. opN | rule"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       op1 op2 .. opN : vector of booleans or $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       rule: AND, OR"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RESULT ?= UTILS.EVALUATE_BOOL_ARRAY TRUE !TRUE FALSE !FALSE 0 1 !1 !0 | AND"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RESULT ?= UTILS.EVALUATE_BOOL_ARRAY $M1 $M2 $M3 | OR"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : TRUE or FALSE (as string)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("FAIL : force the script to fail [always or if the condition is true]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [|condition]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.FAIL"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.FAIL TRUE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.FAIL $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : condition can be TRUE FALSE !TRUE !FALSE 0 1 !0 !1 $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("FORMAT : extract and re-format the items from a \"vector of strings\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : input_string | format_rule"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       format_rule : combination of strings and indexes of substrings (0..N-1) in the input string "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: $RETVAL ?= UTILS.FORMAT 123 11 22 33 44 | \"0x%1 0x%2\" == \"0x%3 0x%4\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       results in: \"0x11 0x22\" == \"0x33 0x44\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : a string with the result)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("MATH : performs basic math operation between 2 vectors of numbers or $MACRONAMES "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : op1 rule op2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       rule: + - * / % & | ^ << >> += -= *= /= %= &= |= ^= <<= >>="));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RESULT ?= UTILS.MATH \"1 2 3\" + \"5 6 7\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       will result in: \"6 8 10\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("MESSAGE : prints a message"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : message"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.MESSAGE Please switch Power ON"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("PRINT : (conditionally) print a message / value of a macro"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : message [|condition]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.PRINT $RETVAL"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT This is the message"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT This is the message | TRUE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $MACRONAME  | !FALSE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $MACRONAME1 | $MACRONAME2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : condition can be TRUE FALSE !TRUE !FALSE 0 1 !0 !1 $MACRONAME"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("RETURN : write a value to a volatile macro"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : string"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RETVAL ?= UTILS.RETURN \"11 22 33\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RETVAL ?= UTILS.RETURN \"HELLO\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RETVAL ?= UTILS.RETURN HELLO"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : If the value contains spaces it has to be bordered \"\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("VALIDATE : compare two values based on the rule"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : item1 rule item2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.VALIDATE $VAL1 == $VAL2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.VALIDATE $VERS1 > $VERS2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.VALIDATE $STR1 EQ $STR2"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : number/version rules: < <= == != > >="));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       string rules: EQ NE eq ne"));


        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief DELAY command implementation; can be used to introduce a
  *        delay (in ms) between execution of the other commands.
  *
  * \note Usage example: <br>
  *       UTILS.DELAY 2000
  *
  * \param[in] args value of the delay in miliseconds
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_DELAY (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expecting arguments, fails if not provided
        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing: delay(ms)"));
            break;
        }

        // fail if more than one space separated arguments is provided ...
        if (true == ustring::containsChar(args, CHAR_SEPARATOR_SPACE))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: delay"));
            break;
        }

        uint32_t u32Delay = 0;

        // convert string to integer
        if (false == numeric::str2uint32( args, u32Delay))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value:"); LOG_STRING(args));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // skip if the requested delay is 0 otherwise sleep
        if (0 != u32Delay)
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Start sleep("); LOG_UINT32(u32Delay); LOG_STRING("ms)"));
            utime::delay_ms(u32Delay);
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("End of sleep("); LOG_UINT32(u32Delay); LOG_STRING("ms)"));
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief MESSAGE command implementation; print a message at the console
  *
  * \note Usage example: <br>
  *       UTILS.MESSAGE Switch Power ON
  *
  * \param[in] args message to be printed at console
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_MESSAGE (const std::string &args) const
{
    return m_GenericMessageHandling (args, false);
}


/**
  * \brief BREAKPOINT command implementation; print a message at console
  *        and stops execution waiting for the user to press a key to continue
  *        Esc key is used to abort the execution, any other key to continue
  *
  * \note Usage example: <br>
  *       UTILS.BREAKPOINT Switch Power OFF
  *
  * \param[in] args string describing the purpose of the breakpoint
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_BREAKPOINT (const std::string &args) const
{
    return m_GenericMessageHandling( args, true);
}


/**
  * \brief PRINT command implementation; print a message at the console (optionally conditioned)
  *        Can be useful to print macros values or other ordinary strings
  *
  * \note Usage example: <br>
  *       UTILS.PRINT $MACRONAME
  *       UTILS.PRINT This is the message
  *       UTILS.PRINT This is the message | TRUE    # (TRUE FALSE !TRUE !FALSE 0 1 !0 !1)
  *       UTILS.PRINT $MACRONAME  | !FALSE             # (TRUE FALSE !TRUE !FALSE 0 1 !0 !1)
  *       UTILS.PRINT $MACRONAME1 | $MACRONAME2
  *
  * \param[in] args string or macro (a macro is a string too)
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_PRINT (const std::string &args) const
{
    bool bRetVal = false;
    const std::string strCmdFormat = "message [| condition]";
    const std::string strNone  = "<none>";
    const std::string strEmpty = "<empty>";
    const std::string strCondition = "[!] TRUE FALSE 1 0 $MACRONAME";

    do {

        // missing args or empty string are handled immediatelly
        size_t szInputLen = 0;
        if (true == args.empty())
        {
            if (true == m_bIsEnabled)
            {
                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(strEmpty));
            }
            bRetVal = true;
            break;
        }

        // condition is mandatory if the vertical bar is provided
        if (true == ustring::endsWithChar(args, CHAR_SEPARATOR_VERTICAL_BAR))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing condition after |, use:"); LOG_STRING(strCondition));
            break;
        }

        // split the string into message/condition
        std::string strMessage;
        std::string strCondition;
        ustring::splitReverseAtChar(args, strMessage, strCondition, CHAR_SEPARATOR_VERTICAL_BAR);

        bool bExecute = true;

        // evaluate the condition if provided
        if (false == strCondition.empty())
        {
            // (get the value of the condition) volatile macro is excepted during the validation but not during the execution
            if (((false == m_bIsEnabled) && (false == eval::string2bool(strCondition, bExecute)) && (false == ustring::isValidMacroUsage(strCondition))) ||
                ((true  == m_bIsEnabled) && (false == eval::string2bool(strCondition, bExecute))))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected condition:"); LOG_STRING(strCondition));
                break;
            }
        }

        // if plugin is not enabled, stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // the message shouldn't be printed due to the false condition
        if (false == bExecute)
        {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("<print skipped @condition>"));
            bRetVal = true;
            break;
        }

        // print the message
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING( (true == strMessage.empty()) ? strEmpty : strMessage));

        // everything OK so far
        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief RETURN command implementation; returns a value (string)
  *        Intended to be used as simulator of values returned to macros
  *
  * \note Usage example: <br>
  *       UTILS.RETURN AABBCC:112233:Hello:11
  *
  * \param[in] args string to be returned to a macro
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_RETURN (const std::string &args) const
{

    // execute it only if the plugin is enabled
    if (true == m_bIsEnabled) {
        if (false == args.empty()) {
            m_strResultData.assign(args);
        }
    }

    return true;
}


/**
  * \brief Compare two values as strings or numbers based on the provided rule
  *
  * \note Usage example: <br>
  *       UTILS.VALIDATE "1 2 3 4" == "1 2 3 4"
  *
  * \param[in] vector1 rule vector2
  *
  * \return true if the validation pass, false otherwise
*/

bool UtilsPlugin::m_Utils_VALIDATE (const std::string &args) const
{
    bool bEvalResult = false;
    return m_EvaluateExpression(args, bEvalResult) && bEvalResult;
}


/**
  * \brief Evaluate the expression provided as argument
  *
  * \note Usage example: <br>
  *      RESULT ?= UTILS.EVALUATE "1 2 3 4" == "1 2 3 4"
  *
  * \param[in] vector1 rule vector2
  *
  * \return true if the execution succeeded, false otherwise
  * \note on success the m_strResultData is set to to either "TRUE" or "FALSE" depending of the evaluation result
*/

bool UtilsPlugin::m_Utils_EVALUATE (const std::string &args) const
{
    bool bRetVal = false;
    bool bEvalResult = false;

    // return the execution status not the evaluation result (returned in m_strResultData)
    if ((true == (bRetVal = m_EvaluateExpression(args, bEvalResult))) && (true == m_bIsEnabled))
    {
        // perform the validation and return the result as "true" or "false" string
        m_strResultData.assign( (true == bEvalResult) ? "TRUE" : "FALSE");
    }

    return bRetVal;
}


/**
  * \brief Evaluate the expression provided as argument
  *
  * \note Usage example: <br>
  *      RESULT1 ?= UTILS.EVALUATE_BOOL_ARRAY 1 0 1 TRUE FALSE 0 1 | OR
  *      RESULT2 ?= UTILS.EVALUATE_BOOL_ARRAY TRUE FALSE 1 0 | AND
  *
  * \param[in] vector | rule
  *
  * \return true if the execution succeeded, false otherwise
  * \note on success the m_strResultData is set to to either "TRUE" or "FALSE" depending of the evaluation result
*/

bool UtilsPlugin::m_Utils_EVALUATE_BOOL_ARRAY (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // no arguments are expected
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing: array | rule"));
            break;
        }

        // extract arguments
        std::vector<std::string> vstrArgs;
        ustring::splitAtFirst(args, CHAR_SEPARATOR_VERTICAL_BAR, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // check the expected number of arguments
        if (2 != szNrArgs) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 2 args, array | rule, got:"); LOG_UINT32((uint32_t)szNrArgs));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        bool bEvalResult = false;
        /*                                       |  array    |  rule      |               */
        if (false == eval::validateVectorBooleans(vstrArgs[0], vstrArgs[1], bEvalResult)) {
            break;
        }

        m_strResultData.assign(true == bEvalResult ? "TRUE" : "FALSE");
        bRetVal = true;

    } while(false);

    return bRetVal;
}


/**
  * \brief Perform math operation between 2 vectors of uint32 and place the result into a vector of uint64 converted to strings
  *
  * \note Usage example: <br>
  *      RESULT ?= UTILS.MATH "1 2 3 4" + "1 2 3 4"
  *
  * \param[in] vector1 rule vector2
  *
  * \return true if the execution succeeded, false otherwise
  * \note on success the m_strResultData is set to to either "true" or "false" depending of the evaluation result
*/

bool UtilsPlugin::m_Utils_MATH (const std::string &args) const
{
   bool bRetVal = false;
   const std::string strCmdFormat = "V1/$M1 rule V2/$M2 or $M [| HEX]";
   const std::string strOption    = "HEX";
   bool bHexResult = false;

    do {

        // arguments are expected
        size_t szInputLen = 0;
        if (args.empty() || ustring::startsWithChar(args, CHAR_SEPARATOR_VERTICAL_BAR))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing args:"); LOG_STRING(strCmdFormat));
            break;
        }

        // extract arguments
        std::vector<std::string> vstrArgs;
        ustring::tokenize(args, CHAR_SEPARATOR_VERTICAL_BAR, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // check for the option
        if (2 == szNrArgs)
        {
            if (strOption != vstrArgs[1])
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid option:"); LOG_STRING(vstrArgs[1]); LOG_STRING("| Expected:"); LOG_STRING(strOption));
                break;
            }
            bHexResult = true;
        }

        // extract arguments
        std::vector<std::string> vstrArgsData;
        ustring::tokenizeSpaceQuotesAware(vstrArgs[0], vstrArgsData);
        size_t szNrArgsData = vstrArgsData.size();

        // check if called with macro as parameter
        if (1 == szNrArgsData)
        {
            if (false == ustring::isValidMacroUsage(vstrArgsData[0]))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid args:"); LOG_STRING(args); LOG_STRING("| Use:"); LOG_STRING(strCmdFormat));
                break;
            }

            // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
            if (false == m_bIsEnabled)
            {
                bRetVal = true;
                break;
            }
        }

        // not a compact macro then expect the normal format val1 rule val2
        if (3 != szNrArgsData)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid args:"); LOG_STRING(args); LOG_STRING("| Use:"); LOG_STRING(strCmdFormat));
            break;
        }

        // check the math rule
        if (false == eval::isMathOperator(vstrArgsData[1]))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid rule:"); LOG_STRING(vstrArgsData[1]));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        std::vector<std::string>vstrLeft;
        std::vector<std::string>vstrRight;
        std::vector<std::string>vstrResult;

        ustring::tokenize(vstrArgsData[0], CHAR_SEPARATOR_SPACE, vstrLeft);
        ustring::tokenize(vstrArgsData[2], CHAR_SEPARATOR_SPACE, vstrRight);

        if (true == m_math.mathInteger(vstrLeft, vstrRight, vstrArgsData[1], vstrResult, bHexResult)) {
            m_strResultData = ustring::joinStrings(vstrResult, CHAR_SEPARATOR_SPACE);
            bRetVal = true;
        }

    } while(false);

    return bRetVal;
}


/**
  * \brief Extract items from a "vector of strings" based on a "vector of indexes"
  *
  * \note Usage example: <br>
  *       $RETVAL ?= UTILS.FORMAT "AA BB CC DD" | "0x%0 0x%1" == "0x%2 0x%3"
  *
  * \param[in] string indexes
  *
  * \return true if succeeded, false otherwise
*/

bool UtilsPlugin::m_Utils_FORMAT(const std::string& args) const
{
    // Bail out early if args is empty
    if (args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing: string | indexes"));
        return false;
    }

    // Split input string at first '|'
    std::vector<std::string> vstrArgs;
    ustring::splitAtFirst(args, CHAR_SEPARATOR_VERTICAL_BAR, vstrArgs);

    if (vstrArgs.size() != 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 2 args: string | indexes, got:");
                  LOG_UINT32(static_cast<uint32_t>(vstrArgs.size())));
        return false;
    }

    // Skip actual formatting if plugin is disabled
    if (false == m_bIsEnabled) {
        return true;
    }

    // Trim decorations and tokenize items
    ustring::undecorate(vstrArgs[0]);
    std::vector<std::string> vstrItems;
    ustring::tokenize(vstrArgs[0], CHAR_SEPARATOR_SPACE, vstrItems);
    const size_t szNrItems = vstrItems.size();

    const std::string& strFormat = vstrArgs[1];
    std::string strTemp;

    for (size_t i = 0; i < strFormat.size(); ++i)
    {
        char c = strFormat[i];

        if (c == '%')
        {
            if (i + 1 >= strFormat.size())
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format: missing last index"));
                return false;
            }

            char cIndex = strFormat[++i]; // move to next char
            uint16_t uiIndex = numeric::ascii2val(cIndex);

            if (uiIndex >= szNrItems)
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Invalid format: index missing/wrong/out of range:");
                          LOG_UINT32(uiIndex);
                          LOG_STRING(">");
                          LOG_UINT32(static_cast<uint32_t>(szNrItems - 1)));
                return false;
            }

            strTemp += vstrItems[uiIndex];
        }
        else
        {
            strTemp += c;
        }
    }

    m_strResultData = std::move(strTemp);
    return true;
}



/**
  * \brief Force the failure of the script
  *
  * \note Usage example: <br>
  *       UTILS.FAIL
  *
  * \param[in] none
  *
  * \return always false
*/

bool UtilsPlugin::m_Utils_FAIL (const std::string &args) const
{
    bool bRetVal = false;
    const std::string strCmdFormat = "| condition";

    do {

        // if no condition was provided then implicitelly fails
        if (args.empty()) {
            break;
        }

        if (false == ustring::isConditionFormat(args)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong format, expected:"); LOG_STRING(strCmdFormat));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        std::string condition;

        // wrong format
        if (false == ustring::extractCondition(args, condition)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong format, expected:"); LOG_STRING(strCmdFormat));
            break;
        }

        bool bResult = false;
        if (false == eval::validateVectorBooleans(condition, "AND", bResult)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to evaluate vector of bool:"); LOG_STRING(condition));
            break;
        }

        if (true == bResult) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("EXIT REQUESTED BY CONDITIONS"));
            break;
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

}




///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////



/**
 * \brief Generic function for message handling
 * \param[in] args Message to be shown to the user
 * \param[in] bBreakpoint flag to decide if the message is a breakpoint too
 * \return true on success, false otherwise
 */

bool UtilsPlugin::m_GenericMessageHandling (const std::string& args, bool bIsBreakpoint) const
{
   bool bRetVal = false;

    do {

        // expecting arguments, fails if not provided
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing: message"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        if (true == bIsBreakpoint) {
            CheckContinue prompt;

            if (false == prompt(nullptr)) {
                std::cout << "Exiting based on user choice\n";
                break;
            }
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
 * \brief Generic function for validation handling
 * \param[in] vstrArgs vector of strings containing the arguments
 * \param[in] bIsStringRule flag to set the compare rule as being for strings
 * \param[out] pbResult pointer where to store evaluation result
 * \return true on success, false otherwise
 */

bool UtilsPlugin::m_GenericEvaluationHandling (std::vector<std::string>& vstrArgs, const bool bIsStringRule, bool& bEvalResult) const
{
   bool bRetVal = true;

    do {

        if (3 != vstrArgs.size()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid vector size"));
            bRetVal = false;
            break;
        }

        ustring::undecorate(vstrArgs[0]);
        ustring::undecorate(vstrArgs[2]);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Vectors: ["); LOG_STRING(vstrArgs[0]); LOG_STRING("] - ["); LOG_STRING(vstrArgs[2]); LOG_STRING("]") );

        if ((true == vstrArgs[0].empty()) && (true == vstrArgs[2].empty())) {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Evaluate empty strings"));
            bEvalResult = m_Validate(vstrArgs[0], vstrArgs[1], vstrArgs[2], bIsStringRule ? eValidateType::STRING : eValidateType::NUMBER);
            break;
        }

        // check if requested to compare as string
        if (true == bIsStringRule) {
            if ((true == eval::isValidVectorOfStrings(vstrArgs[0])) || (true == eval::isValidVectorOfStrings(vstrArgs[2]))) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate vectors of strings"));
                bEvalResult = m_Validate(vstrArgs[0], vstrArgs[1], vstrArgs[2], eValidateType::STRING);
                break;
            }
        }

        // check / compare items as versions
        if ((true == eval::isValidVersion(vstrArgs[0])) || (true == eval::isValidVersion(vstrArgs[2]))) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate versions"));
            bEvalResult = m_Validate(vstrArgs[0], vstrArgs[1], vstrArgs[2], eValidateType::VERSION);
            break;
        }

        // check / compare items as vectors of numbers
        if ((true == eval::isValidVectorOfNumbers(vstrArgs[0])) || (true == eval::isValidVectorOfNumbers(vstrArgs[2]))) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate vector of numbers"));
            bEvalResult = m_Validate(vstrArgs[0], vstrArgs[1], vstrArgs[2], eValidateType::NUMBER);
            break;
        }

        // check / compare items as vectors of booleans
        if ((true == eval::isValidVectorOfBools(vstrArgs[0])) || (true == eval::isValidVectorOfBools(vstrArgs[2]))) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate vector of booleans"));
            bEvalResult = m_Validate(vstrArgs[0], vstrArgs[1], vstrArgs[2], eValidateType::BOOLEAN);
            break;
        }

    } while(false);

    if (false == bRetVal) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Item evaluation execution failed"));
    } else {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Item evaluation"); LOG_STRING((true == bEvalResult) ? "passed":"failed"));
    }

    return bRetVal;

}



bool UtilsPlugin::m_Validate(const std::string& strArgLeft, const std::string& strRule, const std::string& strArgRight, eValidateType eType) const
{
    std::vector<std::string>vstrLeft;
    std::vector<std::string>vstrRight;

    ustring::tokenize(strArgLeft,  CHAR_SEPARATOR_SPACE, vstrLeft);
    ustring::tokenize(strArgRight, CHAR_SEPARATOR_SPACE, vstrRight);

    return m_validator.validate(vstrLeft, vstrRight, strRule, eType);

}



/**
 * \brief Evaluate an expression provided a string (expected to contain vector1 rule vector2)
 * \param[in] args string containing the expression to be evaluated
 * \param[out] bEvalResult reference where to return the result
 * \return true on success, false otherwise
 */

bool UtilsPlugin::m_EvaluateExpression (const std::string& args, bool& bEvalResult) const
{
    const std::string strCmdFormat = "use: V1/$M1 rule V2/$M2 or $M";
    bool bRetVal = false;

    do {

        // no arguments are expected
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing args,"); LOG_STRING(strCmdFormat));
            break;
        }

        // extract arguments
        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // check if called with macro as parameter
        if (1 == szNrArgs) {
            if (false == ustring::isValidMacroUsage(args))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid args:"); LOG_STRING(args); LOG_STRING(strCmdFormat));
                break;
            }

            // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
            if (false == m_bIsEnabled)
            {
                bRetVal = true;
                break;
            }
        }

        // not a compact macro then expect the normal format val1 rule val2
        if (3 != szNrArgs) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 3 args,"); LOG_STRING(strCmdFormat));
            break;
        }

        bool bIsStringRule  = eval::isStringValidationRule(vstrArgs[1]);
        bool bIsNumericRule = eval::isNumericValidationRule(vstrArgs[1]);

        // check if the validation rule is correct
        if ((false == bIsStringRule) && (false == bIsNumericRule)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid rule:"); LOG_STRING(vstrArgs[1]));
            break;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(vstrArgs[1]); LOG_STRING(bIsStringRule ? "string" : "numeric"); LOG_STRING("rule"));

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

       // evaluate the expression and return the status in the provided variable
       bRetVal = m_GenericEvaluationHandling( vstrArgs, bIsStringRule, bEvalResult);

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------*/

bool UtilsPlugin::m_LocalSetParams (const PluginDataSet *psSetParams)
{
    bool bRetVal = true;

    if (false == psSetParams->mapSettings.empty()) {
        do {

            // extract params here

        } while(false);
    }

    return bRetVal;

} /* m_LocalSetParams() */