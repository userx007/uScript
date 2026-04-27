#ifndef U_COMM_SCRIPT_INTERPRETER_HPP
#define U_COMM_SCRIPT_INTERPRETER_HPP

#include "IScriptInterpreter.hpp"
#include "ICommDriver.hpp"
#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uTimer.hpp"

#include <string>
#include <memory>
#include <optional>

/////////////////////////////////////////////////////////////////////////////////
//                             LOG DEFINITIONS                                 //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "COMM_SCR_I  |"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

template <typename TDriver>
class CommScriptInterpreter : public ICommScriptInterpreter<CommCommandsType, TDriver>
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
            size_t szDelay = 0)
            : m_shpCommandInterpreter(std::make_shared<CommScriptCommandInterpreter<TDriver>>(
                shpDriver, 
                szMaxRecvSize, 
                u32DefaultTimeout
              ))
            , m_szDelay(szDelay)
        {}

        bool interpretScript (CommCommandsType& sScriptEntries, bool bRealExec) override
        {
            bool bRetVal = true;

            /* dry validation */
            if (false == bRealExec)
            {            
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Snapshot script entries for (repeated) execution"));

                /* nothing to validate for the comm scripts, just snapshot the script entries */
                m_getValidatedScript() = sScriptEntries.vCommands;
            }
            else
            {
                /* Real execution: run from the validated snapshot (kept intact across iterations) */
                if (false == m_getValidatedScript().has_value()) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Real exec requested but no validated script snapshot exists"));
                    return false;
                }

                for (const auto& command : m_getValidatedScript().value()) {
                    if (false == m_shpCommandInterpreter->interpretCommand(command, bRealExec)) {
                        bRetVal = false;
                        break;
                    }
                    utime::delay_ms(m_szDelay);
                }
            }

            LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
                    LOG_STRING("COMM script");
                    LOG_STRING(false == bRealExec ? "dry run" : "execution");
                    LOG_STRING(bRetVal ? "ok" : "failed"));
            return bRetVal;
        }

    private:
        
        std::shared_ptr<CommScriptCommandInterpreter<TDriver>> m_shpCommandInterpreter;
        size_t m_szDelay;

        /* Validated script snapshot shared across all instances of the same TDriver
         * specialization. Set once during the dry-run pass and read (without
         * consuming) on every real-execution call, so REPEAT N works correctly. */
        static std::optional<std::vector<CommCommand>>& m_getValidatedScript()
        {
            static std::optional<std::vector<CommCommand>> s_snapshot;
            return s_snapshot;
        }
};

#endif //U_COMM_SCRIPT_INTERPRETER_HPP