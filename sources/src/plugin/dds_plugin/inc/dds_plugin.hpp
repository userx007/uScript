#ifndef DDS_PLUGIN_HPP
#define DDS_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uFile.hpp"
#include "uBoolEvaluator.hpp"

#include <string>
#include <memory>

#include "dds_driver.hpp"

#define DDS_PLUGIN_VERSION   "1.0.0.0"
#define DDS_PLUGIN_NAME      "DDS"

#define DDS_PLUGIN_COMMANDS_CONFIG_TABLE \
    DDS_PLUGIN_CMD_RECORD(INFO)          \
    DDS_PLUGIN_CMD_RECORD(CONFIG)        \
    DDS_PLUGIN_CMD_RECORD(CMD)           \
    DDS_PLUGIN_CMD_RECORD(SCRIPT)        \
    DDS_PLUGIN_CMD_RECORD(CYCLIC)

/**
 * @brief OMG DDSI-RTPS (OMG DDS Interoperability Wire Protocol) plugin —
 * publish/subscribe against a real DDS domain (OpenDDS, RTI Connext,
 * CycloneDDS, FastDDS, ...) over plain Ethernet/IP, the way NGVA (STANAG
 * 4754, NATO Generic Vehicle Architecture) uses DDS/OpenDDS to exchange
 * data between sub-systems in a land vehicle — see the "DDS – the data
 * exchange mechanism" section of the NGVA Data Model white paper this
 * plugin was written against. Structured identically to MqttPlugin
 * (mqtt_plugin.hpp) and GrpcPlugin — see that class's doc comment for the
 * general three-way split this follows:
 *
 *   - **Protocol side**: `DdsProtocol` (dds_protocol.hpp) — pure RTPS wire
 *     encode/decode, no I/O.
 *   - **Driver side**: `DdsDriver` (dds_driver.hpp) — owns the three RTPS
 *     UDP sockets (SPDP multicast, metatraffic unicast, user-data
 *     unicast), the background discovery thread, and the DDS.CMD
 *     intermediary command parsing.
 *   - **Plugin side**: this class. CONFIG storage, an INFO summary, and
 *     wiring `m_OpenDriver()`'s `DdsDriver` into
 *     `ucmdexec::generic_cmd()`/`generic_script()`/`generic_send_cyclic()`.
 *
 * Session lifetime matches MqttPlugin's: one persistent `DdsDriver` (and
 * its RTPS participant/sockets/discovery thread) per plugin instance,
 * opened by `m_OpenDriver()` on first use and kept alive until
 * `doCleanup()`. This is what makes `DDS.CMD > SUBSCRIBE topic` followed
 * later by `DDS.CMD <` meaningful — see dds_driver.hpp's receive() doc
 * comment.
 *
 * Scope limitations are documented once, in dds_protocol.hpp's and
 * dds_driver.hpp's class doc comments (best-effort QoS only, IPv4-only,
 * unkeyed topics) — repeated in DDS_INFO's text below for anyone querying
 * the plugin directly rather than reading source.
 */
class DdsPlugin : public PluginInterface
{
public:
    DdsPlugin()
        : m_strVersion(DDS_PLUGIN_VERSION)
        , m_strInstanceName(DDS_PLUGIN_NAME)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_bRawResult(false)
        , m_bCyclicCached(true)
        , m_u32DomainId(0)
        , m_u32ParticipantId(0)
        , m_bUseIpv6(false)
        , m_strIface("0.0.0.0")
        , m_strMcastIface()
        , m_strSpdpMcastGroup()
        , m_strParticipantName("uScript-DDS")
        , m_u8Ttl(1)
        , m_u32SpdpPeriodMs(2000)
        , m_u32LeaseDurationSec(20)
        , m_bReliable(false)
        , m_u32HeartbeatPeriodMs(500)
        , m_u32HistoryDepth(32)
        , m_u32FragmentThresholdBytes(1300)
        , m_u32ReadTimeout(5000)
        , m_u32ReadBufferSize(4096)
    {
        #define DDS_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<DdsPlugin>{&DdsPlugin::m_DDS_##a, false} ));
        DDS_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DDS_PLUGIN_CMD_RECORD
    }

    ~DdsPlugin() = default;

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled(void) const { return m_bIsEnabled; }
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged(void) const { return m_bIsPrivileged; }

    bool doInit(void *pvUserData)
    {
        (void)pvUserData;
        m_bIsInitialized = true;
        return true;
    }

