#ifndef DDS_PLUGIN_HPP
#define DDS_PLUGIN_HPP
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginExport.hpp"
#include "PluginOperations.hpp"
#include "dds_driver.hpp"
#include "uBoolEvaluator.hpp"
#include "uCommandExec.hpp"
#include "uFile.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"

#include <memory>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <utility>

struct PluginDataGet;
struct PluginDataSet;

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define DDS_PLUGIN_VERSION "1.0.0.0"
#define DDS_PLUGIN_NAME    "DDS"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define DDS_PLUGIN_COMMANDS_CONFIG_TABLE \
    DDS_PLUGIN_CMD_RECORD(INFO)          \
    DDS_PLUGIN_CMD_RECORD(CONFIG)        \
    DDS_PLUGIN_CMD_RECORD(CMD)           \
    DDS_PLUGIN_CMD_RECORD(SCRIPT)        \
    DDS_PLUGIN_CMD_RECORD(CYCLIC)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

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
 *   - **Protocol side**: `uDdsProtocol` (protocols/dds) — the one generic
 *     IDL sample type every DDS.CMD topic is published/subscribed as,
 *     compiled once via Cyclone DDS's own `idlc` — no hand-written RTPS
 *     codec anymore, see dds_driver.hpp's class doc comment.
 *   - **Driver side**: `DdsDriver` (dds_driver.hpp) — wraps a real
 *     Eclipse Cyclone DDS (https://github.com/eclipse-cyclonedds/cyclonedds)
 *     participant instead of owning raw RTPS UDP sockets directly, plus
 *     the DDS.CMD intermediary command parsing.
 *   - **Plugin side**: this class. CONFIG storage, an INFO summary, and
 *     wiring `m_OpenDriver()`'s `DdsDriver` into
 *     `ucmdexec::generic_cmd()`/`generic_script()`/`generic_send_cyclic()`.
 *
 * Session lifetime matches MqttPlugin's: one persistent `DdsDriver` (and
 * its Cyclone DDS participant) per plugin instance, opened by
 * `m_OpenDriver()` on first use and kept alive until `doCleanup()`. This
 * is what makes `DDS.CMD > SUBSCRIBE topic` followed later by
 * `DDS.CMD <` meaningful — see dds_driver.hpp's receive() doc comment.
 *
 * `SUBSCRIBE` accepts several topics at once, space- and/or comma-
 * separated (e.g. `SUBSCRIBE a,b c`), each getting its own Cyclone reader
 * running in parallel — up to `MAX_SUBSCRIPTIONS`(ini)/`ms=`(CONFIG),
 * default 64, `0` = unbounded (Cyclone DDS itself has no fixed cap on
 * readers per participant; this only guards a runaway topic list). A
 * plain `DDS.CMD <` then behaves according to how many topics are
 * currently SUBSCRIBEd: exactly one — raw payload, unchanged from every
 * prior release; several — multiplexed, blocking on whichever gets a
 * sample first and returning `<topic>: <payload>`. `DDS.CMD < ~ <topic>`
 * reads one specific already-SUBSCRIBEd topic deterministically (raw
 * payload) regardless of how many others are also SUBSCRIBEd — see
 * `DdsDriver::receive()`'s doc comment for the full grammar.
 *
 * Scope limitations are documented once, in dds_driver.hpp's class doc
 * comment (unkeyed topics, single generic sample type) — repeated in
 * DDS_INFO's text below for anyone querying the plugin directly rather
 * than reading source. Discovery, reliability, fragmentation and IPv4/
 * IPv6 are no longer this plugin's own concern — Cyclone DDS handles all
 * of that as a real, interoperable DDSI-RTPS implementation.
 */
