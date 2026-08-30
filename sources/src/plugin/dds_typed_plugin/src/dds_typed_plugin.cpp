#include "dds_typed_plugin.hpp"
#include "private/dds_typed_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED DdsTypedPlugin* pluginEntry()
    {
        return new DdsTypedPlugin();
    }

    EXPORTED void pluginExit(DdsTypedPlugin *ptrPlugin)
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

namespace
{
    // Splits PRELOAD_PLUGINS="./a.so ; ./b.so" into {"./a.so", "./b.so"} —
    // deliberately local/one-off rather than pulling in a shared split
    // utility, same convention as DdsTypedDriver's own local tokenize()
    // helper (dds_typed_driver.cpp).
    std::vector<std::string> splitPreloadPaths(const std::string& csv)
    {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= csv.size()) {
            const size_t sep = csv.find(';', start);
            const std::string token = ustring::trim(csv.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
            if (!token.empty()) out.push_back(token);
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
        return out;
    }
}

std::shared_ptr<DdsTypedDriver> DdsTypedPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    DdsTypedDriver::Config cfg;
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
    cfg.historyDepth           = m_u32HistoryDepth;
    cfg.fragmentThresholdBytes = m_u32FragmentThresholdBytes;
    cfg.strInstanceName        = m_strInstanceName;
    cfg.preloadPluginPaths     = splitPreloadPaths(m_strPreloadPlugins);

    auto driver = std::make_shared<DdsTypedDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DdsTypedDriver open failed — check DOMAIN/PARTICIPANT_ID aren't already bound by another process"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

bool DdsTypedPlugin::m_DDS_TYPED_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << DDS_TYPED_PLUGIN_NAME " v" << m_strVersion
        << " domain=" << m_u32DomainId
        << " participant_id=" << m_u32ParticipantId
        << " iface=" << m_strIface
        << " name=" << m_strParticipantName;
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DDS_TYPED_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: strongly-typed counterpart to the DDS plugin — publishes/"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             subscribes real, customer-specific IDL structs (not a generic"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             string payload) against a Cyclone DDS domain, by dlopen()ing"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             one or more customer type-plugin .so's built from real .idl"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             files. See DdsTypePluginAbi.h and examples/customer1."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: DdsTypedDriver never includes a customer's generated header"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             or touches a struct field — everything it needs (topic"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             descriptor, alloc/free, text<->sample encode/decode) comes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             from the loaded plugin via DdsTypePluginAbi.h's stable C ABI."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             Swap which customer .so is loaded (or load several at once)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             with zero rebuild of this plugin or its driver."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Scope  : one DDS topic per loaded type, each with its own real IDL struct;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PUBLISH/receive exchange plain text with the customer .so's own"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         decode()/encode() — the text grammar (JSON, key=value, ...) is"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         entirely up to that customer .so, not fixed by this plugin."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the domain, participant identity, transport and preload plugins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=domain] [pid=participant_id] [v6=0|1] [i=iface] [mi=mcast_iface]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [mg=spdp_mcast_group] [n=name] [t=ttl] [sp=spdp_period_ms] [l=lease_sec]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [r=0|1 reliable] [hd=history_depth] [fr=fragment_threshold_bytes]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [pp=path1.so;path2.so] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS_TYPED.CONFIG d=12 pp=./libcustomer1_types.so"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one DDS_TYPED operation, on the plugin's single persistent Cyclone DDS participant"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > LOAD <path.so>   |   > PUBLISH <topic> <payload...>   |"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         > SUBSCRIBE <topic>   |   > UNSUBSCRIBE <topic>   |   > LIST   |   <"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS_TYPED.CMD > LOAD ./libcustomer1_types.so"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS_TYPED.CMD > PUBLISH vehicle/state id=1,label=truck-07,speed=27.5"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS_TYPED.CMD > SUBSCRIBE vehicle/state"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DDS_TYPED.CMD <                    // one blocking receive; requires an active SUBSCRIBE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PUBLISH's <payload...> text is passed verbatim to that topic's loaded"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         type's decode() — its grammar is defined by that customer .so, not this"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         plugin. PUBLISH/SUBSCRIBE on a topic with no loaded type fails; LOAD it first."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several DDS_TYPED.CMD-style lines from a file over the same participant"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS_TYPED.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : periodic publish, same participant as CMD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : \"time1:val1, time2:val2, ...\" — each val is a full DDS_TYPED.CMD-style '> ...' argument"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DDS_TYPED.CYCLIC 1000:> PUBLISH vehicle/state id=1,label=truck-07,speed=27.5"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[DDS_TYPED]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH      =            # directory used by SCRIPT/CMD for reading artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DOMAIN              = 0          # DDS domain id"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_ID      = 0          # this participant's RTPS discovery port index"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("USE_IPV6            = false"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("IFACE               = 0.0.0.0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MCAST_IFACE         ="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_MULTICAST_GROUP="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PARTICIPANT_NAME    = uScript-DDS-Typed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TTL                 = 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPDP_PERIOD_MS      = 2000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("LEASE_DURATION_SEC  = 20"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RELIABLE            = false"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HISTORY_DEPTH       = 32"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FRAGMENT_THRESHOLD_BYTES = 1300"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PRELOAD_PLUGINS     =            # semicolon-separated customer type-plugin .so paths,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                                 # loaded automatically the first time the driver opens"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT        = 5000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE    = 4096"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT          = false"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED       = true"));

    return true;
}

bool DdsTypedPlugin::m_DDS_TYPED_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    if (false == generic_dds_typed_set_params(this, args)) {
        return false;
    }

    m_pDriver.reset();
    return true;
}

// -----------------------------------------------------------------------
// DDS_TYPED.CMD — see class doc comment (dds_typed_plugin.hpp)
// -----------------------------------------------------------------------

bool DdsTypedPlugin::m_DDS_TYPED_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsTypedDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// DDS_TYPED.SCRIPT
// -----------------------------------------------------------------------

bool DdsTypedPlugin::m_DDS_TYPED_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsTypedDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// DDS_TYPED.CYCLIC
// -----------------------------------------------------------------------

bool DdsTypedPlugin::m_DDS_TYPED_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<DdsTypedDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const DdsTypedDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
