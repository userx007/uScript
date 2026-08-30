#include "dds_plugin.hpp"
#include "private/dds_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED DdsPlugin* pluginEntry()
    {
        return new DdsPlugin();
    }

    EXPORTED void pluginExit(DdsPlugin *ptrPlugin)
    {
        if(nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Driver factory
/////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<DdsDriver> DdsPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    DdsDriver::Config cfg;
    cfg.domainId          = m_u32DomainId;
    cfg.participantId     = m_u32ParticipantId;
    cfg.useIpv6             = m_bUseIpv6;
    cfg.ifaceAddress       = m_strIface;
    cfg.multicastInterface = m_strMcastIface;
    cfg.spdpMulticastGroup  = m_strSpdpMcastGroup;
    cfg.participantName    = m_strParticipantName;
    cfg.ttl                = m_u8Ttl;
    cfg.spdpPeriodMs        = m_u32SpdpPeriodMs;
    cfg.leaseDurationSec    = m_u32LeaseDurationSec;
    cfg.reliable             = m_bReliable;
    cfg.heartbeatPeriodMs    = m_u32HeartbeatPeriodMs;
    cfg.historyDepth         = m_u32HistoryDepth;
    cfg.fragmentThresholdBytes = m_u32FragmentThresholdBytes;
    cfg.strInstanceName     = m_strInstanceName;

    auto driver = std::make_shared<DdsDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DdsDriver open failed — check DOMAIN/PARTICIPANT_ID aren't already bound by another process"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Scope  : IPv4 or IPv6 (single-stack per instance); unkeyed topics addressed by"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         name (like an MQTT topic string) rather than the full DDS instance/key"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         data model. Reliable QoS (r=1) tracks HEARTBEAT/ACKNACK at *sample*"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         granularity, keeping the last 'hd' samples per writer for resend — not"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         a full unbounded KEEP_ALL history. Samples over 'fr' bytes are split"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         into DATA_FRAG fragments and reassembled on the reader side; a lost"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         fragment causes the whole sample to be re-requested (no NACK_FRAG)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the domain, participant identity and transport parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=domain] [pid=participant_id] [v6=0|1] [i=iface] [mi=mcast_iface]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [mg=spdp_mcast_group] [n=name] [t=ttl] [sp=spdp_period_ms] [l=lease_sec]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [r=0|1 reliable] [hb=heartbeat_period_ms] [hd=history_depth]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [fr=fragment_threshold_bytes] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CONFIG d=12 i=192.168.1.50 n=uScriptProbe r=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CONFIG v6=1 i=fe80::1 mi=eth0 mg=ff03::1:7401   // IPv6, see note below"));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("USE_IPV6            = false      # true = IPv6 sockets/locators instead of IPv4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("IFACE               = 0.0.0.0    # local bind address for the RTPS sockets (\"::\" if USE_IPV6)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MCAST_IFACE         =            # IPv4: local interface IP; IPv6: local interface NAME (e.g. eth0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_MULTICAST_GROUP=            # empty = 239.255.0.1 (IPv4 default); REQUIRED if USE_IPV6=true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                 # (DDSI-RTPS has no standard IPv6 default — must match your peer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_NAME    = uScript-DDS # advertised in SPDP, shown by DDS.CMD > LIST on peers"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TTL                 = 1          # SPDP multicast TTL / hop limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_PERIOD_MS      = 2000       # participant announcement interval"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("LEASE_DURATION_SEC  = 20         # how long a peer is kept without hearing a fresh SPDP"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RELIABLE            = false      # true = HEARTBEAT/ACKNACK reliability for local writers/readers"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HEARTBEAT_PERIOD_MS = 500        # reliable writers only"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HISTORY_DEPTH       = 32         # reliable writer resend-cache depth, in samples"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FRAGMENT_THRESHOLD_BYTES = 1300  # samples larger than this are DATA_FRAG'd; 0 disables"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT        = 5000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE    = 4096       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT          = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED       = true       # true=validate/parse each CYCLIC entry once per session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply domain/participant/network/QoS settings at runtime, through the
  *        same setters used by the ini-file loader in m_LocalSetParams() (see generic_dds_set_params()
  *        above).
  *
  * \note A CONFIG changing DOMAIN/PARTICIPANT_ID/IFACE after the driver is already open would
  *       silently leave stale sockets bound to the old ports - force a fresh open() next use
  *       instead, same convention as "config changed, re-open on next CMD" everywhere else in
  *       this codebase.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DdsPlugin::m_DDS_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    if (false == generic_dds_set_params(this, args)) {
        return false;
    }

    m_pDriver.reset();
    return true;

} /* m_DDS_CONFIG() */

// -----------------------------------------------------------------------
// DDS.CMD see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// DDS.SCRIPT — see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
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
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
