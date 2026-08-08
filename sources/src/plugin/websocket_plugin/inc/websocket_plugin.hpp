#ifndef WEBSOCKET_PLUGIN_HPP
#define WEBSOCKET_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uWebSocket.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define WEBSOCKET_PLUGIN_VERSION    "1.0.0.0"
#define WEBSOCKET_PLUGIN_NAME       "WEBSOCKET"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// WEBSOCKET_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef WEBSOCKET_GET_BLOCKING
#define WEBSOCKET_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define WEBSOCKET_PLUGIN_COMMANDS_CONFIG_TABLE    \
WEBSOCKET_PLUGIN_CMD_RECORD( INFO               ) \
WEBSOCKET_PLUGIN_CMD_RECORD( CONFIG             ) \
WEBSOCKET_PLUGIN_CMD_RECORD( CMD                ) \
WEBSOCKET_PLUGIN_CMD_RECORD( SCRIPT             ) \
WEBSOCKET_PLUGIN_CMD_RECORD( CYCLIC             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief WEBSOCKET plugin class definition.
  *
  * Wraps the WebSocket (RFC 6455) client driver and exposes it through the
  * standard PluginInterface dispatch model, the same shape as the TCPIP and
  * UDP plugins: this class only owns configuration state and command
  * dispatch, all of the protocol logic (HTTP Upgrade handshake, frame
  * masking/defragmentation, ping/pong/close handling) lives in the driver
  * (see uWebSocket.hpp) under src/lib/drivers/websocket.
  *
  * Like TCPIP, WebSocket is ultimately a byte stream once framing is
  * stripped away, so there is no FILTER command here: acceptance filtering
  * is a CAN-hardware concept with no WebSocket equivalent. The plugin
  * surface is therefore INFO / CONFIG / CMD / SCRIPT / CYCLIC only.
*/
class WEBSOCKETPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        WEBSOCKETPlugin() : m_strVersion(WEBSOCKET_PLUGIN_VERSION)
                    , m_strResultData()
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strWsPath("/")
                    , m_u16WsPort(0U)
                    , m_u32ConnectTimeout(WebSocket::WS_CONNECT_DEFAULT_TIMEOUT)
                    , m_u32ReadTimeout(WebSocket::WS_READ_DEFAULT_TIMEOUT)
                    , m_u32WriteTimeout(WebSocket::WS_WRITE_DEFAULT_TIMEOUT)
                    , m_u32ReadBufferSize(WebSocket::WS_MAX_BUFLENGTH)
        {
            #define WEBSOCKET_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<WEBSOCKETPlugin>{&WEBSOCKETPlugin::m_WEBSOCKET_##a, WEBSOCKET_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            WEBSOCKET_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  WEBSOCKET_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~WEBSOCKETPlugin() = default;

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

            if (true == generic_setparams<WEBSOCKETPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<WEBSOCKETPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<WEBSOCKETPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<WEBSOCKETPlugin> *getMap(void) const
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
          * \brief get WebSocket remote host (hostname or IP literal)
        */
        const char *getWsHost (void) const
        {
            return m_strWsHost.c_str();
        }

        /**
          * \brief set WebSocket remote host (e.g. "192.168.1.10", "echo.example.com")
        */
        void setWsHost (const std::string& strWsHost) const
        {
            m_strWsHost.assign(strWsHost);
        }

        /**
          * \brief get WebSocket request-target path (e.g. "/ws")
        */
        const char *getWsPath (void) const
        {
            return m_strWsPath.c_str();
        }

        /**
          * \brief set WebSocket request-target path. Accepts anything starting with '/';
          *        an empty or non-'/'-prefixed value is rejected (the driver would reject
          *        it anyway, but validating here gives an immediate CONFIG error).
        */
        bool setWsPath (const std::string& strWsPath) const
        {
            if (strWsPath.empty() || strWsPath.front() != '/') {
                LOG_PRINT(LOG_ERROR, LOG_STRING("WEBSOCKET |");
                          LOG_STRING("Path must start with '/':"); LOG_STRING(strWsPath));
                return false;
            }
            m_strWsPath.assign(strWsPath);
            return true;
        }

        /**
          * \brief get WebSocket remote port
        */
        uint16_t getWsPort (void) const
        {
            return m_u16WsPort;
        }

        /**
          * \brief set WebSocket remote port.
          *        Accepts decimal strings in the valid TCP port range [1-65535].
        */
        bool setWsPort (const std::string& strWsPort) const
        {
            static constexpr uint32_t WS_PORT_MAX = 65535U;

            uint32_t u32Port = 0U;
            if (false == numeric::str2uint32(strWsPort, u32Port)) {
                return false;
            }
            if (u32Port == 0U || u32Port > WS_PORT_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("WEBSOCKET |");
                          LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
                return false;
            }

            m_u16WsPort = static_cast<uint16_t>(u32Port);
            return true;
        }

        /**
          * \brief get the configured Sec-WebSocket-Protocol value (empty = header omitted)
        */
        const char *getWsSubprotocol (void) const
        {
            return m_strWsSubprotocol.c_str();
        }

        /**
          * \brief set the Sec-WebSocket-Protocol request header value (empty omits the header)
        */
        void setWsSubprotocol (const std::string& strWsSubprotocol) const
        {
            m_strWsSubprotocol.assign(strWsSubprotocol);
        }

        /**
          * \brief set WebSocket connect+handshake timeout (milliseconds, 0 = use driver default)
        */
        bool setConnectTimeout (const std::string& strConnectTimeout) const
        {
            return numeric::str2uint32(strConnectTimeout, m_u32ConnectTimeout);
        }

        /**
          * \brief set WebSocket read timeout (milliseconds, 0 = use driver default)
        */
        bool setReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set WebSocket write timeout (milliseconds, 0 = use driver default)
        */
        bool setWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set WebSocket read buffer size
          * \note Valid range is 1-WS_MAX_BUFLENGTH bytes (delimiter/token modes
          *       assemble into a buffer of this size; Exact mode truncates a
          *       single defragmented message to this size too).
        */
        bool setWsReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t WS_BUF_MAX = static_cast<uint32_t>(WebSocket::WS_MAX_BUFLENGTH);
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > WS_BUF_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("WEBSOCKET |");
                          LOG_STRING("ReadBufSize out of range [1-4096]:"); LOG_UINT32(u32Size));
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
          * \brief helper: open a fresh WebSocket driver instance against the
          *        configured host/port/path, using the configured connect
          *        timeout. Returns nullptr (and logs) on failure, mirroring
          *        m_OpenDriver() in the TCPIP plugin.
          * \note  Returns the concrete WebSocket type (rather than ICommDriver)
          *        so it can be handed directly to
          *        CommScriptCommandInterpreter<WebSocket> / CommScriptClient<WebSocket>.
        */
        std::shared_ptr<WebSocket> m_OpenDriver (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<WEBSOCKETPlugin> m_mapCmds;

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
          * \brief WebSocket remote host (hostname or IP literal)
        */
        mutable std::string m_strWsHost;

        /**
          * \brief WebSocket request-target path, e.g. "/ws" (defaults to "/")
        */
        mutable std::string m_strWsPath;

        /**
          * \brief WebSocket remote port
        */
        mutable uint16_t m_u16WsPort;

        /**
          * \brief Sec-WebSocket-Protocol request header value (empty omits the header)
        */
        mutable std::string m_strWsSubprotocol;

        /**
          * \brief WebSocket connect+handshake timeout in milliseconds
        */
        mutable uint32_t m_u32ConnectTimeout;

        /**
          * \brief WebSocket read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief WebSocket write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for WebSocket read operations (max WS_MAX_BUFLENGTH bytes)
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define WEBSOCKET_PLUGIN_CMD_RECORD(a, ...)  bool m_WEBSOCKET_##a ( const std::string& args, std::stop_token st ) const;
        WEBSOCKET_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  WEBSOCKET_PLUGIN_CMD_RECORD
};

#endif /* WEBSOCKET_PLUGIN_HPP */
