#include "uSharedConfig.hpp"
#include "uArgsParserExt.hpp"
#include "uIniCfgLoader.hpp"
#include "uScriptClient.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"   // g_gui_mode + gui_notify_* (GUI front-end support)

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "USCRIPT_MAIN|"
#define LOG_HDR    LOG_STRING(LT_HDR)

/*-------------------------------------------------------------------------------
                             MAIN
-------------------------------------------------------------------------------*/

int main(int argc, char const *argv[])
{
    bool bRetVal = false;

    LOG_INIT(LOGGER_DEFAULT_CONSOLE_SEVERITY, 
             LOGGER_DEFAULT_LOGFILE_SEVERITY, 
             LOGGER_DEFAULT_ENABLE_FILELOG, 
             LOGGER_DEFAULT_USE_COLORS, 
             LOGGER_DEFAULT_INCLUDE_DATE);

    do {
        CommandLineParser cli("Script execution tool");
        cli.add_option("script", "s", "script pathname", false, SCRIPT_DEFAULT);
        cli.add_option("inicfg", "c", "ini config pathname", false, SCRIPT_INI_CONFIG);
        // Note: GUI mode is activated via the SCRIPT_GUI_MODE environment variable,
        // set by the Qt front-end before launching this process.  No CLI flag needed.

        // Parse returns a result object with success status and error details
        auto result = cli.parse(argc, argv);
        
        if (!result) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Parsing failed!"));
            CommandLineParser::print_errors(result);
            cli.print_usage(argv[0]);
            break;
        }

        // GUI mode: activated exclusively via SCRIPT_GUI_MODE env var so the
        // CLI parser does not need a special flag type.  The Qt front-end sets
        // this before QProcess::start().  stdout is made fully unbuffered so
        // every GUI:xxx line reaches the QProcess pipe without delay.
        if (std::getenv("SCRIPT_GUI_MODE") != nullptr) {
            g_gui_mode = true;
            setbuf(stdout, nullptr);    // unbuffered — critical for QProcess pipe
        }

        // Use get_or() for cleaner code with defaults
        std::string scriptPathName = cli.get_or("script", SCRIPT_DEFAULT);
        std::string iniPathName = cli.get_or("inicfg", SCRIPT_INI_CONFIG);

        IniCfgLoader iniLoader;
        if (iniLoader.load(iniPathName)) {
            if (iniLoader.loadSection(COMMON_INI_SECTION_NAME)) {
                size_t szLogSeverityConsole = static_cast<size_t>(LOGGER_DEFAULT_CONSOLE_SEVERITY);
                size_t szLogSeverityFile    = static_cast<size_t>(LOGGER_DEFAULT_LOGFILE_SEVERITY);
                bool   bLogIncludeDate      = true;
                bool   bLogColoredConsole   = true;
                bool   bLog2FileEnabled     = true;

                iniLoader.getNumFromIni (SCRIPT_INI_LOG_SEVERITY_CONSOLE, szLogSeverityConsole);
                iniLoader.getNumFromIni (SCRIPT_INI_LOG_SEVERITY_FILE,    szLogSeverityFile);
                iniLoader.getBoolFromIni(SCRIPT_INI_INCLUDE_DATE,         bLogIncludeDate);
                iniLoader.getBoolFromIni(SCRIPT_INI_LOG_CONSOLE_COLORED,  bLogColoredConsole);
                iniLoader.getBoolFromIni(SCRIPT_INI_ENABLE_LOG_TO_FILE,   bLog2FileEnabled);

                LOG_INIT(sizet2loglevel(szLogSeverityConsole).value_or(LOGGER_DEFAULT_CONSOLE_SEVERITY),
                         sizet2loglevel(szLogSeverityFile   ).value_or(LOGGER_DEFAULT_LOGFILE_SEVERITY),
                         bLog2FileEnabled,
                         bLogColoredConsole,
                         bLogIncludeDate);
            }  
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Script: ["); LOG_STRING(scriptPathName); LOG_STRING("]"));
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Config: ["); LOG_STRING(iniPathName); LOG_STRING("]"));

        ScriptClient client(scriptPathName, std::move(iniLoader));
        
        // dry execution for command validation
        if (client.execute(false)) {
            // real execution
            bRetVal = client.execute(true);
        }

    } while(false);

    return (true == bRetVal) ? 0 : 1;
}