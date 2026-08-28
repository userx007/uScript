#ifndef TCPIP_PLUGIN_HPP
#define TCPIP_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uTcpip.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define TCPIP_PLUGIN_VERSION    "1.0.0.0"
#define TCPIP_PLUGIN_NAME       "TCPIP"


/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// TCPIP_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef TCPIP_GET_BLOCKING
#define TCPIP_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define TCPIP_PLUGIN_COMMANDS_CONFIG_TABLE    \
TCPIP_PLUGIN_CMD_RECORD( INFO               ) \
TCPIP_PLUGIN_CMD_RECORD( CONFIG             ) \
TCPIP_PLUGIN_CMD_RECORD( CMD                ) \
TCPIP_PLUGIN_CMD_RECORD( SCRIPT             ) \
TCPIP_PLUGIN_CMD_RECORD( CYCLIC             ) \

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
  * \brief TCPIP plugin class definition.
  *
  * Wraps the TCPIP TCP-client driver and exposes it through the standard
  * PluginInterface dispatch model. Connects to a single remote host:port
  * over TCP (see uTcpip.hpp).
  *
  * Unlike KVCAN, TCPIP is a byte stream rather than frame based, so there
  * is no FILTER command here: acceptance filtering is a CAN-hardware
  * concept with no TCP-stream equivalent. The plugin surface is therefore
  * INFO / CONFIG / CMD / SCRIPT only.
