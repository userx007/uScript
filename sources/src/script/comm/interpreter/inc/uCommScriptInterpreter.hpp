#ifndef COMMSCRIPTINTERPRETER_HPP
#define COMMSCRIPTINTERPRETER_HPP

#include "uSharedConfig.hpp"

#include "uIScriptInterpreter.hpp"
#include "ICommDriver.hpp"

#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandInterpreter.hpp"

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
class CommScriptInterpreter : public ICommScriptInterpreter<CommScriptEntriesType, TDriver>
{
    public:

        /**
         * @brief Constructor
         * @param shpDriver Shared pointer to the communication driver
         * @param szMaxRecvSize Maximum buffer size for receive operations
         * @param u32DefaultTimeout Default timeout in milliseconds
         * @param szDelay Delay in milliseconds between command executions
         */
        explicit CommScriptInterpreter(
            std::shared_ptr<const TDriver> shpDriver, 
            size_t szMaxRecvSize = PLUGIN_DEFAULT_RECEIVE_SIZE,
            uint32_t u32DefaultTimeout = 5000,
            size_t szDelay = 0
        )
            : m_shpItemInterpreter(std::make_shared<CommScriptCommandInterpreter<TDriver>>(
                shpDriver, 
                szMaxRecvSize, 
                u32DefaultTimeout
              ))
            , m_szDelay(szDelay)
        {}

        bool interpretScript (CommScriptEntriesType& sScriptEntries) override
        {
            bool bRetVal = true;

            for (const auto& command : sScriptEntries.vCommands) {
                if (false == m_shpItemInterpreter->interpretItem(command)) {
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

        std::shared_ptr<CommScriptCommandInterpreter<TDriver>> m_shpItemInterpreter;
        size_t m_szDelay;
};

#endif //COMMSCRIPTINTERPRETER_HPP