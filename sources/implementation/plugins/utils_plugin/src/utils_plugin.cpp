#include "CommonSettings.hpp"
#include "PluginSpecOperations.hpp"

#include "utils_plugin.hpp"

#include "uTimer.hpp"
#include "uString.hpp"
#include "uEvaluator.hpp"
#include "uCheckContinue.hpp"


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
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#if 0
// thread callback type declaration
#if defined(_MSC_VER)
    using THREADFUNCPTR = void (*)(std::atomic<bool> &);
#else
    using THREADFUNCPTR = void* (*)(void*);
#endif
#endif

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
//                          CLASS STATIC MEMBERS                 //
///////////////////////////////////////////////////////////////////


//uint32_t UtilsPlugin::m_u32PollingInterval;
//std::atomic<bool> UtilsPlugin::m_bUartMonitoring;

///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool UtilsPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
//    m_bUartMonitoring.store(false);

    return m_bIsInitialized;

}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void UtilsPlugin::doCleanup(void)
{
#if 0
    int iThreadRetVal = 0;

    if (false == m_vThreadArray.empty())
    {

        // if started then stop the UART insertion monitoring
        if (true == m_bUartMonitoring.load())
        {
            uart_list_ports("Stopping UART monitoring =>");
            m_bUartMonitoring.store(false);
        }

#if defined(_MSC_VER)

        // join threads
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Joining threads created by plugin:"); LOG_UINT32((uint32_t)m_vThreadArray.size()));
        for( unsigned int i = 0; i < m_vThreadArray.size(); ++i)
        {
            m_vThreadArray.at(i).join();
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] std::thread.join() OK"));
        }

#else

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Stopping & joining threads created by plugin:"); LOG_UINT32((uint32_t)m_vThreadArray.size()));
        for( unsigned int i = 0; i < m_vThreadArray.size(); ++i)
        {
            void *pvJoinRetVal = nullptr;

            // cancel threads (ensure that they were set as cancelable)
            iThreadRetVal = pthread_cancel(m_vThreadArray.at(i));
            LOG_PRINT(((0 == iThreadRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] pthread_cancel()"); LOG_STRING((0 == iThreadRetVal) ? "ok" : "failed"));

            // join threads
            iThreadRetVal = pthread_join(m_vThreadArray.at(i), &pvJoinRetVal);
            LOG_PRINT(((0 == iThreadRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] pthread_join()"); LOG_STRING((0 == iThreadRetVal) ? "ok" : "failed"));

        }
#endif

    }

#endif

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

#if 0
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("LIST_UART_PORTS : lists the uart ports reported by the system"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.LIST_UART_PORTS"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_UART_INSERT : wait for UART port insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.WAIT_UART_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UTILS.WAIT_UART_INSERT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UTILS.WAIT_UART_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $NEW_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be absent at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_UART_REMOVE : wait for UART port removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.WAIT_UART_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UTILS.WAIT_UART_REMOVE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UTILS.WAIT_UART_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $REMOVED_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be present at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("START_UART_MONITORING : start reporting UART port insertions and removals"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.START_UART_MONITORING"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : runs until the end of script; for experimental monitoring use as:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.START_UART_MONITORING"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.DELAY 10000"));
#endif
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
    if (!m_bIsEnabled)
        return true;

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


#if 0

/**
  * \brief WAIT_UART_INSERT wait for an USB UART port to be inserted
  *        with a specified timeout (if 0 or not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UTILS.WAIT_UART_INSERT
  *       UTILS.WAIT_UART_INSERT 3000
  *
  * \param[in] none or timeout to wait for the UART insertion
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_WAIT_UART_INSERT (const std::string &args) const
{
    return m_GenericUartHandling (args, uart_wait_port_insert);

}


/**
  * \brief WAIT_UART_REMOVE wait for an USB UART port to be removed
  *        with a specified timeout (if 0 or not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UTILS.WAIT_UART_REMOVE
  *       UTILS.WAIT_UART_REMOVE 3000
  *
  * \note If no port is available at the moment of call then the command returns immediatelly
  *
  * \param[in] none or timeout to wait for the UART removal
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_WAIT_UART_REMOVE (const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (0 == uart_get_available_ports_number())
        {
            LOG_PRINT(LOG_WARN, LOG_HDR; LOG_STRING("No UART port(s) currently available"));
            bRetVal = true;
            break;
        }

        bRetVal = m_GenericUartHandling (args, uart_wait_port_remove);

    } while(false);

    return bRetVal;

}


/**
  * \brief Monitor UART ports for the specified action (insertion / removal)
  *
  * \note Usage example: <br>
  *      UTILS.START_UART_MONITORING
  *
  * \param none
  *
  * \return true if the execution succeeded, false otherwise
*/

bool UtilsPlugin::m_Utils_START_UART_MONITORING (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // arguments expected
        if (false == args.empty()) {
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No argument expected"));
            break;
        }

        // only one monitoring per operation is allowed
        if (true == m_bUartMonitoring.load())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART port monitoring already exists"));
            break;
        }

        // set the internal flags (used to avoid multiple monitorings per operation)
        m_bUartMonitoring.store(true);

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // declare the function to be passed to the monitoring thread
        THREADFUNCPTR pfctThreadCB = (THREADFUNCPTR)&UtilsPlugin::m_threadUartMonitoring;

        // create the thread and add it to the vector of threads for joining them later ..
#if defined(_MSC_VER)

        std::thread threadExec(pfctThreadCB, std::ref(m_bUartMonitoring));
        m_vThreadArray.push_back(std::move(threadExec));

#else // Linux & MINGW

        pthread_t threadExec;
        if (0 != pthread_create( &threadExec, nullptr, pfctThreadCB, nullptr))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to create pthread for UART monitoring"));
            break;
        }
        m_vThreadArray.push_back(threadExec);

#endif // #if defined(_MSC_VER)

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief list UART ports reported by the system
  *
  * \note Usage example: <br>
  *       UTILS.LIST_UART_PORTS
  *
  * \param[in] none
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_LIST_UART_PORTS (const std::string &args) const
{
   bool bRetVal = false;

    do {

        // no arguments are expected
        if (false == args.empty()) {
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unexpected arguments:"); LOG_STRING(args));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        uart_list_ports();
        bRetVal = true;

    } while(false);

    return bRetVal;

}

#endif

///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////

#if 0

/**
 * \brief Generic function for UART port handling (insert, remove)
 * \param[in] args argumen(s) as string, here timeout to wait for insert/removal
 * \param[in] pfUartHdl pointer to a function to be called for handling
 * \return true on success, false otherwise
 */

bool UtilsPlugin::m_GenericUartHandling (const char *args, PFUARTHDL pfUartHdl) const
{
    bool bRetVal = false;
    uint32_t uiTimeout = 0;

    do {

        // if arguments are provided
        if (false == args.empty()) {
        {
            // fail if more than one space separated arguments is provided ...
            if (true == string_contains_char(args, CHAR_SEPARATOR_SPACE))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: delay"));
                break;
            }

            // convert string to integer
            if (false == numeric::str2uint32( args, uiTimeout))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Incorrect delay value:"); LOG_STRING(args));
                break;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // buffer to store device name, i.e. windows: COM0 ... COM255, linux: /dev/ttyUSB0 .. 255 /dev/ttyACM0 .. 255
        char vcItem[32] = { 0 };

        // execute the callback
        if (false == pfUartHdl(vcItem, sizeof(vcItem), uiTimeout, m_u32PollingInterval))
        {
            break;
        }

        // this value can be returned to a variable
        m_strResultData.assign(vcItem);

        bRetVal = true;

    } while(false);

    return bRetVal;

}

#endif

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
   bool bRetVal = false;

    do {

        if ((true == vstrArgs[0].empty()) && (true == vstrArgs[2].empty()))
        {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Evaluate: empty strings"));
            break;
        }


        // check if requested to compare as string
        if (true == bIsStringRule)
        {
            if ((true == eval::isValidVectorOfStrings(vstrArgs[0])) || (true == eval::isValidVectorOfStrings(vstrArgs[2])))
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate: vectors of strings"));
                bEvalResult = m_validator.validate(vstrArgs[0], vstrArgs[2], vstrArgs[1], eValidateType::STRING)
                break;
            }
        }

        // check / compare items as versions
        if ((true == eval::isValidVersion(vstrArgs[0])) || (true == eval::isValidVersion(vstrArgs[2])))
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate: versions"));
            bEvalResult = m_validator.validate(vstrArgs[0], vstrArgs[2], vstrArgs[1], eValidateType::VERSION)
            break;
        }

        // check / compare items as vectors of numbers
        if ((true == eval::isValidVectorOfNumbers(vstrArgs[0])) || (true == eval::isValidVectorOfNumbers(vstrArgs[2])))
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate: vector of numbers"));
            bEvalResult = m_validator.validate(vstrArgs[0], vstrArgs[2], vstrArgs[1], eValidateType::NUMBER)
            break;
        }

        // check / compare items as vectors of booleans
        if ((true == eval::isValidVectorOfBools(vstrArgs[0])) || (true == eval::isValidVectorOfBools(vstrArgs[2])))
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Evaluate: vector of booleans"));
            bEvalResult = m_validator.validate(vstrArgs[0], vstrArgs[2], vstrArgs[1], eValidateType::BOOLEAN)
            break;
        }

        bRetVal = true;

    } while(false);

    if (false == bRetVal) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Item evaluation execution failed"));
    } else {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Item evaluation"); LOG_STRING((true == bEvalResult) ? "passed":"failed"));
    }

    return bRetVal;

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


