#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "tcpip_setup.hpp"
#include "tcpip_plugin.hpp"
#include "uTcpip.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "TCPIP       |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"                          
#define TCP_HOST                    "TCP_HOST"
#define TCP_PORT                    "TCP_PORT"                 
#define TCP_CONNECT_TIMEOUT         "TCP_CONNECT_TIMEOUT"         
#define TCP_READ_TIMEOUT            "TCP_READ_TIMEOUT"            
#define TCP_WRITE_TIMEOUT           "TCP_WRITE_TIMEOUT"           
#define TCP_READ_BUFFER_SIZE        "TCP_READ_BUFFER_SIZE"


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED TCPIPPlugin* pluginEntry()
    {
        return new TCPIPPlugin();
    }

    EXPORTED void pluginExit( TCPIPPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
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
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
    
} /* doInit() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the de-initialization of modules used by the plugin.
*/
/*--------------------------------------------------------------------------------------------------------*/
void TCPIPPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
    m_strResultData.clear();

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));

} /* doCleanup() */


// ============================================================================
// PARAMETER HANDLING
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Mirrors the KVCAN plugin's handling of the CAN_TX_ID ini entry: pulls the
  * plugin-specific keys out of the ini-backed PluginDataSet and feeds them
  * through the same setter surface the CONFIG command uses, so an ini file
  * and a runtime CONFIG command are always interpreted identically.
  *
  * \note The exact PluginDataSet accessor (getValue() below) is assumed to
  *       match the one used by the KVCAN plugin's m_LocalSetParams(); adjust
  *       the calls if this tree's PluginDataSet exposes a different method
  *       name/signature.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "TCPIP:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? TCPIP_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,       m_strArtefactsPath);
    sSettings.Bind(TCP_HOST,             [this](const std::string& v) { setTcpHost(v); return true; });
    sSettings.Bind(TCP_PORT,             [this](const std::string& v) { return setTcpPort(v); });
    sSettings.Bind(TCP_CONNECT_TIMEOUT,  [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(TCP_READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(TCP_WRITE_TIMEOUT,    [this](const std::string& v) { return setWriteTimeout(v); });
    // Route through the setter so the [1-TCPIP_MAX_BUFLENGTH] range check is
    // applied consistently regardless of whether the value came from the ini
    // file or from the CONFIG command.
    sSettings.Bind(TCP_READ_BUFFER_SIZE, [this](const std::string& v) { return setTcpReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


// ============================================================================
// DRIVER HELPERS
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief open a fresh TCPIP driver instance against the configured
  *        host/port, honouring the configured connect timeout.
  *
  * Opened per-invocation (from m_TCPIP_CMD / m_TCPIP_SCRIPT) rather than
  * held open for the plugin's lifetime, the same pattern the KVCAN plugin
  * uses for its SocketKVCAN handle: this keeps a single command's failure
  * (e.g. an unreachable peer) from poisoning the state of the next one.
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<TCPIP> TCPIPPlugin::m_OpenDriver(void) const
{
    if (m_strTcpHost.empty() || m_u16TcpPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host/port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<TCPIP>(m_strTcpHost, m_u16TcpPort, m_u32ConnectTimeout,
                                              m_strTcpHost + ":" + std::to_string(m_u16TcpPort));

    if (!shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect failed:"); LOG_STRING(m_strTcpHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16TcpPort));
        return nullptr;
    }

    return shpDriver;

} /* m_OpenDriver() */


// ============================================================================
// COMMAND HANDLERS
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       TCPIP.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_INFO(const std::string& args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(TCPIP_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(TCPIP_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via TCP/IP (client socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the TCP host, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [c=connect_tout] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : TCPIP.CONFIG h=192.168.1.10 p=5000 c=3000 r=2000 w=2000 s=512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         TCPIP.CONFIG h=localhost p=8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : TCPIP.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : TCPIP.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         TCPIP.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection to host:port is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : TCPIP.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         TCPIP.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id has no meaning here (single-peer stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();

    return true;

} /* m_TCPIP_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply host/port/timeout/buffer-size settings at
  *        runtime, using the same key=value grammar as the ini-backed
  *        m_LocalSetParams() (see tcpip_setup.hpp).
  *
  *        Recognised keys: h=host  p=port  c=connect_tout  r=read_tout
  *        w=write_tout  s=recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_tcp_set_params(this, args);

} /* m_TCPIP_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open a connection to the configured host:port and
  *        run a single send/receive command against it, the TCPIP analogue
  *        of m_UART_CMD.
  *
  *        Mirrors m_UART_CMD's per-call open/use/close lifecycle: the
  *        driver only lives for the duration of this single dispatch, and
  *        command parsing/execution is delegated to the shared
  *        CommScriptCommandValidator / CommScriptCommandInterpreter, the
  *        same as UART.
  *
  * \note Usage example: <br>
  *       TCPIP.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       TCPIP.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<TCPIP> {
            // open the TCPIP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);

} /* m_TCPIP_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single connection, the TCPIP analogue of m_UART_SCRIPT.
  *
  * \note Usage example: <br>
  *       TCPIP.SCRIPT scriptname [|delay]
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<TCPIP> {
            // open the TCPIP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);

} /* m_TCPIP_SCRIPT() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic TCPIP messages.
  *
  * \note The TCP connection is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). TCPIP is a single-peer stream with no addressable
  *       channels, so each entry's optional "id" is never sent on the wire — omit it — and
  *       "val" is the payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       TCPIP.CYCLIC 100 AABBCCDD, 250 06
  *       TCPIP.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<TCPIP> {
            // open the TCPIP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st);

} /* m_TCPIP_CYCLIC() */
