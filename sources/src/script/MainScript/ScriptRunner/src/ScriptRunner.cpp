#include "ScriptRunner.hpp"
#include "ScriptReader.hpp"
#include "ScriptValidator.hpp"
#include "ScriptInterpreter.hpp"
#include "IScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"
#include "uLogger.hpp"

#include <vector>
#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "S_RUNNER   :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////


bool ScriptRunner::runScript()
{
    bool bRetVal = false;

    do {

        std::vector<std::string> vstrScriptLines;
        ScriptEntriesType sScriptEntries;

        if (false == m_shpScriptReader->readScript(vstrScriptLines)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to read script"));
            break;
        }

        if (false == m_shvScriptValidator->validateScript(vstrScriptLines, sScriptEntries)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate script"));
            break;
        }

        if (false == m_shvScriptInterpreter->interpretScript(sScriptEntries)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to interpret script"));
            break;
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

}
