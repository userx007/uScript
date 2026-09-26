#include "dds_plugin.hpp"

#include "ICommDriver.hpp"
#include "PluginExport.hpp"
#include "private/dds_setup.hpp"
#include "uCommandExec.hpp"
#include "uLogger.hpp"

#include <span>
#include <sstream>
#include <string_view>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C" {
    EXPORTED DdsPlugin *pluginEntry()
    {
        return new DdsPlugin();
    }

    EXPORTED void pluginExit(DdsPlugin *pPtrPlugin)
    {
        if (nullptr != pPtrPlugin) {
            delete pPtrPlugin;
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
    cfg.domainId               = m_u32DomainId;
    cfg.participantId          = m_u32ParticipantId;
    cfg.useIpv6                = m_bUseIpv6;
    cfg.ifaceAddress           = m_strIface;
    cfg.multicastInterface     = m_strMcastIface;
    cfg.spdpMulticastGroup     = m_strSpdpMcastGroup;
    cfg.participantName        = m_strParticipantName;
    cfg.ttl                    = m_u8Ttl;
    cfg.spdpPeriodMs           = m_u32SpdpPeriodMs;
    cfg.leaseDurationSec       = m_u32LeaseDurationSec;
    cfg.reliable               = m_bReliable;
    cfg.heartbeatPeriodMs      = m_u32HeartbeatPeriodMs;
    cfg.historyDepth           = m_u32HistoryDepth;
    cfg.fragmentThresholdBytes = m_u32FragmentThresholdBytes;
    cfg.strInstanceName        = m_strInstanceName;
    cfg.maxSubscriptions       = m_u32MaxSubscriptions;

    auto driver                = std::make_shared<DdsDriver>(cfg);
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

bool DdsPlugin::m_DDS_INFO(const std::string &strArgs, std::stop_token st) const
{
    (void)strArgs;
    (void)st;
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: OMG DDSI-RTPS publish/subscribe against a real DDS domain,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             backed by Eclipse Cyclone DDS — interoperates with any other"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             real DDSI-RTPS implementation (OpenDDS, RTI Connext, FastDDS,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             ...) over plain Ethernet/IP — the wire protocol NGVA (STANAG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             4754) mandates for inter-subsystem data exchange in the vehicle"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             Data Model. https://github.com/eclipse-cyclonedds/cyclonedds"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: uDdsProtocol (the one generic IDL sample type every topic"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             uses) / DdsDriver (owns the Cyclone DDS participant, ICommDriver)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             / this plugin (CONFIG + wiring only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Scope  : IPv4 or IPv6 (single-stack per instance); unkeyed topics addressed by"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         name (like an MQTT topic string) rather than the full DDS instance/key"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         data model — every topic carries the same generic { string payload; }"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         IDL type regardless of name. Discovery (SPDP/SEDP), reliability"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (HEARTBEAT/ACKNACK), and fragmentation/reassembly of large samples are"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         all handled internally by Cyclone DDS, not by this plugin."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the domain, participant identity and transport parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=domain] [pid=participant_id] [v6=0|1] [i=iface] [mi=mcast_iface]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [mg=spdp_mcast_group] [n=name] [t=ttl] [sp=spdp_period_ms] [l=lease_sec]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [r=0|1 reliable] [hb=heartbeat_period_ms] [hd=history_depth]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [fr=fragment_threshold_bytes] [rt=read_tout] [rb=read_bufsize] [ms=max_subs]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CONFIG d=12 i=192.168.1.50 n=uScriptProbe r=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CONFIG v6=1 i=fe80::1 mi=eth0 mg=ff03::1:7401   // IPv6, see note below"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one DDS operation, on the plugin's single persistent Cyclone DDS participant"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (created on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > PUBLISH <topic> <payload...>   |"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         > SUBSCRIBE <topic>[,<topic>...] [<topic>...]   |   > UNSUBSCRIBE <topic>   |"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         > LIST   |   <   |   < ~ <topic>"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS.CMD > PUBLISH C_Actual_Video_Stream_requestVideoStream 12"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD > SUBSCRIBE C_Actual_Video_Sink,C_Actual_Alarms C_Fleet_Status"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD <                    // 1 topic SUBSCRIBEd: raw payload;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                      // 2+ topics SUBSCRIBEd: blocks on whichever"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                      // gets a sample first, returns \"<topic>: <payload>\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD < ~ C_Actual_Alarms  // reads that one topic specifically, raw payload,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                      // however many are SUBSCRIBEd"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         reading ?= DDS.CMD < &       // background thread; $reading tracks the latest sample"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD > LIST followed by DDS.CMD <   // dumps discovered participants/endpoints"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PUBLISH's payload may contain spaces (everything after <topic> is joined with"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         single spaces and CDR-encoded as one opaque string sample); PUBLISH succeeds even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with no matched subscriber yet (best-effort, matching is asynchronous discovery)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS.CMD < always needs a SUBSCRIBE earlier in the same '>'/'<' chain (or thread)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Each SUBSCRIBEd topic gets its own parallel Cyclone reader, up to ms= concurrently"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (default 64, 0=unbounded) — a safety cap only, Cyclone DDS itself has no fixed max."));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_ID      = 0          # this participant's RTPS discovery port index (Cyclone ParticipantIndex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("USE_IPV6            = false      # true = IPv6 transport instead of IPv4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("IFACE               = 0.0.0.0    # local bind address for Cyclone's transport (\"::\" if USE_IPV6)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MCAST_IFACE         =            # used when IFACE is left at its \"any\" default: IPv4 interface IP or"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                 # interface NAME (e.g. eth0) for either family"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_MULTICAST_GROUP=            # empty = Cyclone's own family default (239.255.0.1 for IPv4); set"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                 # this to match a non-Cyclone peer's configured group"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_NAME    = uScript-DDS # carried in standard USER_DATA QoS, shown by DDS.CMD > LIST on peers"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TTL                 = 1          # SPDP multicast TTL / hop limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_PERIOD_MS      = 2000       # participant announcement interval"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("LEASE_DURATION_SEC  = 20         # how long a peer is kept without hearing a fresh SPDP"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RELIABLE            = false      # true = HEARTBEAT/ACKNACK reliability for local writers/readers"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HEARTBEAT_PERIOD_MS = 500        # accepted for ini/CONFIG back-compat only — Cyclone has no public"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                 # per-writer HEARTBEAT knob, this value is not applied"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HISTORY_DEPTH       = 32         # KEEP_LAST depth QoS for local writers/readers, in samples"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FRAGMENT_THRESHOLD_BYTES = 1300  # Cyclone General/FragmentSize; 0 leaves Cyclone's own default"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT        = 5000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE    = 4096       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MAX_SUBSCRIPTIONS   = 64         # safety cap on concurrently SUBSCRIBEd topics, 0=unbounded"));
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

bool DdsPlugin::m_DDS_CONFIG(const std::string &strArgs, std::stop_token st) const
{
    (void)st;
    resetData();

    if (false == generic_dds_set_params(this, strArgs)) {
        return false;
    }

    m_pDriver.reset();
    return true;

} /* m_DDS_CONFIG() */

// -----------------------------------------------------------------------
// DDS.CMD see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_CMD(const std::string &strArgs, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_cmd(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->send(t, d, x, stop_tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->receive(t, b, o, x, stop_tok);
        },
        st);
}

// -----------------------------------------------------------------------
// DDS.SCRIPT — see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_SCRIPT(const std::string &strArgs, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_script(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->send(t, d, x, stop_tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->receive(t, b, o, x, stop_tok);
        },
        st);
}

// -----------------------------------------------------------------------
// DDS.CYCLIC — see class doc comment (dds_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsPlugin::m_DDS_CYCLIC(const std::string &strArgs, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->send(t, d, x, stop_tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const DdsDriver> drv, std::string_view x, std::stop_token stop_tok) {
            return drv->receive(t, b, o, x, stop_tok);
        });
}
