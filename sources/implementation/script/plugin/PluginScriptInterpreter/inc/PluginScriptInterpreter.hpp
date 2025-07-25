#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "CommonSettings.hpp"
#include "IScriptInterpreter.hpp"
#include "PluginScriptDataTypes.hpp"
#include "PluginScriptItemInterpreter.hpp"

#include "uLogger.hpp"
#include "uTimer.hpp"

#include <string>
#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                             LOG DEFINITIONS                                 //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PSINTERPRET:"
#define LOG_HDR    LOG_STRING(LT_HDR); LOG_STRING(__FUNCTION__)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

template <typename TDriver>
class PluginScriptInterpreter : public IScriptInterpreter<PluginScriptEntriesType, TDriver>
{
    public:

        using SendFunc = PFSEND<TDriver>;
        using RecvFunc = PFRECV<TDriver>;

        explicit PluginScriptInterpreter (std::shared_ptr<TDriver> shpDriver, SendFunc pfsend, RecvFunc pfrecv, size_t szDelay, size_t szMaxRecvSize)
            : m_shpItemInterpreter(std::make_shared<PluginScriptItemInterpreter<TDriver>>(shpDriver, pfsend, pfrecv, szMaxRecvSize))
            , m_szDelay(szDelay)
            {}

        bool interpretScript (PluginScriptEntriesType& sScriptEntries) override
        {
            bool bRetVal = true;

            for (const auto& item : sScriptEntries.vCommands) {
                if (false == m_shpItemInterpreter->interpretItem(item)) {
                    bRetVal = false;
                    break;
                }

                /* delay between commands execution */
                utime::delay_ms(m_szDelay);
            }

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));
            return bRetVal;

        } /* interpretScript() */

    private:

        std::shared_ptr<PluginScriptItemInterpreter<TDriver>> m_shpItemInterpreter;
        size_t m_szDelay;
};

#endif //PLUGINSCRIPTINTERPRETER_HPP