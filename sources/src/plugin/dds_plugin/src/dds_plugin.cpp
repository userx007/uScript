#include "dds_plugin.hpp"
#include "private/dds_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "DDS PLUGIN  |"

extern "C"
{
    EXPORTED DdsPlugin* pluginEntry() { return new DdsPlugin(); }
    EXPORTED void pluginExit(DdsPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool DdsPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void DdsPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    m_pDriver.reset(); // ~DdsDriver() stops the discovery thread and closes the RTPS sockets
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool DdsPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    if (generic_setparams<DdsPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void DdsPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<DdsPlugin>(this, psGetParams);
}

bool DdsPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<DdsPlugin>(this, strCmd, strParams, st);
}

// -----------------------------------------------------------------------
// Driver factory
// -----------------------------------------------------------------------

std::shared_ptr<DdsDriver> DdsPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    DdsDriver::Config cfg;
    cfg.domainId          = m_u32DomainId;
    cfg.participantId     = m_u32ParticipantId;
    cfg.ifaceAddress       = m_strIface;
    cfg.multicastInterface = m_strMcastIface;
    cfg.participantName    = m_strParticipantName;
    cfg.ttl                = m_u8Ttl;
    cfg.spdpPeriodMs        = m_u32SpdpPeriodMs;
    cfg.leaseDurationSec    = m_u32LeaseDurationSec;
    cfg.strInstanceName     = m_strInstanceName;

    auto driver = std::make_shared<DdsDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DdsDriver open failed — check DOMAIN/PARTICIPANT_ID aren't already bound by another process"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

// -----------------------------------------------------------------------
// Top-level commands
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << DDS_PLUGIN_NAME " v" << m_strVersion
        << " domain=" << m_u32DomainId
        << " participant_id=" << m_u32ParticipantId
        << " iface=" << m_strIface
        << " name=" << m_strParticipantName;
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DDS_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: OMG DDSI-RTPS publish/subscribe against a real DDS domain"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             (OpenDDS, RTI Connext, CycloneDDS, FastDDS, ...) over plain"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             Ethernet/IP — the wire protocol NGVA (STANAG 4754) mandates"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             for inter-subsystem data exchange in the vehicle Data Model."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: DdsProtocol (protocol, pure RTPS codec) / DdsDriver (owns the"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             3 RTPS UDP sockets + discovery thread, ICommDriver) / this"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             plugin (CONFIG + wiring only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Scope  : best-effort QoS only (no HEARTBEAT/ACKNACK/fragmentation),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         IPv4 only, unkeyed topics addressed by name (like an MQTT topic"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         string) rather than the full DDS instance/key data model."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the domain, participant identity and transport parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=domain] [pid=participant_id] [i=iface] [mi=mcast_iface] [n=name]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [t=ttl] [sp=spdp_period_ms] [l=lease_sec] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CONFIG d=12 i=192.168.1.50 n=uScriptProbe"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one DDS operation, on the plugin's single persistent RTPS participant"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (opened, i.e. sockets bound + discovery thread started, on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > PUBLISH <topic> <payload...>   |   > SUBSCRIBE <topic>   |"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         > UNSUBSCRIBE <topic>   |   > LIST   |   <"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CMD > PUBLISH C_Actual_Video_Stream_requestVideoStream 12"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD > SUBSCRIBE C_Actual_Video_Sink"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD <                    // one blocking receive; requires an active SUBSCRIBE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         reading ?= DDS.CMD < &       // background thread; $reading tracks the latest sample"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD > LIST followed by DDS.CMD <   // dumps discovered participants/endpoints"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PUBLISH's payload may contain spaces (everything after <topic> is joined with"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         single spaces and CDR-encoded as one opaque string sample); PUBLISH succeeds even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with no matched subscriber yet (best-effort, matching is asynchronous discovery)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD < always needs a SUBSCRIBE earlier in the same '>'/'<' chain (or thread)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several DDS.CMD-style lines from a file over the same participant"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : periodic publish, same participant as CMD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : \"time1:val1, time2:val2, ...\" — each val is a full DDS.CMD-style '> ...' argument"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CYCLIC 1000:> PUBLISH C_Actual_Video_Sink 3"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[DDS]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH      =            # directory used by SCRIPT/CMD for reading artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DOMAIN              = 0          # DDS domain id (RTPS port formula: 7400 + 250*domain + ...)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_ID      = 0          # selects this participant's unicast metatraffic/user ports"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("IFACE               = 0.0.0.0    # local bind address for the RTPS sockets"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MCAST_IFACE         =            # local interface used to join/send SPDP multicast (empty = kernel default)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_NAME    = uScript-DDS # advertised in SPDP, shown by DDS.CMD > LIST on peers"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TTL                 = 1          # SPDP multicast TTL"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_PERIOD_MS      = 2000       # participant announcement interval"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("LEASE_DURATION_SEC  = 20         # how long a peer is kept without hearing a fresh SPDP"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT        = 5000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE    = 4096       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT          = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED       = true       # true=validate/parse each CYCLIC entry once per session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;
}

// -----------------------------------------------------------------------
// DDS.CMD / DDS.SCRIPT — see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, &m_strResultData, m_bRawResult,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

bool DdsPlugin::m_DDS_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// DDS.CYCLIC — see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, st, m_bCyclicCached,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
