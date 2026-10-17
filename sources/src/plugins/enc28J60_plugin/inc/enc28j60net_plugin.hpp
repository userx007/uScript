#ifndef ENC28J60NET_PLUGIN_HPP
#define ENC28J60NET_PLUGIN_HPP
#include "ICommDriver.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginExport.hpp"
#include "PluginOperations.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uCommandExec.hpp"
#include "uEnc28J60Net.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define ENC28J60NET_PLUGIN_VERSION "1.0.0.0"
#define ENC28J60NET_PLUGIN_NAME    "ENC28J60NET"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

#ifndef ENC28J60NET_GET_BLOCKING
#define ENC28J60NET_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE \
    ENC28J60NET_PLUGIN_CMD_RECORD(INFO)          \
    ENC28J60NET_PLUGIN_CMD_RECORD(CONFIG)        \
    ENC28J60NET_PLUGIN_CMD_RECORD(CMD)           \
    ENC28J60NET_PLUGIN_CMD_RECORD(SCRIPT)        \
    ENC28J60NET_PLUGIN_CMD_RECORD(CYCLIC)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

class Enc28J60NetPlugin : public PluginInterface
{
    public:
        /** Registers the plugin's commands and default settings. */
        Enc28J60NetPlugin()
            : m_strVersion(ENC28J60NET_PLUGIN_VERSION)
            , m_strInstanceName(ENC28J60NET_PLUGIN_NAME)
            , m_bIsInitialized(false)
            , m_bIsEnabled(false)
            , m_bIsFaultTolerant(false)
            , m_bIsPrivileged(false)
            , m_strResultData()
            , m_bRawResult(false)
            , m_bCyclicCached(true)
            , m_u16ServerPort(0U)
            , m_u32ReadTimeout(Enc28J60Net::ENC28J60NET_TIMEOUT_MS)
            , m_u32WriteTimeout(Enc28J60Net::ENC28J60NET_TIMEOUT_MS)
            , m_u32ReadBufferSize(1460U)
        {
            // clang-format off
#define ENC28J60NET_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert(std::make_pair(#a, \
            PluginCommandEntry<Enc28J60NetPlugin>{&Enc28J60NetPlugin::m_ENC28J60NET_##a, ENC28J60NET_GET_BLOCKING(a, ##__VA_ARGS__, false)}));
            ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE
#undef ENC28J60NET_PLUGIN_CMD_RECORD
            // clang-format on
        }

        ~Enc28J60NetPlugin() = default;

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

        /** Applies the settings loaded from the ini file. */
        bool setParams(const PluginDataSet *psSetParams)
        {
            bool bRetVal = false;
            if (true == generic_setparams<Enc28J60NetPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }
            return bRetVal;
        }

        /** Fills @p psGetParams with the current settings. */
        void getParams(PluginDataGet *psGetParams) const
        {
            generic_getparams<Enc28J60NetPlugin>(this, psGetParams);
        }

        /** Runs command @p strCmd with @p strParams; @p st requests early stop. */
        bool doDispatch(const std::string &strCmd, const std::string &strParams, std::stop_token st = {}) const
        {
            return generic_dispatch<Enc28J60NetPlugin>(this, strCmd, strParams, st);
        }

        /** Returns the command table. */
        const PluginCommandsMap<Enc28J60NetPlugin> *getMap(void) const
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

        /**
         * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
         */
        bool setRawResult(const std::string &strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /**
         * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
         */
        bool setCyclicCached(const std::string &strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
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

        /** Enables the plugin. */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /** Returns the server ip setting. */
        const char *getServerIp(void) const
        {
            return m_strServerIp.c_str();
        }

        /** Sets the server ip. */
        void setServerIp(const std::string &strServerIp) const
        {
            m_strServerIp.assign(strServerIp);
        }

        /** Returns the server port setting. */
        uint16_t getServerPort(void) const
        {
            return m_u16ServerPort;
        }

        /** Initialises the plugin. */
        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = true;
            return m_bIsInitialized;
        }

        /** Releases the plugin's resources. */
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
            m_strResultData.clear();
        }

        /** Sets the server port from its string form; returns false if the value is invalid. */
        bool setServerPort(const std::string &strServerPort) const
        {
            static constexpr uint32_t TCP_PORT_MAX = 65535U;
            uint32_t u32Port                       = 0U;
            if (false == numeric::str2uint32(strServerPort, u32Port)) {
                return false;
            }
            if (u32Port == 0U || u32Port > TCP_PORT_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
                return false;
            }
            m_u16ServerPort = static_cast<uint16_t>(u32Port);
            return true;
        }

        /** Sets the read timeout from its string form; returns false if the value is invalid. */
        bool setReadTimeout(const std::string &strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /** Sets the write timeout from its string form; returns false if the value is invalid. */
        bool setWriteTimeout(const std::string &strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /** Sets the read buffer size from its string form; returns false if the value is invalid. */
        bool setReadBufferSize(const std::string &strReadBufferSize) const
        {
            static constexpr uint32_t MAX_BUF = 1460U;
            uint32_t u32Size                  = 0U;
            if (!numeric::str2uint32(strReadBufferSize, u32Size, /*bFailOnZero=*/true)) {
                return false;
            }
            if (u32Size > MAX_BUF) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ReadBufSize out of range [1-"); LOG_UINT32(MAX_BUF); LOG_STRING("]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32ReadBufferSize = u32Size;
            return true;
        }

    private:
        /** Applies the ini settings to this plugin's configuration. */
        bool m_LocalSetParams(const PluginDataSet *psSetParams);
        std::shared_ptr<Enc28J60Net> m_OpenDriver(void) const;

        PluginCommandsMap<Enc28J60NetPlugin> m_mapCmds;
        std::string m_strVersion;

        /**
         * \brief runtime instance identity used for the GUI comm-dump panel
         *        (e.g. "ENC28J60NET" or "ENC28J60NET:1" -- see
         *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
         *        name macro when unset (e.g. standalone construction outside the
         *        script interpreter).
         */
        std::string m_strInstanceName;
        mutable std::string m_strResultData;

        /**
         * \brief when true, CMD returns the raw received bytes as-is instead of
         *        hexlifying them (see ucmdexec::generic_cmd()'s bRawResult parameter);
         *        settable via the ini file's RAW_RESULT key or the CONFIG command's
         *        raw= token (see ucmdexec::RAW_RESULT_INI_KEY / RAW_RESULT_CONFIG_KEY)
         */
        mutable bool m_bRawResult;

        /**
         * \brief CYCLIC caching mode: true (default) validates/parses each CYCLIC entry's
         *        command exactly once for the whole session; false re-resolves and re-validates
         *        every due entry on every tick, needed to track a volatile ("?=") macro used as
         *        one entry's val/id - settable via the ini file's CYCLIC_CACHED key or the CONFIG
         *        command's cached= token (see ucmdexec::CYCLIC_CACHED_INI_KEY / CYCLIC_CACHED_CONFIG_KEY
         *        and ucmdexec::generic_send_cyclic()'s bCached parameter)
         */
        mutable bool m_bCyclicCached;
        bool m_bIsInitialized;
        bool m_bIsEnabled;
        bool m_bIsFaultTolerant;
        bool m_bIsPrivileged;
        std::string m_strArtefactsPath;

        mutable std::string m_strServerIp;
        mutable uint16_t m_u16ServerPort;
        mutable uint32_t m_u32ReadTimeout;
        mutable uint32_t m_u32WriteTimeout;
        mutable uint32_t m_u32ReadBufferSize;

#define ENC28J60NET_PLUGIN_CMD_RECORD(a, ...) bool m_ENC28J60NET_##a(const std::string &args, std::stop_token st) const;
        ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE
#undef ENC28J60NET_PLUGIN_CMD_RECORD
};

#endif /* ENC28J60NET_PLUGIN_HPP */
