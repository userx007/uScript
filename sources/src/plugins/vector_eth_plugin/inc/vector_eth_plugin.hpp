#ifndef VECTOR_ETH_PLUGIN_HPP
#define VECTOR_ETH_PLUGIN_HPP
#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uBoolEvaluator.hpp"
#include "uLogger.hpp"
#include "uVectorEth.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <memory>
#include <optional>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define VECTOR_ETH_PLUGIN_VERSION    "1.0.0.0"
#define VECTOR_ETH_PLUGIN_NAME       "VECTOR_ETH"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

#ifndef VECTOR_ETH_GET_BLOCKING
#define VECTOR_ETH_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define VECTOR_ETH_PLUGIN_COMMANDS_CONFIG_TABLE    \
VECTOR_ETH_PLUGIN_CMD_RECORD( INFO               ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( CONFIG             ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( FILTER             ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( CMD                ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( SCRIPT             ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( CYCLIC             ) \
VECTOR_ETH_PLUGIN_CMD_RECORD( DEVICES            ) \

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
  * \brief Vector Ethernet plugin class definition.
  *
  * Wraps the uVectorEth driver (Vector Informatik XL-API's direct/port-based
  * Ethernet interface, e.g. VN5610(A)/VN7610/VN7570/VX1135) and exposes it
  * through the standard PluginInterface dispatch model. Windows only — see
  * uVectorEth.hpp. Sibling to the VECTOR (CAN/CAN-FD) plugin — device
  * enumeration/direct-selection grammar (hw=/serial=/name=/hwidx=/hwch=,
  * DEVICES) is identical, since both share Vector::enumerateChannels().
  *
  * Key differences vs the VECTOR (CAN) plugin:
  *   - No bitrate ("b=") — Ethernet PHY link speed/duplex/etc. are
  *     configured via "speed="/"duplex="/"connector="/"phy=" instead
  *     (xlEthSetConfig()), all defaulting to auto-negotiate.
  *   - "x="/"y=" (CAN TX/RX id) are replaced by "dst=" (destination MAC,
  *     default broadcast) and "type=" (EtherType, default 0x88B5).
  *   - FILTER takes "src=<mac>" and/or "type=<ethertype>" (either or both,
  *     space- or comma-separated) instead of PCAN/VECTOR's "<id>:<mask>" -
  *     see uVectorEth.hpp's setRxFilterSrcMac()/setRxFilterEtherType().
  *     Unlike VECTOR.FILTER's single-id caveat, BOTH filters are enforced
  *     together (logical AND) since there is only ever one of each.
  *   - No multi-frame transport-protocol layer (no "t=", no TpConfig tuning
  *     keys) — see uVectorEth.hpp's class comment for why.
  *   - CMD/SCRIPT/CYCLIC's optional per-call xtra_params override is
  *     "<mac>", "<mac>/<ethertype>", or "/<ethertype>" (see
  *     VectorEth::resolveDest()), not a bare numeric CAN id.
*/
class VectorEthPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        VectorEthPlugin() : m_strVersion(VECTOR_ETH_PLUGIN_VERSION)
                    , m_strInstanceName(VECTOR_ETH_PLUGIN_NAME)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_bCyclicCached(true)
                    , m_strAppName("VectorEth_Plugin")   // must match an app configured in Vector Hardware Config
                    , m_u32AppChannel(0U)
                    , m_strDeviceHw()
                    , m_bDeviceHwSet(false)
                    , m_u32DeviceHw(0U)
                    , m_u32DeviceSerial(0U)
                    , m_strDeviceName()
                    , m_bDeviceHwIndexSet(false)
                    , m_u32DeviceHwIndex(0U)
                    , m_bDeviceHwChannelSet(false)
                    , m_u32DeviceHwChannel(0U)
                    , m_destMac(VectorEth::BROADCAST_MAC)
                    , m_u16EtherType(VectorEth::VECTOR_ETH_DEFAULT_ETHERTYPE)
                    , m_rxFilterSrcMac()
                    , m_rxFilterEtherType()
                    , m_phyConfig()
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32ReadBufferSize(1500U)
        {
            #define VECTOR_ETH_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<VectorEthPlugin>{&VectorEthPlugin::m_VECTOR_ETH_##a, VECTOR_ETH_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            VECTOR_ETH_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  VECTOR_ETH_PLUGIN_CMD_RECORD
        }

        ~VectorEthPlugin() = default;

        bool isInitialized( void ) const { return m_bIsInitialized; }
        bool isEnabled (void) const { return m_bIsEnabled; }

        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<VectorEthPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<VectorEthPlugin>(this, psGetParams);
        }

        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = true;
            return m_bIsInitialized;
        }

        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
        }

        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<VectorEthPlugin>(this, strCmd, strParams, st);
        }

        const PluginCommandsMap<VectorEthPlugin> *getMap(void) const { return &m_mapCmds; }
        const std::string& getVersion(void) const { return m_strVersion; }
        const std::string& getData(void) const { return m_strResultData; }
        void resetData(void) const { m_strResultData.clear(); }

        bool setRawResult (const std::string& strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        bool setCyclicCached (const std::string& strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
        }

        bool isFaultTolerant (void) const { return m_bIsFaultTolerant; }
        bool isPrivileged (void) const { return m_bIsPrivileged; }

        /**
          * \brief get the Vector Hardware Config application name
        */
        const char *getAppName (void) const { return m_strAppName.c_str(); }

        /**
          * \brief set the Vector Hardware Config application name.
          *        Must match an entry created in "Vector Hardware Config"
          *        with an Ethernet channel assigned to it.
        */
        void setAppName (const std::string& strAppName) const
        {
            m_strAppName.assign(strAppName);
        }

        /**
          * \brief set the zero-based channel index within this application's assignment
        */
        bool setAppChannel (const std::string& strChannel) const
        {
            return numeric::str2uint32(strChannel, m_u32AppChannel);
        }

        /**
          * \brief select a physical channel directly by device type, bypassing
          *        Vector Hardware Config entirely - see VectorPlugin::setDeviceHw()
          *        for the full rationale, identical here (shares Vector::hwTypeFromString()).
        */
        bool setDeviceHw (const std::string& strHw) const
        {
            uint32_t u32HwType = 0U;
            if (false == Vector::hwTypeFromString(strHw, u32HwType)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("Unknown device type, run VECTOR_ETH.DEVICES to see what's connected:");
                          LOG_STRING(strHw.c_str()));
                return false;
            }
            m_strDeviceHw = strHw;
            m_u32DeviceHw = u32HwType;
            m_bDeviceHwSet = true;
            return true;
        }

        bool setDeviceSerial (const std::string& strSerial) const
        {
            return numeric::str2uint32(strSerial, m_u32DeviceSerial);
        }

        bool setDeviceName (const std::string& strName) const
        {
            if (strName.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |"); LOG_STRING("Device name cannot be empty"));
                return false;
            }
            m_strDeviceName = strName;
            return true;
        }

        bool setDeviceHwIndex (const std::string& strHwIndex) const
        {
            if (false == numeric::str2uint32(strHwIndex, m_u32DeviceHwIndex)) {
                return false;
            }
            m_bDeviceHwIndexSet = true;
            return true;
        }

        bool setDeviceHwChannel (const std::string& strHwChannel) const
        {
            if (false == numeric::str2uint32(strHwChannel, m_u32DeviceHwChannel)) {
                return false;
            }
            m_bDeviceHwChannelSet = true;
            return true;
        }

        bool isUsingDirectSelection (void) const
        {
            return m_bDeviceHwSet || (m_u32DeviceSerial != 0U) || !m_strDeviceName.empty();
        }

        /**
          * \brief set the default destination MAC address for outgoing frames
          *        ("AA:BB:CC:DD:EE:FF", or "broadcast" for FF:FF:FF:FF:FF:FF).
          *        Overridable per CMD/SCRIPT/CYCLIC call - see class comment.
        */
        bool setDestMac (const std::string& strMac) const
        {
            if (0 == ustring_icompare(strMac, "broadcast")) {
                m_destMac = VectorEth::BROADCAST_MAC;
                return true;
            }
            VectorEth::MacAddress mac;
            if (false == VectorEth::parseMac(strMac, mac)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("Malformed MAC address (expected AA:BB:CC:DD:EE:FF):"); LOG_STRING(strMac.c_str()));
                return false;
            }
            m_destMac = mac;
            return true;
        }

        /**
          * \brief set the EtherType stamped on outgoing frames (decimal or 0x-hex,
          *        e.g. "0x0800" for IPv4). Default 0x88B5. Overridable per call.
        */
        bool setEtherType (const std::string& strType) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strType, u32Val)) || (u32Val > 0xFFFFU)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("EtherType must fit in 16 bits:"); LOG_UINT32(u32Val));
                return false;
            }
            m_u16EtherType = static_cast<uint16_t>(u32Val);
            return true;
        }

        /**
          * \brief RX filter: only accept frames from this source MAC. Empty clears the filter
          *        (accept any source) - see VectorEth::setRxFilterSrcMac().
        */
        bool setRxFilterSrcMac (const std::string& strMac) const
        {
            if (strMac.empty()) {
                m_rxFilterSrcMac.reset();
                return true;
            }
            VectorEth::MacAddress mac;
            if (false == VectorEth::parseMac(strMac, mac)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("Malformed filter MAC address:"); LOG_STRING(strMac.c_str()));
                return false;
            }
            m_rxFilterSrcMac = mac;
            return true;
        }

        /**
          * \brief RX filter: only accept frames with this EtherType. Empty clears the filter
          *        (accept any type).
        */
        bool setRxFilterEtherType (const std::string& strType) const
        {
            if (strType.empty()) {
                m_rxFilterEtherType.reset();
                return true;
            }
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strType, u32Val)) || (u32Val > 0xFFFFU)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("Filter EtherType must fit in 16 bits:"); LOG_UINT32(u32Val));
                return false;
            }
            m_rxFilterEtherType = static_cast<uint16_t>(u32Val);
            return true;
        }

        /**
          * \brief PHY link speed: "auto100", "auto1000", "auto" (or "auto100or1000",
          *        the default), "10", "100", "1000" (the last three disable
          *        auto-negotiation - see XL_ETH_MODE_SPEED_FIXED_*).
        */
        bool setEthSpeed (const std::string& strSpeed) const
        {
            static const std::pair<const char*, unsigned int> table[] = {
                { "auto",         XL_ETH_MODE_SPEED_AUTO_100_1000 },
                { "auto100or1000",XL_ETH_MODE_SPEED_AUTO_100_1000 },
                { "auto100",      XL_ETH_MODE_SPEED_AUTO_100      },
                { "auto1000",     XL_ETH_MODE_SPEED_AUTO_1000     },
                { "10",           XL_ETH_MODE_SPEED_FIXED_10      },
                { "100",          XL_ETH_MODE_SPEED_FIXED_100     },
                { "1000",         XL_ETH_MODE_SPEED_FIXED_1000    },
            };
            for (const auto& e : table) {
                if (0 == ustring_icompare(strSpeed, e.first)) {
                    m_phyConfig.u32Speed = e.second;
                    return true;
                }
            }
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                      LOG_STRING("Unknown speed (expected auto/auto100/auto1000/10/100/1000):");
                      LOG_STRING(strSpeed.c_str()));
            return false;
        }

        /** PHY duplex: "auto" (default), "half", "full". */
        bool setEthDuplex (const std::string& strDuplex) const
        {
            static const std::pair<const char*, unsigned int> table[] = {
                { "auto", XL_ETH_MODE_DUPLEX_AUTO },
                { "half", XL_ETH_MODE_DUPLEX_HALF },
                { "full", XL_ETH_MODE_DUPLEX_FULL },
            };
            for (const auto& e : table) {
                if (0 == ustring_icompare(strDuplex, e.first)) {
                    m_phyConfig.u32Duplex = e.second;
                    return true;
                }
            }
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                      LOG_STRING("Unknown duplex (expected auto/half/full):"); LOG_STRING(strDuplex.c_str()));
            return false;
        }

        /** Physical connector: "dontcare" (default), "rj45", "dsub" - only relevant on multi-connector boards (VN5610(A)). */
        bool setEthConnector (const std::string& strConnector) const
        {
            static const std::pair<const char*, unsigned int> table[] = {
                { "dontcare", XL_ETH_MODE_CONNECTOR_DONT_CARE },
                { "rj45",     XL_ETH_MODE_CONNECTOR_RJ45      },
                { "dsub",     XL_ETH_MODE_CONNECTOR_DSUB      },
            };
            for (const auto& e : table) {
                if (0 == ustring_icompare(strConnector, e.first)) {
                    m_phyConfig.u32Connector = e.second;
                    return true;
                }
            }
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                      LOG_STRING("Unknown connector (expected dontcare/rj45/dsub):"); LOG_STRING(strConnector.c_str()));
            return false;
        }

        /** PHY layer: "dontcare" (default), "8023" (IEEE 802.3), "broadr" (BroadR-Reach). */
        bool setEthPhy (const std::string& strPhy) const
        {
            static const std::pair<const char*, unsigned int> table[] = {
                { "dontcare", XL_ETH_MODE_PHY_DONT_CARE    },
                { "8023",     XL_ETH_MODE_PHY_IEEE_802_3   },
                { "broadr",   XL_ETH_MODE_PHY_BROADR_REACH },
            };
            for (const auto& e : table) {
                if (0 == ustring_icompare(strPhy, e.first)) {
                    m_phyConfig.u32Phy = e.second;
                    return true;
                }
            }
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                      LOG_STRING("Unknown phy (expected dontcare/8023/broadr):"); LOG_STRING(strPhy.c_str()));
            return false;
        }

        bool setEthReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        bool setEthWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set the local read buffer size in bytes (1-1500, the Ethernet MTU this
          *        driver frames against - see VectorEth::VECTOR_ETH_MAX_PAYLOAD).
        */
        bool setEthReadBufferSize (const std::string& strReadBufferSize) const
        {
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > static_cast<uint32_t>(VectorEth::VECTOR_ETH_MAX_PAYLOAD)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR_ETH |");
                          LOG_STRING("ReadBufSize out of range [1-1500]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32ReadBufferSize = u32Size;
            return true;
        }

    private:

        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /** case-insensitive string compare helper (avoids pulling in a whole locale-aware comparator for this) */
        static int ustring_icompare(const std::string& a, const char* b);

        /**
          * \brief helper: parse FILTER's "src=<mac> type=<ethertype>" argument string
          *        (space- or comma-separated, either/both keys optional) into
          *        outSrc/outType. An omitted key leaves the corresponding
          *        std::optional untouched at its caller-supplied default (so a
          *        bare "VECTOR_ETH.FILTER type=0x0800" doesn't clobber a
          *        previously set src= filter, and vice versa) - clearing a
          *        filter entirely is done with an explicit empty value, e.g.
          *        "src= type=0x0800".
        */
        bool m_ParseFilter (const std::string& strFilter,
                            std::optional<VectorEth::MacAddress>& outSrc,
                            std::optional<uint16_t>&              outType) const;

        /**
          * \brief Open the VectorEth channel with the current configuration parameters.
          *        Returns a ready-to-use VectorEth driver instance, or nullptr if any
          *        step failed (already logged).
        */
        std::shared_ptr<VectorEth> m_OpenAndConfigure (void) const;

        PluginCommandsMap<VectorEthPlugin> m_mapCmds;
        std::string m_strVersion;
        std::string m_strInstanceName;
        bool m_bIsInitialized;
        bool m_bIsEnabled;
        bool m_bIsFaultTolerant;
        bool m_bIsPrivileged;
        mutable std::string m_strResultData;
        mutable bool m_bRawResult;
        mutable bool m_bCyclicCached;
        std::string m_strArtefactsPath;

        mutable std::string m_strAppName;
        mutable uint32_t m_u32AppChannel;

        mutable std::string m_strDeviceHw;
        mutable bool        m_bDeviceHwSet;
        mutable uint32_t    m_u32DeviceHw;
        mutable uint32_t    m_u32DeviceSerial;
        mutable std::string m_strDeviceName;
        mutable bool        m_bDeviceHwIndexSet;
        mutable uint32_t    m_u32DeviceHwIndex;
        mutable bool        m_bDeviceHwChannelSet;
        mutable uint32_t    m_u32DeviceHwChannel;

        mutable VectorEth::MacAddress m_destMac;
        mutable uint16_t               m_u16EtherType;
        mutable std::optional<VectorEth::MacAddress> m_rxFilterSrcMac;
        mutable std::optional<uint16_t>              m_rxFilterEtherType;
        mutable VectorEth::PhyConfig    m_phyConfig;

        mutable uint32_t m_u32ReadTimeout;
        mutable uint32_t m_u32WriteTimeout;
        mutable uint32_t m_u32ReadBufferSize;

        #define VECTOR_ETH_PLUGIN_CMD_RECORD(a, ...)  bool m_VECTOR_ETH_##a ( const std::string& args, std::stop_token st ) const;
        VECTOR_ETH_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  VECTOR_ETH_PLUGIN_CMD_RECORD
};

#endif /* VECTOR_ETH_PLUGIN_HPP */