class DdsPlugin : public PluginInterface
{
    public:
        /** Registers the plugin's commands and default settings. */
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
            , m_u32MaxSubscriptions(64)
        {
            // clang-format off
#define DDS_PLUGIN_CMD_RECORD(a) m_mapCmds.insert(std::make_pair(#a, \
        PluginCommandEntry<DdsPlugin>{&DdsPlugin::m_DDS_##a, false}));
        DDS_PLUGIN_COMMANDS_CONFIG_TABLE
#undef DDS_PLUGIN_CMD_RECORD
            // clang-format on
        }

        ~DdsPlugin() = default;

        /** Returns whether doInit() has succeeded. */
        bool isInitialized(void) const
        {
            return m_bIsInitialized;
        }

        /** Returns whether the plugin has been enabled. */
        bool isEnabled(void) const
        {
            return m_bIsEnabled;
        }

        /** Returns the fault-tolerant flag. */
        bool isFaultTolerant(void) const
        {
            return m_bIsFaultTolerant;
        }

        /** Returns whether the plugin needs privileged access. */
        bool isPrivileged(void) const
        {
            return m_bIsPrivileged;
        }

        /** Initialises the plugin. */
        bool doInit(void *pvUserData)
        {
            (void)pvUserData;
            m_bIsInitialized = true;
            return true;
        }

        /** Releases the plugin's resources. */
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
            m_strResultData.clear();
            m_pDriver.reset(); // ~DdsDriver() closes this instance's Cyclone DDS participant (see dds_driver.hpp's close() doc comment)
        }

        /** Applies the settings loaded from the ini file. */
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

        /** Fills @p psGetParams with the current settings. */
        void getParams(PluginDataGet *psGetParams) const
        {
            generic_getparams<DdsPlugin>(this, psGetParams);
        }

        /** Runs command @p strCmd with @p strParams; @p st requests early stop. */
        bool doDispatch(const std::string &strCmd, const std::string &strParams, std::stop_token st) const
        {
            return generic_dispatch<DdsPlugin>(this, strCmd, strParams, st);
        }

        /** Enables the plugin. */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /** Sets the raw-result flag from its string form; returns false if invalid. */
        bool setRawResult(const std::string &strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /** Sets the cyclic-cached flag from its string form; returns false if invalid. */
        bool setCyclicCached(const std::string &strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
        }

        /** Returns the command table. */
        const PluginCommandsMap<DdsPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /** Returns the plugin version string. */
        const std::string &getVersion(void) const
        {
            return m_strVersion;
        }

        /** Returns the result data of the last command. */
        const std::string &getData(void) const
        {
            return m_strResultData;
        }

        /** Clears the stored result data. */
        void resetData(void) const
        {
            m_strResultData.clear();
        }

        // Getters/Setters
        uint32_t getDomainId(void) const
        {
            return m_u32DomainId;
        }

        /** Sets the domain id from its string form; returns false if the value is invalid. */
        bool setDomainId(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32DomainId);
        }

        /** Returns the participant id setting. */
        uint32_t getParticipantId(void) const
        {
            return m_u32ParticipantId;
        }

