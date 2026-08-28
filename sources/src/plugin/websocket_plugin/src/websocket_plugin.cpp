#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "websocket_setup.hpp"
#include "websocket_plugin.hpp"
#include "uWebSocket.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED WEBSOCKETPlugin* pluginEntry()
    {
        return new WEBSOCKETPlugin();
    }

    EXPORTED void pluginExit( WEBSOCKETPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Driver factory
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief open a fresh WebSocket driver instance against the configured
  *        host/port/path, honouring the configured connect timeout.
  *
  * Opened per-invocation (from m_WEBSOCKET_CMD / m_WEBSOCKET_SCRIPT /
  * m_WEBSOCKET_CYCLIC) rather than held open for the plugin's lifetime, the
  * same pattern the TCPIP plugin uses for its TCPIP handle: this keeps a
  * single command's failure (e.g. an unreachable peer or a rejected
  * handshake) from poisoning the state of the next one.
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<WebSocket> WEBSOCKETPlugin::m_OpenDriver(void) const
{
    if (m_strWsHost.empty() || m_u16WsPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host/port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<WebSocket>(m_strWsHost, m_u16WsPort, m_strWsPath, m_u32ConnectTimeout,
                                                  m_strWsHost + ":" + std::to_string(m_u16WsPort) + m_strWsPath,
                                                  m_strWsSubprotocol);

    if (!shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect/handshake failed:"); LOG_STRING(m_strWsHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16WsPort); LOG_STRING(m_strWsPath.c_str()));
        return nullptr;
    }

    return shpDriver;

} /* m_OpenDriver() */


/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       WEBSOCKET.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_WEBSOCKET_INFO(const std::string& args, std::stop_token st) const
{
    (void)st;

    // expected no arguments
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(WEBSOCKET_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(WEBSOCKET_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via WebSocket (RFC 6455 client)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the WS host, port, path and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [u=path] [o=subprotocol] [c=connect_tout] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : WEBSOCKET.CONFIG h=192.168.1.10 p=8080 u=/ws c=3000 r=2000 w=2000 s=1024"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         WEBSOCKET.CONFIG h=echo.example.com p=443 u=/"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         path defaults to '/' and must start with '/'"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : WEBSOCKET.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : WEBSOCKET.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         WEBSOCKET.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection + handshake to host:port/path is opened for CMD and closed once it completes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : every send goes out as a single Binary WebSocket frame"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : WEBSOCKET.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         WEBSOCKET.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id has no meaning here (single-peer stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[WEBSOCKET]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH      =            # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_HOST             = 127.0.0.1  # WebSocket server host to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_PORT             = 8080       # WebSocket server port to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_PATH             = /          # HTTP upgrade request path"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_SUBPROTOCOL      =            # Sec-WebSocket-Protocol value to request (empty = none)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_CONNECT_TIMEOUT  = 3000       # connect timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_READ_TIMEOUT     = 2000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_WRITE_TIMEOUT    = 2000       # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WS_READ_BUFFER_SIZE = 1024       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT          = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED       = true       # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;

} /* m_WEBSOCKET_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply host/port/path/subprotocol/timeout/buffer-size
  *        settings at runtime, using the same key=value grammar as the
  *        ini-backed m_LocalSetParams() (see websocket_setup.hpp).
  *
  *        Recognised keys: h=host  p=port  u=path  o=subprotocol
  *        c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_WEBSOCKET_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_websocket_set_params(this, args);

} /* m_WEBSOCKET_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open a connection + handshake to the configured
  *        host:port/path and run a single send/receive command against it,
  *        the WEBSOCKET analogue of m_TCPIP_CMD.
  *
  *        Mirrors m_TCPIP_CMD's per-call open/use/close lifecycle: the
  *        driver only lives for the duration of this single dispatch, and
  *        command parsing/execution is delegated to the shared
  *        CommScriptCommandValidator / CommScriptCommandInterpreter, the
  *        same as TCPIP/UART.
  *
  * \note Usage example: <br>
  *       WEBSOCKET.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       WEBSOCKET.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
*/
/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_WEBSOCKET_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<WebSocket> {
            // open the WebSocket connection (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);

} /* m_WEBSOCKET_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single connection, the WEBSOCKET analogue of m_TCPIP_SCRIPT.
  *
  * \note Usage example: <br>
  *       WEBSOCKET.SCRIPT scriptname [|delay]
*/
/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_WEBSOCKET_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<WebSocket> {
            // open the WebSocket connection (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);

} /* m_WEBSOCKET_SCRIPT() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic WEBSOCKET messages.
  *
  * \note The connection is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). WEBSOCKET is a single-peer stream with no addressable
  *       channels, so each entry's optional "id" is never sent on the wire - omit it - and
  *       "val" is the payload as a plain hex string (e.g. "AABBCCDD"), sent as a Binary frame.
  *
  * \note Usage example:
  *       WEBSOCKET.CYCLIC 100 AABBCCDD, 250 06
  *       WEBSOCKET.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_WEBSOCKET_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<WebSocket> {
            // open the WebSocket connection (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);

} /* m_WEBSOCKET_CYCLIC() */
