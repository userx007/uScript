#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "raweth_setup.hpp"
#include "raweth_plugin.hpp"
#include "uRawEth.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>
#include <cstdio>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "RAWETH PLUGIN |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define RAWETH_IFACE                "RAWETH_IFACE"
#define RAWETH_DEST_MAC             "RAWETH_DEST_MAC"
#define RAWETH_ETHERTYPE            "RAWETH_ETHERTYPE"
#define RAWETH_PROMISCUOUS          "RAWETH_PROMISCUOUS"
#define RAWETH_READ_TIMEOUT         "RAWETH_READ_TIMEOUT"
#define RAWETH_WRITE_TIMEOUT        "RAWETH_WRITE_TIMEOUT"
#define RAWETH_READ_BUFFER_SIZE     "RAWETH_READ_BUFFER_SIZE"


///////////////////////////////////////////////////////////////////
//                          LOCAL HELPERS                        //
///////////////////////////////////////////////////////////////////

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
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
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


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the initialization of modules used by the plugin.
  *
  * Follows the same lazy-open convention as TCPIP: doInit() only records the
  * plugin as ready to accept setParams()/dispatch() calls. The actual
  * socket()/bind() happens per invocation in m_OpenDriver(), called from
  * m_RAWETH_CMD / m_RAWETH_SCRIPT, so an interface that is down or renamed
  * at load time does not fail plugin initialization itself.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;

} /* doInit() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the de-initialization of modules used by the plugin.
*/
/*--------------------------------------------------------------------------------------------------------*/
void RawEthPlugin::doCleanup(void)
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
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and
  * feeds them through the same setter surface the CONFIG command uses
  * (see m_TCPIP_CONFIG's counterpart, m_RAWETH_CONFIG), so an ini file and
  * a runtime CONFIG command are always interpreted identically.
  *
  * \note The exact PluginDataSet accessor (getValue() below) is assumed to
  *       match the one used by the TCPIP plugin's m_LocalSetParams(); adjust
  *       the calls if this tree's PluginDataSet exposes a different method
  *       name/signature.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool RawEthPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            // Use find() for each key — single lookup instead of count()+at().

            auto it = psSetParams->mapSettings.find(ARTEFACTS_PATH);
            if (it != psSetParams->mapSettings.end()) {
                m_strArtefactsPath = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            it = psSetParams->mapSettings.find(RAWETH_IFACE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setIface(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Iface :"); LOG_STRING(m_strIface));
            }

            it = psSetParams->mapSettings.find(RAWETH_DEST_MAC);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setDestMac(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("DestMac :"); LOG_STRING(macToString(m_destMac)));
            }

            it = psSetParams->mapSettings.find(RAWETH_ETHERTYPE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setEtherType(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("EtherType :"); LOG_UINT32(m_u16EtherType));
            }

            it = psSetParams->mapSettings.find(RAWETH_PROMISCUOUS);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setPromiscuous(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Promiscuous :"); LOG_UINT32(m_bPromiscuous ? 1U : 0U));
            }

            it = psSetParams->mapSettings.find(RAWETH_READ_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setReadTimeout(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            it = psSetParams->mapSettings.find(RAWETH_WRITE_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setWriteTimeout(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            it = psSetParams->mapSettings.find(RAWETH_READ_BUFFER_SIZE);
            if (it != psSetParams->mapSettings.end()) {
                // Route through the setter so the [1-RAWETH_MAX_BUFLENGTH]
                // range check is applied consistently regardless of whether
                // the value came from the ini file or from the CONFIG command.
                if (false == setRawEthReadBufferSize(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_u32RawEthReadBufferSize));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;

} /* m_LocalSetParams() */


// ============================================================================
// DRIVER HELPERS
// ============================================================================

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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i:iface] [d:dest_mac] [t:ethertype] [x:promiscuous] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : RAWETH.CONFIG i:eth0 d:AA:BB:CC:DD:EE:FF t:0x88B5 x:1 r:2000 w:2000 s:1500"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         RAWETH.CONFIG i:eth1 d:FF:FF:FF:FF:FF:FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         t:ethertype defaults to the driver's standard EtherType if never set"));
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

    return true;

} /* m_RAWETH_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply interface/destination/EtherType/timeout/
  *        buffer-size settings at runtime, using the same key:value grammar
  *        as the ini-backed m_LocalSetParams() (see raweth_setup.hpp).
  *
  *        Recognised keys: i:iface  d:dest_mac  t:ethertype  x:promiscuous
  *        r:read_tout  w:write_tout  s:recv_bufsize
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
  *       (d:/t: keys) — there is no per-command address override the way a
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
        RAWETH_PLUGIN_NAME,
        m_u32RawEthReadBufferSize, m_u32ReadTimeout, LT_HDR);

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
        RAWETH_PLUGIN_NAME,
        m_strArtefactsPath, m_u32RawEthReadBufferSize, m_u32ReadTimeout, LT_HDR);

} /* m_RAWETH_SCRIPT() */