        /** Sets the participant id from its string form; returns false if the value is invalid. */
        bool setParticipantId(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32ParticipantId);
        }

        /** Returns the use ipv6 setting. */
        bool getUseIpv6(void) const
        {
            return m_bUseIpv6;
        }

        /** Sets the use ipv6 from its string form; returns false if the value is invalid. */
        bool setUseIpv6(const std::string &strV) const
        {
            BoolExprEvaluator e;
            return e.evaluate(strV, m_bUseIpv6);
        }

        /** Returns the iface setting. */
        const std::string &getIface(void) const
        {
            return m_strIface;
        }

        /** Sets the iface setting. */
        void setIface(const std::string &strV) const
        {
            m_strIface = strV;
        }

        /** Returns the mcast iface setting. */
        const std::string &getMcastIface(void) const
        {
            return m_strMcastIface;
        }

        /** Sets the mcast iface setting. */
        void setMcastIface(const std::string &strV) const
        {
            m_strMcastIface = strV;
        }

        /** Returns the spdp mcast group setting. */
        const std::string &getSpdpMcastGroup(void) const
        {
            return m_strSpdpMcastGroup;
        }

        /** Sets the spdp mcast group setting. */
        void setSpdpMcastGroup(const std::string &strV) const
        {
            m_strSpdpMcastGroup = strV;
        }

        /** Returns the participant name setting. */
        const std::string &getParticipantName(void) const
        {
            return m_strParticipantName;
        }

        /** Sets the participant name setting. */
        void setParticipantName(const std::string &strV) const
        {
            m_strParticipantName = strV;
        }

        /** Returns the ttl setting. */
        uint8_t getTtl(void) const
        {
            return m_u8Ttl;
        }

        /** Sets the ttl from its string form; returns false if the value is invalid. */
        bool setTtl(const std::string &strV) const
        {
            uint32_t ttl = 0;
            if (!numeric::str2uint32(strV, ttl) || ttl > 255) {
                return false;
            }
            m_u8Ttl = static_cast<uint8_t>(ttl);
            return true;
        }

        /** Returns the spdp period ms setting. */
        uint32_t getSpdpPeriodMs(void) const
        {
            return m_u32SpdpPeriodMs;
        }

        /** Sets the spdp period ms from its string form; returns false if the value is invalid. */
        bool setSpdpPeriodMs(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32SpdpPeriodMs);
        }

        /** Returns the lease duration sec setting. */
        uint32_t getLeaseDurationSec(void) const
        {
            return m_u32LeaseDurationSec;
        }

        /** Sets the lease duration sec from its string form; returns false if the value is invalid. */
        bool setLeaseDurationSec(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32LeaseDurationSec);
        }

        /** Returns the reliable setting. */
        bool getReliable(void) const
        {
            return m_bReliable;
        }

        /** Sets the reliable from its string form; returns false if the value is invalid. */
        bool setReliable(const std::string &strV) const
        {
            BoolExprEvaluator e;
            return e.evaluate(strV, m_bReliable);
        }

        /** Returns the heartbeat period ms setting. */
        uint32_t getHeartbeatPeriodMs(void) const
        {
            return m_u32HeartbeatPeriodMs;
        }

        /** Sets the heartbeat period ms from its string form; returns false if the value is invalid. */
        bool setHeartbeatPeriodMs(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32HeartbeatPeriodMs);
        }

        /** Returns the history depth setting. */
        uint32_t getHistoryDepth(void) const
        {
            return m_u32HistoryDepth;
        }

        /** Sets the history depth from its string form; returns false if the value is invalid. */
        bool setHistoryDepth(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32HistoryDepth);
        }

        /** Returns the fragment threshold bytes setting. */
        uint32_t getFragmentThresholdBytes(void) const
        {
            return m_u32FragmentThresholdBytes;
        }

        /** Sets the fragment threshold bytes from its string form; returns false if the value is invalid. */
        bool setFragmentThresholdBytes(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32FragmentThresholdBytes);
        }

        /** Returns the read timeout setting. */
        uint32_t getReadTimeout(void) const
        {
            return m_u32ReadTimeout;
        }

        /** Sets the read timeout from its string form; returns false if the value is invalid. */
        bool setReadTimeout(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32ReadTimeout);
        }

        /** Returns the read buffer size setting. */
        uint32_t getReadBufferSize(void) const
        {
            return m_u32ReadBufferSize;
        }

        /** Sets the read buffer size from its string form; returns false if the value is invalid. */
        bool setReadBufferSize(const std::string &strV) const
        {
            uint32_t sz = 0;
            if (!numeric::str2uint32(strV, sz) || sz == 0) {
                return false;
            }
            m_u32ReadBufferSize = sz;
            return true;
        }

        // Safety cap on concurrently SUBSCRIBEd topics — see
        // DdsDriver::Config::maxSubscriptions's doc comment. 0 disables the
        // cap (unbounded, limited only by process resources).
        uint32_t getMaxSubscriptions(void) const
        {
            return m_u32MaxSubscriptions;
        }

        /** Sets the max subscriptions from its string form; returns false if the value is invalid. */
        bool setMaxSubscriptions(const std::string &strV) const
        {
            return numeric::str2uint32(strV, m_u32MaxSubscriptions);
        }

    private:
        // Factory used by CMD/SCRIPT/CYCLIC (passed as ucmdexec's openFn): builds
        // a DdsDriver::Config from the stored settings and returns the one
        // persistent DdsDriver for this plugin instance, constructing (and
        // open()-ing, i.e. creating the Cyclone DDS domain/participant) it on
        // first use. See class doc comment's "Session lifetime".
        std::shared_ptr<DdsDriver> m_OpenDriver(void) const;

        /** Applies the ini settings to this plugin's configuration. */
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
        mutable uint32_t m_u32MaxSubscriptions;

        // The persistent driver — see class doc comment's "Session lifetime".
        mutable std::shared_ptr<DdsDriver> m_pDriver;

#define DDS_PLUGIN_CMD_RECORD(a) bool m_DDS_##a(const std::string &args, std::stop_token st) const;
        DDS_PLUGIN_COMMANDS_CONFIG_TABLE
#undef DDS_PLUGIN_CMD_RECORD
};

#endif // DDS_PLUGIN_HPP