    void doCleanup(void)
    {
        m_bIsInitialized = false;
        m_bIsEnabled = false;
        m_strResultData.clear();
        m_pDriver.reset(); // ~DdsDriver() stops the discovery thread and closes the RTPS sockets
    }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<DdsPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }
        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<DdsPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
    {
        return generic_dispatch<DdsPlugin>(this, strCmd, strParams, st);
    }

    bool doEnable(void) { m_bIsEnabled = true; return true; }

    bool setRawResult (const std::string& strValue) const
    {
        return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
    }
    bool setCyclicCached (const std::string& strValue) const
    {
        return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
    }

    const PluginCommandsMap<DdsPlugin>* getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData(void) const { return m_strResultData; }
    void resetData(void) const { m_strResultData.clear(); }

    // Getters/Setters
    uint32_t getDomainId(void) const { return m_u32DomainId; }
    bool setDomainId(const std::string& v) const { return numeric::str2uint32(v, m_u32DomainId); }
    uint32_t getParticipantId(void) const { return m_u32ParticipantId; }
    bool setParticipantId(const std::string& v) const { return numeric::str2uint32(v, m_u32ParticipantId); }
    bool getUseIpv6(void) const { return m_bUseIpv6; }
    bool setUseIpv6(const std::string& v) const { BoolExprEvaluator e; return e.evaluate(v, m_bUseIpv6); }
    const std::string& getIface(void) const { return m_strIface; }
    void setIface(const std::string& v) const { m_strIface = v; }
    const std::string& getMcastIface(void) const { return m_strMcastIface; }
    void setMcastIface(const std::string& v) const { m_strMcastIface = v; }
    const std::string& getSpdpMcastGroup(void) const { return m_strSpdpMcastGroup; }
    void setSpdpMcastGroup(const std::string& v) const { m_strSpdpMcastGroup = v; }
    const std::string& getParticipantName(void) const { return m_strParticipantName; }
    void setParticipantName(const std::string& v) const { m_strParticipantName = v; }
    uint8_t getTtl(void) const { return m_u8Ttl; }
    bool setTtl(const std::string& v) const {
        uint32_t ttl = 0;
        if (!numeric::str2uint32(v, ttl) || ttl > 255) return false;
        m_u8Ttl = static_cast<uint8_t>(ttl);
        return true;
    }
    uint32_t getSpdpPeriodMs(void) const { return m_u32SpdpPeriodMs; }
    bool setSpdpPeriodMs(const std::string& v) const { return numeric::str2uint32(v, m_u32SpdpPeriodMs); }
    uint32_t getLeaseDurationSec(void) const { return m_u32LeaseDurationSec; }
    bool setLeaseDurationSec(const std::string& v) const { return numeric::str2uint32(v, m_u32LeaseDurationSec); }
    bool getReliable(void) const { return m_bReliable; }
    bool setReliable(const std::string& v) const { BoolExprEvaluator e; return e.evaluate(v, m_bReliable); }
    uint32_t getHeartbeatPeriodMs(void) const { return m_u32HeartbeatPeriodMs; }
    bool setHeartbeatPeriodMs(const std::string& v) const { return numeric::str2uint32(v, m_u32HeartbeatPeriodMs); }
    uint32_t getHistoryDepth(void) const { return m_u32HistoryDepth; }
    bool setHistoryDepth(const std::string& v) const { return numeric::str2uint32(v, m_u32HistoryDepth); }
    uint32_t getFragmentThresholdBytes(void) const { return m_u32FragmentThresholdBytes; }
    bool setFragmentThresholdBytes(const std::string& v) const { return numeric::str2uint32(v, m_u32FragmentThresholdBytes); }
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    bool setReadTimeout(const std::string& v) const { return numeric::str2uint32(v, m_u32ReadTimeout); }
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }
    bool setReadBufferSize(const std::string& v) const {
        uint32_t sz = 0;
        if (!numeric::str2uint32(v, sz) || sz == 0) return false;
        m_u32ReadBufferSize = sz;
        return true;
    }

private:
    // Factory used by CMD/SCRIPT/CYCLIC (passed as ucmdexec's openFn): builds
    // a DdsDriver::Config from the stored settings and returns the one
    // persistent DdsDriver for this plugin instance, constructing (and
    // open()-ing, i.e. binding sockets + starting the discovery thread) it
    // on first use. See class doc comment's "Session lifetime".
    std::shared_ptr<DdsDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    PluginCommandsMap<DdsPlugin> m_mapCmds;
    std::string m_strVersion;
    std::string m_strInstanceName;
    mutable std::string m_strResultData;
    mutable bool m_bRawResult;
    mutable bool m_bCyclicCached;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    std::string m_strArtefactsPath;

    mutable uint32_t m_u32DomainId;
    mutable uint32_t m_u32ParticipantId;
    mutable bool m_bUseIpv6;
    mutable std::string m_strIface;
    mutable std::string m_strMcastIface;
    mutable std::string m_strSpdpMcastGroup;
    mutable std::string m_strParticipantName;
    mutable uint8_t m_u8Ttl;
    mutable uint32_t m_u32SpdpPeriodMs;
    mutable uint32_t m_u32LeaseDurationSec;
    mutable bool m_bReliable;
    mutable uint32_t m_u32HeartbeatPeriodMs;
    mutable uint32_t m_u32HistoryDepth;
    mutable uint32_t m_u32FragmentThresholdBytes;

    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    // The persistent driver — see class doc comment's "Session lifetime".
    mutable std::shared_ptr<DdsDriver> m_pDriver;

    #define DDS_PLUGIN_CMD_RECORD(a)  bool m_DDS_##a ( const std::string& args, std::stop_token st ) const;
    DDS_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  DDS_PLUGIN_CMD_RECORD
};

#endif // DDS_PLUGIN_HPP
