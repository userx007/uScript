#ifndef UDP_PLUGIN_HPP
#define UDP_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uUdp.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define UDP_PLUGIN_VERSION    "1.0.0.0"
#define UDP_PLUGIN_NAME       "UDP"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// UDP_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef UDP_GET_BLOCKING
#define UDP_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define UDP_PLUGIN_COMMANDS_CONFIG_TABLE    \
UDP_PLUGIN_CMD_RECORD( INFO               ) \
UDP_PLUGIN_CMD_RECORD( CONFIG             ) \
UDP_PLUGIN_CMD_RECORD( CMD                ) \
UDP_PLUGIN_CMD_RECORD( SCRIPT             ) \
UDP_PLUGIN_CMD_RECORD( CYCLIC             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief UDP plugin class definition.
  *
  * Wraps the UDP datagram-client driver (see uUdp.hpp) and exposes it
  * through the standard PluginInterface dispatch model. UDP is datagram
  * based and message-preserving, like KVCAN and unlike the byte-stream
  * TCPIP plugin, so this plugin's shape mirrors the KVCAN plugin more
  * closely than the TCPIP one:
  *
  * Extra behaviour vs the TCPIP plugin:
  *   CMD accepts an optional leading "d:host:port" override token (numeric
  *   literals only, e.g. "d:192.168.1.20:9000" or "d:[::1]:9000") ahead of
  *   the payload, the plugin-level equivalent of uUdp.hpp's per-call
  *   xtra_params destination override. Without it, the datagram goes to the
  *   default peer set by CONFIG's h=/p= keys — the same "default unless
  *   overridden for one call" pattern the KVCAN plugin uses for its default
  *   RX filter vs a per-call xtra_params override.
  *
  * No FILTER command: like TCPIP, source filtering of incoming datagrams is
  * done implicitly by the kernel against the connect()ed default peer —
  * there is no per-plugin acceptance-filter table to manage as there is on
  * a CAN socket.
*/
class UDPPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        UDPPlugin() : m_strVersion(UDP_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_u16UdpPort(0U)
                    , m_u32ConnectTimeout(UDP::UDP_CONNECT_DEFAULT_TIMEOUT)
                    , m_u32ReadTimeout(UDP::UDP_READ_DEFAULT_TIMEOUT)
                    , m_u32WriteTimeout(UDP::UDP_WRITE_DEFAULT_TIMEOUT)
                    , m_u32ReadBufferSize(static_cast<uint32_t>(UDP::UDP_SAFE_PAYLOAD))
        {
            #define UDP_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<UDPPlugin>{&UDPPlugin::m_UDP_##a, UDP_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            UDP_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  UDP_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~UDPPlugin() = default;

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

            if (true == generic_setparams<UDPPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<UDPPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<UDPPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<UDPPlugin> *getMap(void) const
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
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitly after loading the plugin
        */
        bool doInit(void *pvUserData);

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
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitly before closing/freeing the shared library
        */
        void doCleanup(void);

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
          * \brief get UDP default peer host (hostname or IP literal)
        */
        const char *getUdpHost (void) const
        {
            return m_strUdpHost.c_str();
        }

        /**
          * \brief set UDP default peer host (e.g. "192.168.1.10", "myhost.local")
        */
        void setUdpHost (const std::string& strUdpHost) const
        {
            m_strUdpHost.assign(strUdpHost);
        }

        /**
          * \brief get UDP default peer port
        */
        uint16_t getUdpPort (void) const
        {
            return m_u16UdpPort;
        }

        /**
          * \brief set UDP default peer port.
          *        Accepts decimal strings in the valid UDP port range [1-65535].
        */
        bool setUdpPort (const std::string& strUdpPort) const
        {
            static constexpr uint32_t UDP_PORT_MAX = 65535U;

            uint32_t u32Port = 0U;
            if (false == numeric::str2uint32(strUdpPort, u32Port)) {
                return false;
            }
            if (u32Port == 0U || u32Port > UDP_PORT_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("UDP |");
                          LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
                return false;
            }

            m_u16UdpPort = static_cast<uint16_t>(u32Port);
            return true;
        }

        /**
          * \brief set UDP connect timeout (milliseconds, 0 = use driver default)
          * \note Accepted for API/CONFIG-grammar symmetry with TCPIP/KVCAN;
          *       connect()ing a UDP socket does not handshake, so this is not
          *       expected to matter in practice (see uUdp.hpp class docs).
        */
        bool setConnectTimeout (const std::string& strConnectTimeout) const
        {
            return numeric::str2uint32(strConnectTimeout, m_u32ConnectTimeout);
        }

        /**
          * \brief set UDP read timeout (milliseconds, 0 = use driver default)
        */
        bool setReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set UDP write timeout (milliseconds, 0 = use driver default)
        */
        bool setWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set UDP read buffer size
          * \note Valid range is 1–UDP_MAX_DGRAM_LEN bytes. Defaults to
          *       UDP_SAFE_PAYLOAD (fits one standard-MTU Ethernet frame
          *       without IP fragmentation); raise it only if your peer is
          *       known to send larger datagrams, since undersizing silently
          *       truncates per normal UDP semantics (see uUdp.hpp).
        */
        bool setUdpReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t UDP_BUF_MAX = static_cast<uint32_t>(UDP::UDP_MAX_DGRAM_LEN);
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > UDP_BUF_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("UDP |");
                          LOG_STRING("ReadBufSize out of range [1-65507]:"); LOG_UINT32(u32Size));
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
          * \brief helper: open a fresh UDP driver instance against the
          *        configured host/port, using the configured connect timeout.
          *        Returns nullptr (and logs) on failure, mirroring the way
          *        m_KVCAN_CMD/m_KVCAN_SCRIPT open a socket per invocation
          *        in the KVCAN plugin.
          * \note  Returns the concrete TCPIP type (rather than ICommDriver)
          *        so it can be handed directly to
          *        CommScriptCommandInterpreter<UDP> / CommScriptClient<UDP>,
          *        the same way UART's RAII constructor result is used in
          *        m_UART_CMD/m_UART_SCRIPT.
        */
        std::shared_ptr<UDP> m_OpenDriver (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<UDPPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

        /**
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

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
          * \brief UDP default peer host (hostname or IP literal, e.g. "192.168.1.10")
        */
        mutable std::string m_strUdpHost;

        /**
          * \brief UDP default peer port
        */
        mutable uint16_t m_u16UdpPort;

        /**
          * \brief UDP connect timeout in milliseconds (see setConnectTimeout note)
        */
        mutable uint32_t m_u32ConnectTimeout;

        /**
          * \brief UDP read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief UDP write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for UDP read operations (max UDP_MAX_DGRAM_LEN bytes)
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define UDP_PLUGIN_CMD_RECORD(a, ...)  bool m_UDP_##a ( const std::string& args, std::stop_token st ) const;
        UDP_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UDP_PLUGIN_CMD_RECORD
};

#endif /* UDP_PLUGIN_HPP */
