#include "ScriptClient.hpp"
#include "CommonSettings.hpp"
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

#define LT_HDR     "S_MAIN     :"
#define LOG_HDR    LOG_STRING(LT_HDR)

/*-------------------------------------------------------------------------------
                             MAIN
-------------------------------------------------------------------------------*/

int main(int argc, char const *argv[])
{
    bool bRetVal = false;

    do {
        CommandLineParser cli(argv[0]);
        cli.add_option("script", "s", "script pathname", false);
        cli.add_option("inicfg", "c", "ini config pathname", false);
        cli.parse(argc, argv);

        auto script = cli.get("script");
        auto inicfg = cli.get("inicfg");

        if (false == cli.check_required()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing required options!"));
            cli.print_usage();
            break;
        }

        if (script) {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cmdline script ["); LOG_STRING(*script); LOG_STRING("]"));
        }

        if (inicfg) {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cmdline config ["); LOG_STRING(*inicfg); LOG_STRING("]"));
        }

        std::string scriptName = script.value_or(SCRIPT_DEFAULT);
        std::string inicfgName = inicfg.value_or(SCRIPT_INI_CONFIG);
        ScriptClient client(scriptName, inicfgName);

        bool bRetVal = client.execute();

    } while(false);

    return (true == bRetVal) ? 0 : 1;

}