*/
class TCPIPPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        TCPIPPlugin() : m_strVersion(TCPIP_PLUGIN_VERSION)
                    , m_strInstanceName(TCPIP_PLUGIN_NAME)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_bCyclicCached(true)
                    , m_u16TcpPort(0U)
                    , m_u32ConnectTimeout(TCPIP::TCPIP_CONNECT_DEFAULT_TIMEOUT)
                    , m_u32ReadTimeout(TCPIP::TCPIP_READ_DEFAULT_TIMEOUT)
                    , m_u32WriteTimeout(TCPIP::TCPIP_WRITE_DEFAULT_TIMEOUT)
                    , m_u32ReadBufferSize(TCPIP::TCPIP_MAX_BUFLENGTH)
        {
            #define TCPIP_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<TCPIPPlugin>{&TCPIPPlugin::m_TCPIP_##a, TCPIP_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            TCPIP_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  TCPIP_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~TCPIPPlugin() = default;

        /**
          * \brief get the plugin initialization status
        */
        bool isInitialized( void ) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool isEnabled (void) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<TCPIPPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<TCPIPPlugin>(this, psGetParams);
        }

        /**
          * \brief perform the initialization of modules used by the plugin.
          *
          * Unlike KVCAN — where doInit() has nothing to open (the SocketKVCAN
          * interface is opened lazily, per command, against m_strCanIface) — TCPIP
          * follows the same lazy-open convention: doInit() only records the plugin
          * as ready to accept setParams()/dispatch() calls. The actual TCP connect
          * happens per invocation in m_OpenDriver(), called from m_TCPIP_CMD /
          * m_TCPIP_SCRIPT, so a stale or unreachable host configured at load time
          * does not fail plugin initialization itself.
        */
        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = true;
            return m_bIsInitialized;

        } /* doInit() */


        /**
          * \brief perform the de-initialization of modules used by the plugin.
        */
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
            m_strResultData.clear();

        } /* doCleanup() */

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<TCPIPPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<TCPIPPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion(void) const
        {
            return m_strVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& getData(void) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void resetData(void) const
        {
            m_strResultData.clear();
        }
        
        /**
          * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
        */
        bool setRawResult (const std::string& strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /**
          * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
        */
        bool setCyclicCached (const std::string& strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
        }

        /**
          * \brief get fault tolerant flag status
        */
        bool isFaultTolerant (void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged (void) const
        {
            return m_bIsPrivileged;
        }

        /**
          * \brief get TCPIP remote host (hostname or IP literal)
        */
        const char *getTcpHost (void) const
        {
            return m_strTcpHost.c_str();
        }

        /**
          * \brief set TCPIP remote host (e.g. "192.168.1.10", "myhost.local")
        */
        void setTcpHost (const std::string& strTcpHost) const
        {
            m_strTcpHost.assign(strTcpHost);
        }

        /**
          * \brief get TCPIP remote port
        */
        uint16_t getTcpPort (void) const
        {
            return m_u16TcpPort;
        }

        /**
          * \brief set TCPIP remote port.
          *        Accepts decimal strings in the valid TCP port range [1-65535].
        */
        bool setTcpPort (const std::string& strTcpPort) const
        {
            static constexpr uint32_t TCP_PORT_MAX = 65535U;

            uint32_t u32Port = 0U;
            if (false == numeric::str2uint32(strTcpPort, u32Port)) {
                return false;
            }
            if (u32Port == 0U || u32Port > TCP_PORT_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("TCPIP |");
                          LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
                return false;
            }

            m_u16TcpPort = static_cast<uint16_t>(u32Port);
            return true;
        }

        /**
          * \brief set TCPIP connect timeout (milliseconds, 0 = use driver default)
        */
        bool setConnectTimeout (const std::string& strConnectTimeout) const
        {
            return numeric::str2uint32(strConnectTimeout, m_u32ConnectTimeout);
        }

        /**
          * \brief set TCPIP read timeout (milliseconds, 0 = use driver default)
        */
        bool setReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set TCPIP write timeout (milliseconds, 0 = use driver default)
        */
        bool setWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set TCPIP read buffer size
          * \note Valid range is 1–TCPIP_MAX_BUFLENGTH bytes (delimiter/token
          *       modes assemble into a buffer of this size; Exact mode caps
          *       a single recv(2) at this size too).
        */
        bool setTcpReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t TCPIP_BUF_MAX = static_cast<uint32_t>(TCPIP::TCPIP_MAX_BUFLENGTH);
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > TCPIP_BUF_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("TCPIP |");
                          LOG_STRING("ReadBufSize out of range [1-256]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32ReadBufferSize = u32Size;
            return true;
        }

    private:

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief helper: open a fresh TCPIP driver instance against the
          *        configured host/port, using the configured connect timeout.
          *        Returns nullptr (and logs) on failure, mirroring the way
          *        m_KVCAN_CMD/m_KVCAN_SCRIPT open a socket per invocation
          *        in the KVCAN plugin.
          * \note  Returns the concrete TCPIP type (rather than ICommDriver)
          *        so it can be handed directly to
          *        CommScriptCommandInterpreter<TCPIP> / CommScriptClient<TCPIP>,
          *        the same way UART's RAII constructor result is used in
          *        m_UART_CMD/m_UART_SCRIPT.
        */
        std::shared_ptr<TCPIP> m_OpenDriver (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<TCPIPPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;


        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "TCPIP" or "TCPIP:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
        /**
          * \brief data returned by plugin
        */
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

        /**
          * \brief plugin initialization status
        */
        bool m_bIsInitialized;

        /**
          * \brief plugin enabling status
        */
        bool m_bIsEnabled;

        /**
          * \brief plugin fault tolerant mode
        */
        bool m_bIsFaultTolerant;

        /**
          * \brief plugin is privileged
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief TCPIP remote host (hostname or IP literal, e.g. "192.168.1.10")
        */
        mutable std::string m_strTcpHost;

        /**
          * \brief TCPIP remote port
        */
        mutable uint16_t m_u16TcpPort;

        /**
          * \brief TCPIP connect timeout in milliseconds
        */
        mutable uint32_t m_u32ConnectTimeout;

        /**
          * \brief TCPIP read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief TCPIP write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for TCPIP read operations (max TCPIP_MAX_BUFLENGTH bytes)
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define TCPIP_PLUGIN_CMD_RECORD(a, ...)  bool m_TCPIP_##a ( const std::string& args, std::stop_token st ) const;
        TCPIP_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  TCPIP_PLUGIN_CMD_RECORD
};

#endif /* TCPIP_PLUGIN_HPP */
