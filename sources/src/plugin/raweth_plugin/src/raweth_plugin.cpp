#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "raweth_setup.hpp"
#include "raweth_plugin.hpp"
#include "uRawEth.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>
#include <cstdio>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED RawEthPlugin* pluginEntry()
    {
        return new RawEthPlugin();
    }

    EXPORTED void pluginExit( RawEthPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN INTERNALS                                           //
/////////////////////////////////////////////////////////////////////////////////

namespace
{
    /**
      * \brief Format a MacAddr as "AA:BB:CC:DD:EE:FF" for the INFO command.
    */
    std::string macToString(const RawEth::MacAddr& mac)
    {
        char szBuf[18];
        std::snprintf(szBuf, sizeof(szBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(szBuf);
    }
}




///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////
// Driver factory
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief open a fresh RawEth driver instance against the configured
  *        interface, destination MAC and EtherType.
  *
  * Opened per-invocation (from m_RAWETH_CMD / m_RAWETH_SCRIPT) rather than
  * held open for the plugin's lifetime, the same pattern the TCPIP plugin
  * uses for its TCPIP handle: this keeps a single command's failure (e.g.
  * an interface that went down) from poisoning the state of the next one.
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<RawEth> RawEthPlugin::m_OpenDriver(void) const
{
    if (m_strIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Interface not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<RawEth>(m_strIface, m_destMac, m_u16EtherType, m_bPromiscuous, m_strIface);

    if (!shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Open failed on interface:"); LOG_STRING(m_strIface.c_str()));
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
  *       RAWETH.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_RAWETH_INFO(const std::string& args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(RAWETH_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(RAWETH_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via raw Ethernet frames (AF_PACKET socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the interface, destination MAC, EtherType and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=iface] [d=dest_mac] [t=ethertype] [x=promiscuous] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : RAWETH.CONFIG i=eth0 d=AA:BB:CC:DD:EE:FF t=0x88B5 x=1 r=2000 w=2000 s=1500"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         RAWETH.CONFIG i=eth1 d=FF:FF:FF:FF:FF:FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         t=ethertype defaults to the driver's standard EtherType if never set"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : RAWETH.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : RAWETH.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         RAWETH.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : writes use the dest_mac/ethertype configured via CONFIG; there is no"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         per-call address override"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : RAWETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF, 250 1122 11:22:33:44:55:66/0800"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         RAWETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF, 250 1122 11:22:33:44:55:66/0800 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is an optional per-message dest MAC[/ethertype] override; when"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         omitted, falls back to the dest_mac/ethertype set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[RAWETH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH          =                    # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_IFACE            = eth0               # network interface name to bind the raw socket to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_DEST_MAC         = FF:FF:FF:FF:FF:FF  # destination MAC address for transmitted frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_ETHERTYPE        = 0x88B5             # EtherType value used for transmitted/filtered frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_PROMISCUOUS      = false              # put the interface into promiscuous mode when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_READ_TIMEOUT     = 2000               # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_WRITE_TIMEOUT    = 2000               # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWETH_READ_BUFFER_SIZE = 1518               # size in bytes of the local read buffer (>= max Ethernet frame)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT              = false              # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED           = true               # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;

} /* m_RAWETH_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply interface/destination/EtherType/timeout/
  *        buffer-size settings at runtime, using the same key=value grammar
  *        as the ini-backed m_LocalSetParams() (see raweth_setup.hpp).
  *
  *        Recognised keys: i=iface  d=dest_mac  t=ethertype  x=promiscuous
  *        r=read_tout  w=write_tout  s=recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_RAWETH_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_raweth_set_params(this, args);

} /* m_RAWETH_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open the configured interface and run a single
  *        send/receive command against it, the RawEth analogue of m_TCPIP_CMD.
  *
  *        Mirrors m_TCPIP_CMD's per-call open/use/close lifecycle: the
  *        driver only lives for the duration of this single dispatch, and
  *        command parsing/execution is delegated to the shared
  *        CommScriptCommandValidator / CommScriptCommandInterpreter, the
  *        same as TCPIP/UART.
  *
  * \note Usage example: <br>
  *       RAWETH.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       RAWETH.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
  *
  *       Writes use the destination MAC / EtherType configured via CONFIG
  *       (d=/t= keys) — there is no per-command address override the way a
  *       CAN plugin might expose one, consistent with RawEth's own
  *       xtra_params being used for that purpose at the driver level.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_RAWETH_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<RawEth> {
            // open the RawEth socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);

} /* m_RAWETH_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single interface binding, the RawEth analogue of m_TCPIP_SCRIPT.
  *
  * \note Usage example: <br>
  *       RAWETH.SCRIPT scriptname [|delay]
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_RAWETH_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<RawEth> {
            // open the RawEth socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);

} /* m_RAWETH_SCRIPT() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic RawEth frames.
  *
  * \note The RawEth socket is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Each entry's optional "id" is a per-message destination
  *       override in "AA:BB:CC:DD:EE:FF" or "AA:BB:CC:DD:EE:FF/0800" form (same syntax
  *       RawEth::tout_write()'s xtra_params already accepts — MAC only, or MAC + EtherType;
  *       omitted/empty falls back to the dest_mac/ethertype set via CONFIG); "val" is the
  *       payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       RAWETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF, 250 1122 11:22:33:44:55:66/0800
  *       RAWETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF, 250 1122 11:22:33:44:55:66/0800 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_RAWETH_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<RawEth> {
            // open the RawEth socket (per-invocation; closed by shpDriver's destructor)
            return m_OpenDriver();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
} /* m_RAWETH_CYCLIC() */
