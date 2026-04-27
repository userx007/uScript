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
#include <unordered_map>
#include <vector>

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
         * @param shpDriver         Shared pointer to the communication driver
         * @param szMaxRecvSize     Maximum buffer size for receive operations
         * @param u32DefaultTimeout Default timeout in milliseconds
         * @param szDelay           Delay in milliseconds between command executions
         * @param strScriptPath     Script path used as key in the snapshot cache
         */
        explicit CommScriptInterpreter(
            std::shared_ptr<const TDriver> shpDriver,
            size_t szMaxRecvSize     = PLUGIN_DEFAULT_RECEIVE_SIZE,
            uint32_t u32DefaultTimeout = 5000,
            size_t szDelay           = 0,
            std::string strScriptPath = {})
            : m_shpCommandInterpreter(std::make_shared<CommScriptCommandInterpreter<TDriver>>(
                shpDriver,
                szMaxRecvSize,
                u32DefaultTimeout
              ))
            , m_szDelay(szDelay)
            , m_strScriptPath(std::move(strScriptPath))
        {}

        bool interpretScript(CommCommandsType& sScriptEntries, bool bRealExec) override
        {
            bool bRetVal = true;

            if (!bRealExec)
            {
                /* Dry-run pass: store the validated command list in the
                 * per-path cache.  No device I/O takes place here. */
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("Snapshot script entries for (repeated) execution"));
                m_getCache()[m_strScriptPath] = sScriptEntries.vCommands;
            }
            else
            {
                /* Real-execution pass: look up the snapshot by script path.
                 * The entry must have been populated during the dry-run pass. */
                auto& cache = m_getCache();
                auto  it    = cache.find(m_strScriptPath);
                if (it == cache.end()) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("Real exec requested but no snapshot exists for:");
                              LOG_STRING(m_strScriptPath));
                    return false;
                }

                for (const auto& command : it->second) {
                    if (!m_shpCommandInterpreter->interpretCommand(command, bRealExec)) {
                        bRetVal = false;
                        break;
                    }
                    utime::delay_ms(m_szDelay);
                }
            }

            LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR;
                      LOG_STRING("COMM script");
                      LOG_STRING(!bRealExec ? "dry run" : "execution");
                      LOG_STRING(bRetVal ? "ok" : "failed"));
            return bRetVal;
        }

        /**
         * @brief Returns true if a validated snapshot exists for the given path.
         *        Used by CommScriptClient to decide whether the dry-run phase
         *        can be skipped before real execution.
         */
        static bool isCached(const std::string& strScriptPath)
        {
            return m_getCache().count(strScriptPath) != 0;
        }

    private:

        std::shared_ptr<CommScriptCommandInterpreter<TDriver>> m_shpCommandInterpreter;
        size_t      m_szDelay;
        std::string m_strScriptPath;

        /* Per-path snapshot cache, shared across all instances of the same
         * TDriver specialization.
         *
         * Key   : canonical script path (same string passed to the constructor)
         * Value : validated command list, populated once during the core dry-run
         *         pass and reused on every subsequent real-execution dispatch —
         *         including all iterations of REPEAT N — without re-reading or
         *         re-validating the script file. */
        static std::unordered_map<std::string, std::vector<CommCommand>>& m_getCache()
        {
            static std::unordered_map<std::string, std::vector<CommCommand>> s_cache;
            return s_cache;
        }
};

#endif // U_COMM_SCRIPT_INTERPRETER_HPP