#if 0
#if defined(_MSC_VER)

/**
  * \brief Thread's callback used to monitor the UART port insertion
  * \param[in] bRun flag used to control the thread execution
  * \return null pointer
*/

void UtilsPlugin::m_threadUartMonitoring( std::atomic<bool> & bRun)
{
    uart_list_ports("(T) UART monitoring started in background =>");
    uart_monitor( m_u32PollingInterval, bRun);

}

#else // Linux & MINGW

/**
  * \brief Thread's callback used to monitor the UART port insertion
  * \param[in] pvThreadArgs pointer to the thread parameters
  * \return null pointer
*/

void* UtilsPlugin::m_threadUartMonitoring (void *pvThreadArgs)
{
    const std::string strCaption = "(T) UART monitoring";
    int iThRetVal = 0;

    do {

        if (0 != (iThRetVal = pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL)))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrCaption); LOG_STRING(": pthread_setcancelstate() failed, error:"); LOG_INT(iThRetVal));
            break;
        }

        if (0 != (iThRetVal = pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL)))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrCaption); LOG_STRING(": pthread_setcanceltype() failed, error:"); LOG_INT(iThRetVal));
            break;
        }

        // permanently false as the thread will be canceled
        std::atomic<bool> bRun (true);

        uart_list_ports("(T) UART monitoring started in background =>");
        uart_monitor( m_u32PollingInterval, std::ref(bRun));

    } while(false);

    return nullptr;

}

#endif // #if defined(_MSC_VER)

#endif

///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool UtilsPlugin::m_LocalSetParams (const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {

            bRetVal = true;

        } while(false);
    }

    return bRetVal;

} /* m_LocalSetParams() */