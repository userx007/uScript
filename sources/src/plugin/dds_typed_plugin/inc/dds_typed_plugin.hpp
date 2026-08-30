#ifndef DDS_TYPED_PLUGIN_HPP
#define DDS_TYPED_PLUGIN_HPP

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

#include "dds_typed_driver.hpp"

#include <string>
#include <memory>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define DDS_TYPED_PLUGIN_VERSION   "1.0.0.0"
#define DDS_TYPED_PLUGIN_NAME      "DDS_TYPED"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define DDS_TYPED_PLUGIN_COMMANDS_CONFIG_TABLE \
    DDS_TYPED_PLUGIN_CMD_RECORD(INFO)          \
    DDS_TYPED_PLUGIN_CMD_RECORD(CONFIG)        \
    DDS_TYPED_PLUGIN_CMD_RECORD(CMD)           \
    DDS_TYPED_PLUGIN_CMD_RECORD(SCRIPT)        \
    DDS_TYPED_PLUGIN_CMD_RECORD(CYCLIC)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
 * @brief The strongly-typed counterpart to `DdsPlugin` (dds_plugin.hpp).
 * Where `DdsPlugin`/`DdsDriver` publish/subscribe every topic as the same
 * one generic `{ string payload; }` IDL sample, this plugin loads real,
 * customer-specific IDL types at runtime from a `.so` built with
 * Cyclone's `idlc` — see `DdsTypePluginAbi.h`'s doc comment for the
 * loading mechanism and ABI, `dds_typed_driver.hpp`'s class doc comment
 * for how the driver stays entirely ignorant of any customer struct's
 * field layout, and `examples/customer1` for a complete, buildable
 * example customer plugin using this plugin end to end.
 *
 * Same three-way split and session lifetime as `DdsPlugin` — see that
 * class's doc comment; not repeated here. The only structural difference
 * is `DdsTypedDriver` in place of `DdsDriver`, and one extra command verb,
 * `LOAD`, on `DDS_TYPED.CMD` (see `m_DDS_TYPED_CMD`'s wiring in the .cpp
 * and `DdsTypedDriver::send()`'s doc comment):
 *
 *   DDS_TYPED.CMD > LOAD <path-to-customer.so>
 *   DDS_TYPED.CMD > PUBLISH <topic> <payload...>   // payload -> that topic's loaded type's decode()
 *   DDS_TYPED.CMD > SUBSCRIBE <topic>   |   > UNSUBSCRIBE <topic>   |   > LIST   |   <
 *
 * `PRELOAD_PLUGINS` (ini key, see dds_typed_setup.hpp) loads one or more
 * customer `.so`s automatically when the driver first opens, so a script
 * doesn't have to LOAD before every PUBLISH/SUBSCRIBE — additional LOADs
 * at runtime are still fine, and several customers' `.so`s can be loaded
 * at once (each topic name routes to whichever plugin most recently
 * registered it — see `DdsTypedDriver::m_LoadPlugin()`'s doc comment).
 *
 * When to reach for this plugin instead of (or alongside) `DdsPlugin`:
 * only when this process needs to exchange *real*, specific IDL structs
 * with an external DDS participant that expects them on the wire (e.g.
 * an NGVA subsystem publishing a real `VehicleState`). `DdsPlugin`'s
 * generic string-topic model remains the right tool for ad-hoc
 * publish/subscribe/bridging where no fixed struct is required.
 */
class DdsTypedPlugin : public PluginInterface
{
public:
    DdsTypedPlugin()
        : m_strVersion(DDS_TYPED_PLUGIN_VERSION)
        , m_strInstanceName(DDS_TYPED_PLUGIN_NAME)
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
        , m_strParticipantName("uScript-DDS-Typed")
        , m_u8Ttl(1)
        , m_u32SpdpPeriodMs(2000)
        , m_u32LeaseDurationSec(20)
        , m_bReliable(false)
        , m_u32HistoryDepth(32)
        , m_u32FragmentThresholdBytes(1300)
        , m_strPreloadPlugins()
        , m_u32ReadTimeout(5000)
        , m_u32ReadBufferSize(4096)
    {
        #define DDS_TYPED_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<DdsTypedPlugin>{&DdsTypedPlugin::m_DDS_TYPED_##a, false} ));
        DDS_TYPED_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DDS_TYPED_PLUGIN_CMD_RECORD
    }

    ~DdsTypedPlugin() = default;

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
        m_pDriver.reset(); // ~DdsTypedDriver() closes the participant and dlclose()s every loaded customer plugin
    }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<DdsTypedPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }
        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<DdsTypedPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
    {
        return generic_dispatch<DdsTypedPlugin>(this, strCmd, strParams, st);
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

    const PluginCommandsMap<DdsTypedPlugin>* getMap(void) const { return &m_mapCmds; }
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
    uint32_t getHistoryDepth(void) const { return m_u32HistoryDepth; }
    bool setHistoryDepth(const std::string& v) const { return numeric::str2uint32(v, m_u32HistoryDepth); }
    uint32_t getFragmentThresholdBytes(void) const { return m_u32FragmentThresholdBytes; }
    bool setFragmentThresholdBytes(const std::string& v) const { return numeric::str2uint32(v, m_u32FragmentThresholdBytes); }
    // Semicolon-separated list of customer type-plugin .so paths, loaded
    // in order by m_OpenDriver() the first time the driver opens — see
    // class doc comment's PRELOAD_PLUGINS note.
    const std::string& getPreloadPlugins(void) const { return m_strPreloadPlugins; }
    void setPreloadPlugins(const std::string& v) const { m_strPreloadPlugins = v; }
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
    // Factory used by CMD/SCRIPT/CYCLIC — see DdsPlugin::m_OpenDriver()'s
    // identical rationale. Additionally splits PRELOAD_PLUGINS on ';' into
    // DdsTypedDriver::Config::preloadPluginPaths before constructing.
    std::shared_ptr<DdsTypedDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    PluginCommandsMap<DdsTypedPlugin> m_mapCmds;
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
    mutable uint32_t m_u32HistoryDepth;
    mutable uint32_t m_u32FragmentThresholdBytes;
    mutable std::string m_strPreloadPlugins;

    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    mutable std::shared_ptr<DdsTypedDriver> m_pDriver;

    #define DDS_TYPED_PLUGIN_CMD_RECORD(a)  bool m_DDS_TYPED_##a ( const std::string& args, std::stop_token st ) const;
    DDS_TYPED_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  DDS_TYPED_PLUGIN_CMD_RECORD
};

#endif // DDS_TYPED_PLUGIN_HPP
