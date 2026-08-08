#ifndef U_COMM_SCRIPT_INTERPRETER_HPP
#define U_COMM_SCRIPT_INTERPRETER_HPP

#include "IScriptInterpreter.hpp"
#include "ICommDriver.hpp"
#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uSharedConfig.hpp"
#include "uLogger.hpp"
#include "uTimer.hpp"
#include "uGuiNotify.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>

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

        using SendFunc = typename CommScriptCommandInterpreter<TDriver>::SendFunc;
        using RecvFunc = typename CommScriptCommandInterpreter<TDriver>::RecvFunc;

        /**
         * @brief Constructor
         * @param shpDriver         Shared pointer to the communication driver
         * @param strPluginName     Plugin identity for the GUI comm-dump panel
         *                          (e.g. UART_PLUGIN_NAME), forwarded to
         *                          CommScriptCommandInterpreter.
         * @param szMaxRecvSize     Maximum buffer size for receive operations
         * @param u32DefaultTimeout Default timeout in milliseconds
         * @param szDelay           Delay in milliseconds between command executions
         * @param strScriptPath     Script path used as key in the snapshot cache
         * @param pfsend            Optional physical-write override, forwarded verbatim to
         *                          CommScriptCommandInterpreter (see its constructor docs).
         *                          Default empty preserves today's behaviour for every
         *                          caller that doesn't pass one.
         * @param pfrecv            Optional physical-read override, see pfsend.
         */
        explicit CommScriptInterpreter(
            std::shared_ptr<const TDriver> shpDriver,
            std::string strPluginName,
            size_t szMaxRecvSize       = PLUGIN_DEFAULT_RECEIVE_SIZE,
            uint32_t u32DefaultTimeout = 5000,
            size_t szDelay             = 0,
            std::string strScriptPath  = {},
            SendFunc pfsend            = SendFunc{},
            RecvFunc pfrecv            = RecvFunc{})
            : m_shpCommandInterpreter(std::make_shared<CommScriptCommandInterpreter<TDriver>>(
                shpDriver,
                std::move(strPluginName),
                szMaxRecvSize,
                u32DefaultTimeout,
                std::move(pfsend),
                std::move(pfrecv)
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
                 * per-path cache. No device I/O takes place here.
                 *
                 * The (potentially large) vector copy happens *before* the
                 * lock is taken, so the lock itself is only ever held for
                 * the map insertion/assignment - never for the length of a
                 * copy, and never anywhere near device I/O. */
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("Snapshot script entries for (repeated) execution"));
                auto snapshot = std::make_shared<const std::vector<CommCommand>>(sScriptEntries.vCommands);
                std::unique_lock lock(m_getCacheMutex());
                m_getCache()[m_strScriptPath] = std::move(snapshot);
            }
            else
            {
                /* Real-execution pass: look up the snapshot by script path.
                 * The entry must have been populated during the dry-run pass.
                 *
                 * Only the map lookup + a shared_ptr copy (an atomic refcount
                 * bump, not a vector copy) happens under the lock. The lock is
                 * released before iterating/executing, so a concurrent
                 * dry-run on another thread - for this path or any other -
                 * never blocks on, or is blocked by, this thread's (possibly
                 * slow, real device I/O bound) execution loop. Holding our
                 * own shared_ptr also means that if another thread's
                 * dry-run *does* replace this path's cache entry while we're
                 * mid-execution, our in-flight snapshot stays alive and
                 * unchanged underneath us - no dangling reference into a
                 * vector that could be reassigned/reallocated concurrently. */
                std::shared_ptr<const std::vector<CommCommand>> snapshot;
                {
                    std::shared_lock lock(m_getCacheMutex());
                    auto& cache = m_getCache();
                    auto  it    = cache.find(m_strScriptPath);
                    if (it == cache.end()) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("Real exec requested but no snapshot exists for:");
                                  LOG_STRING(m_strScriptPath));
                        return false;
                    }
                    snapshot = it->second;
                }

                for (const auto& command : *snapshot) {
                    /* Notify the GUI front-end which comm-script line is about
                     * to execute so it can advance the execution bar in the correct
                     * comm tab.  g_gui_comm_tid is set to the spawning line number by
                     * ScriptInterpreter before doDispatch() is called on each thread;
                     * it is 0 on the main thread.  Using a thread_local avoids any
                     * interface change to IPlugin or CommScriptClient. */
                    if (get_gui_comm_tid() > 0) {
                        gui_notify_exec_comm_t(get_gui_comm_tid(), command.iLineNumber);
                    } else {
                        gui_notify_exec_comm(command.iLineNumber);
                    }

                    if (!m_shpCommandInterpreter->interpretCommand(command, bRealExec)) {
                        gui_notify_error_comm(command.iLineNumber);
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
            std::shared_lock lock(m_getCacheMutex());
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
         *         re-validating the script file.
         *
         * Value type is shared_ptr<const vector<CommCommand>> rather than a
         * plain vector<CommCommand>: this map (and the mutex guarding it) is
         * static, i.e. process-wide, and is read/written from multiple
         * threads running the same script path in parallel (e.g. one thread
         * per unit under test). A shared_ptr lets a reader take its own
         * reference-counted handle to the current snapshot while holding the
         * lock only as long as an atomic refcount bump takes - not for the
         * O(n) cost of copying the vector, and not anywhere near this
         * thread's (I/O-bound, potentially slow) execution loop. It also
         * means a concurrent dry-run replacing this path's entry can't pull
         * the vector out from under a thread that's still executing off the
         * previous snapshot. */
        static std::unordered_map<std::string, std::shared_ptr<const std::vector<CommCommand>>>& m_getCache()
        {
            static std::unordered_map<std::string, std::shared_ptr<const std::vector<CommCommand>>> s_cache;
            return s_cache;
        }

        /* Guards m_getCache(). A shared_mutex rather than a plain mutex since
         * real-execution reads (one lookup + a shared_ptr copy each) vastly
         * outnumber dry-run writes for any given script path in normal use -
         * multiple threads can hold the shared (read) lock concurrently, and
         * only the infrequent write path needs exclusive access. */
        static std::shared_mutex& m_getCacheMutex()
        {
            static std::shared_mutex s_mutex;
            return s_mutex;
        }
};

#endif // U_COMM_SCRIPT_INTERPRETER_HPP
