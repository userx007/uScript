#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "IScriptInterpreter.hpp"
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

#define LT_HDR     "PSINTERPRET:"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

class PluginScriptInterpreter : public IScriptInterpreter<PluginScriptEntriesType>
{

    public:

        PluginScriptInterpreter () = default;
        virtual ~PluginScriptInterpreter () = default;

        bool interpretScript(PluginScriptEntriesType& sScriptEntries, PFSEND pfsend, PFWAIT pfwait ) override
        {
            bool bRetVal = true;

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));

            return bRetVal;
        }
};

#endif // PLUGINSCRIPTINTERPRETER_HPP