#ifndef UARTMON_PLUGIN_HPP
#define UARTMON_PLUGIN_HPP
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginExport.hpp"
#include "PluginOperations.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"
#include "uUartMonitor.hpp"

#include <stdint.h>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct PluginDataGet;
struct PluginDataSet;

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define UARTMON_PLUGIN_VERSION "2.0.0.0"
#define UARTMON_PLUGIN_NAME    "UARTMON"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// UARTMON_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef UARTMON_GET_BLOCKING
#define UARTMON_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE     \
    UARTMON_PLUGIN_CMD_RECORD(INFO)              \
    UARTMON_PLUGIN_CMD_RECORD(CONFIG)            \
    UARTMON_PLUGIN_CMD_RECORD(START)             \
    UARTMON_PLUGIN_CMD_RECORD(STOP)              \
    UARTMON_PLUGIN_CMD_RECORD(LIST_PORTS)        \
    UARTMON_PLUGIN_CMD_RECORD(WAIT_INSERT, true) \
    UARTMON_PLUGIN_CMD_RECORD(WAIT_REMOVE, true)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

class UartmonPlugin : public PluginInterface
{
    public:
        /** Registers the plugin's commands and default settings. */
        UartmonPlugin()
            : m_strVersion(UARTMON_PLUGIN_VERSION)
            , m_bIsInitialized(false)
            , m_bIsEnabled(false)
            , m_bIsFaultTolerant(false)
            , m_bIsPrivileged(false)
            , m_strResultData("")
            , m_u32PollingInterval(PLUGIN_DEFAULT_UARTMON_POLLING_INTERVAL)
        {
            // clang-format off
#define UARTMON_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert(std::make_pair(#a, \
            PluginCommandEntry<UartmonPlugin>{&UartmonPlugin::m_Uartmon_##a, UARTMON_GET_BLOCKING(a, ##__VA_ARGS__, false)}));
            UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
#undef UARTMON_PLUGIN_CMD_RECORD
            // clang-format on
        }

        ~UartmonPlugin()
        {
            for (std::thread &t : m_vThreads) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }

        /** Returns whether doInit() has succeeded. */
        bool isInitialized(void) const
        {
            return m_bIsInitialized;
        }

        /** Returns whether the plugin has been enabled. */
        bool isEnabled(void) const
        {
            return m_bIsEnabled;
        }

        /** Initialises the plugin. */
        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = m_UartMonitor.setPollingInterval(m_u32PollingInterval);

            if (!m_bIsInitialized) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Initialization failed: invalid polling interval or monitor already active"));
            }

            return m_bIsInitialized;
        }

        /** Releases the plugin's resources. */
        void doCleanup(void)
        {
            if (m_isRunning) {
                m_UartMonitor.stopMonitoring();
                m_isRunning = false;
            }

            m_bIsInitialized = false;
            m_bIsEnabled     = false;
        }

        /** Enables the plugin. */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /** Applies the settings loaded from the ini file. */
        bool setParams(const PluginDataSet *psSetParams)
        {
            bool bRetVal = false;
            if (true == generic_setparams<UartmonPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }
            return bRetVal;
        }

        /** Fills @p psGetParams with the current settings. */
        void getParams(PluginDataGet *psGetParams) const
        {
            generic_getparams<UartmonPlugin>(this, psGetParams);
        }

        /** Runs command @p strCmd with @p strParams; @p st requests early stop. */
        bool doDispatch(const std::string &strCmd, const std::string &strParams,
                        std::stop_token st = {}) const
        {
            return generic_dispatch<UartmonPlugin>(this, strCmd, strParams, st);
        }

        /** Returns the command table. */
        const PluginCommandsMap<UartmonPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /** Returns the plugin version string. */
        const std::string &getVersion(void) const
        {
            return m_strVersion;
        }

        /** Returns the result data of the last command. */
        const std::string &getData(void) const
        {
            return m_strResultData;
        }

        /** Clears the stored result data. */
        void resetData(void) const
        {
            m_strResultData.clear();
        }

        /** Returns the fault-tolerant flag. */
        bool isFaultTolerant(void) const
        {
            return m_bIsFaultTolerant;
        }

        /** Returns whether the plugin needs privileged access. */
        bool isPrivileged(void) const
        {
            return m_bIsPrivileged;
        }

        /** \brief CONFIG-command setter for m_u32PollingInterval (flag 'i') */
        bool setPollingInterval(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_u32PollingInterval);
        }

    private:
        /** Applies the ini settings to this plugin's configuration. */
        bool m_LocalSetParams(const PluginDataSet *psSetParams);
        PluginCommandsMap<UartmonPlugin> m_mapCmds;
        std::string m_strVersion;
        mutable std::string m_strResultData;
        bool m_bIsInitialized;
        bool m_bIsEnabled;
        bool m_bIsFaultTolerant;
        bool m_bIsPrivileged;
        mutable uint32_t m_u32PollingInterval;

        // Changed from UartMonitor to uart::PortMonitor
        mutable uart::PortMonitor m_UartMonitor;

        mutable std::vector<std::thread> m_vThreads;
        mutable bool m_isRunning = false;

        bool m_GenericWaitFor(const std::string &strArgs, bool bInsert, std::stop_token st) const;

#define UARTMON_PLUGIN_CMD_RECORD(a, ...) bool m_Uartmon_##a(const std::string &args, std::stop_token st) const;
        UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
#undef UARTMON_PLUGIN_CMD_RECORD
};

#endif /* UARTMON_PLUGIN_HPP */