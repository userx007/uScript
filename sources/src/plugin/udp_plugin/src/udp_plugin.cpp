#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "udp_setup.hpp"
#include "udp_plugin.hpp"

#include "uUdp.hpp"

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
    EXPORTED UDPPlugin* pluginEntry()
    {
        return new UDPPlugin();
    }

    EXPORTED void pluginExit( UDPPlugin *ptrPlugin)
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
  * \brief open a fresh UDP driver instance against the configured default
  *        peer host/port.
  *
  * Opened per-invocation (from m_UDP_CMD / m_UDP_SCRIPT) rather than held
  * open for the plugin's lifetime — the same pattern the KVCAN plugin uses
  * for its SocketKVCAN handle and the UDP plugin uses for its TCP socket.
  * connect()ing a UDP socket does not handshake, so unlike UDP this call
  * essentially never blocks on the network; it can still fail synchronously
  * (e.g. invalid address family, resolution failure).
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<UDP> UDPPlugin::m_OpenDriver(void) const
{
    if (m_strUdpHost.empty() || m_u16UdpPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host/port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<UDP>(m_strUdpHost, m_u16UdpPort, m_u32ConnectTimeout,
                                            m_strUdpHost + ":" + std::to_string(m_u16UdpPort));

    if (!shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Open failed:"); LOG_STRING(m_strUdpHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16UdpPort));
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
  *       UDP.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_INFO(const std::string& args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(UDP_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(UDP_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via UDP (connected datagram socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the default UDP peer, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [c=connect_tout] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : UDP.CONFIG h=192.168.1.10 p=5000 c=3000 r=2000 w=2000 s=512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UDP.CONFIG h=localhost p=8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : UDP.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : UDP.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UDP.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh socket connect()ed to host:port is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : UDP.CYCLIC 100 AABBCCDD 192.168.1.10:5000, 250 1122 192.168.1.11:5000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UDP.CYCLIC 100 AABBCCDD 192.168.1.10:5000, 250 1122 192.168.1.11:5000 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is an optional per-message host:port override; when omitted,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         falls back to the peer set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[UDP]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH       =            # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_HOST             = 127.0.0.1  # default remote peer to send to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_PORT             = 5000       # default remote UDP port to send to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_CONNECT_TIMEOUT  = 3000       # socket connect/bind timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_READ_TIMEOUT     = 2000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_WRITE_TIMEOUT    = 2000       # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UDP_READ_BUFFER_SIZE = 1024       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT           = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED        = true       # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;

} /* m_UDP_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply default-peer/timeout/buffer-size settings
  *        at runtime, using the same key=value grammar as the ini-backed
  *        m_LocalSetParams() (see udp_setup.hpp).
  *
  *        Recognised keys: h=host  p=port  c=connect_tout  r=read_tout
  *        w=write_tout  s=recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_udp_set_params(this, args);

} /* m_UDP_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open a socket against the configured default peer
  *        and run a single send/receive command against it, the UDP
  *        analogue of m_UART_CMD.
  *
  *        Mirrors m_UART_CMD's per-call open/use/close lifecycle: the
  *        driver only lives for the duration of this single dispatch, and
  *        command parsing/execution is delegated to the shared
  *        CommScriptCommandValidator / CommScriptCommandInterpreter, the
  *        same as UART.
  *
  * \note Usage example: <br>
  *       UDP.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       UDP.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UDP> {
            // open the UDP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);

} /* m_UDP_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single default-peer socket, the UDP analogue of m_UART_SCRIPT.
  *
  * \note Usage example: <br>
  *       UDP.SCRIPT scriptname [|delay]
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UDP> {
            // open the UDP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);

} /* m_UDP_SCRIPT() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic UDP messages.
  *
  * \note The UDP socket is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Each entry's optional "id" is a per-message destination
  *       override in "host:port" form (same syntax UDP::tout_write()'s xtra_params already
  *       accepts — omitted/empty falls back to the peer set via CONFIG/open()) and "val" is the
  *       payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       UDP.CYCLIC 100 AABBCCDD 192.168.1.10:5000, 250 1122 192.168.1.11:5000
  *       UDP.CYCLIC 100 AABBCCDD 192.168.1.10:5000, 250 1122 192.168.1.11:5000 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UDP> {
            // open the UDP socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);

} /* m_UDP_CYCLIC() */
