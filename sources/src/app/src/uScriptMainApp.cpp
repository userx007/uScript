#include "uScriptClient.hpp"
#include "uSharedConfig.hpp"
#include "uArgsParserExt.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "USCRIPT_APP:"
#define LOG_HDR    LOG_STRING(LT_HDR)

/*-------------------------------------------------------------------------------
                             MAIN
-------------------------------------------------------------------------------*/

int main(int argc, char const *argv[])
{
    bool bRetVal = false;

    do {
        CommandLineParser cli("Script execution tool");
        cli.add_option("script", "s", "script pathname", false, SCRIPT_DEFAULT);
        cli.add_option("inicfg", "c", "ini config pathname", false, SCRIPT_INI_CONFIG);
        
        // Parse returns a result object with success status and error details
        auto result = cli.parse(argc, argv);
        
        if (!result) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Parsing failed!"));
            CommandLineParser::print_errors(result);
            cli.print_usage(argv[0]);
            break;
        }

        // Use get_or() for cleaner code with defaults
        std::string scriptName = cli.get_or("script", SCRIPT_DEFAULT);
        std::string inicfgName = cli.get_or("inicfg", SCRIPT_INI_CONFIG);

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Script: ["); LOG_STRING(scriptName); LOG_STRING("]"));
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Config: ["); LOG_STRING(inicfgName); LOG_STRING("]"));

        ScriptClient client(scriptName, inicfgName);
        bRetVal = client.execute();

    } while(false);

    return (true == bRetVal) ? 0 : 1;
}