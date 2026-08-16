#ifndef RAWETH_PLUGIN_HPP
#define RAWETH_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uRawEth.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdio>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define RAWETH_PLUGIN_VERSION    "1.0.0.0"
#define RAWETH_PLUGIN_NAME       "RAWETH"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// RAWETH_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef RAWETH_GET_BLOCKING
#define RAWETH_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define RAWETH_PLUGIN_COMMANDS_CONFIG_TABLE    \
RAWETH_PLUGIN_CMD_RECORD( INFO               ) \
RAWETH_PLUGIN_CMD_RECORD( CONFIG             ) \
RAWETH_PLUGIN_CMD_RECORD( CMD                ) \
RAWETH_PLUGIN_CMD_RECORD( SCRIPT             ) \
RAWETH_PLUGIN_CMD_RECORD( CYCLIC             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief RAWETH plugin class definition.
  *
  * Wraps the RawEth raw-Ethernet (AF_PACKET) driver and exposes it through
  * the standard PluginInterface dispatch model. Sends/receives frames on a
  * single configured interface (see uRawEth.hpp).
  *
  * Like KVCAN, RawEth is frame based rather than stream based — but unlike
  * KVCAN there is no FILTER command here: the only acceptance filtering
  * point is the bind() to interface + EtherType performed at open() time,
  * and the destination MAC / EtherType used for writes are configured the
  * same way TCPIP's host/port are, through CONFIG. The plugin surface is
  * therefore INFO / CONFIG / CMD / SCRIPT only, the same set as TCPIP.
*/
class RawEthPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        RawEthPlugin() : m_strVersion(RAWETH_PLUGIN_VERSION)
                    , m_strInstanceName(RAWETH_PLUGIN_NAME)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_bCyclicCached(true)
                    , m_strIface()
                    , m_destMac(RawEth::RAWETH_BROADCAST_MAC)
                    , m_u16EtherType(0U)
                    , m_bPromiscuous(false)
                    , m_u32ReadTimeout(RawEth::RAWETH_READ_DEFAULT_TIMEOUT)
                    , m_u32WriteTimeout(RawEth::RAWETH_WRITE_DEFAULT_TIMEOUT)
                    , m_u32ReadBufferSize(RawEth::RAWETH_MAX_BUFLENGTH)
        {
            #define RAWETH_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<RawEthPlugin>{&RawEthPlugin::m_RAWETH_##a, RAWETH_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            RAWETH_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  RAWETH_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~RawEthPlugin() = default;

        /**
          * \brief get the plugin initialization status
        */
        bool isInitialized( void ) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool isEnabled (void) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<RawEthPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<RawEthPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<RawEthPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<RawEthPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion(void) const
        {
            return m_strVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& getData(void) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void resetData(void) const
        {
            m_strResultData.clear();
        }
        
        /**
          * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
        */
        bool setRawResult (const std::string& strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /**
          * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
        */
        bool setCyclicCached (const std::string& strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
        }

        /**
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitly after loading the plugin
        */
        bool doInit(void *pvUserData);

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitly before closing/freeing the shared library
        */
        void doCleanup(void);

        /**
          * \brief get fault tolerant flag status
        */
        bool isFaultTolerant (void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged (void) const
        {
            return m_bIsPrivileged;
        }

        /**
          * \brief get the configured interface name (e.g. "eth0")
        */
        const char *getIface (void) const
        {
            return m_strIface.c_str();
        }

        /**
          * \brief set the interface name.
          *        Rejects empty names and names that would not fit a Linux
          *        ifreq (IFNAMSIZ, i.e. 15 usable characters).
        */
        bool setIface (const std::string& strIface) const
        {
            static constexpr size_t RAWETH_IFACE_NAME_MAX = 15; // IFNAMSIZ - 1

            if (strIface.empty() || strIface.size() > RAWETH_IFACE_NAME_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("Interface name empty or too long:"); LOG_STRING(strIface.c_str()));
                return false;
            }

            m_strIface.assign(strIface);
            return true;
        }

        /**
          * \brief get the configured default destination MAC address
        */
        const RawEth::MacAddr& getDestMac (void) const
        {
            return m_destMac;
        }

        /**
          * \brief set the default destination MAC address.
          *        Accepts exactly "AA:BB:CC:DD:EE:FF" (colon-separated hex).
        */
        bool setDestMac (const std::string& strMac) const
        {
            if (strMac.size() != 17) { // "AA:BB:CC:DD:EE:FF" is exactly 17 characters
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("Malformed MAC address:"); LOG_STRING(strMac.c_str()));
                return false;
            }

            unsigned int auiBytes[RawEth::RAWETH_MAC_ADDR_LEN];
            const int iParsed = std::sscanf(strMac.c_str(),
                                            "%02x:%02x:%02x:%02x:%02x:%02x",
                                            &auiBytes[0], &auiBytes[1], &auiBytes[2],
                                            &auiBytes[3], &auiBytes[4], &auiBytes[5]);
            if (iParsed != static_cast<int>(RawEth::RAWETH_MAC_ADDR_LEN)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("Unparsable MAC address:"); LOG_STRING(strMac.c_str()));
                return false;
            }

            for (size_t i = 0; i < RawEth::RAWETH_MAC_ADDR_LEN; ++i) {
                m_destMac[i] = static_cast<uint8_t>(auiBytes[i]);
            }
            return true;
        }

        /**
          * \brief get the configured EtherType (0 = driver default, RawEth::RAWETH_DEFAULT_ETHERTYPE)
        */
        uint16_t getEtherType (void) const
        {
            return m_u16EtherType;
        }

        /**
          * \brief set the EtherType. Accepts hex, with or without a "0x" prefix
          *        (e.g. "88b5" or "0x88B5"). "0" selects the driver default.
        */
        bool setEtherType (const std::string& strEtherType) const
        {
            std::string strValue = strEtherType;
            if (strValue.size() >= 2 && strValue[0] == '0' && (strValue[1] == 'x' || strValue[1] == 'X')) {
                strValue.erase(0, 2);
            }
            if (strValue.empty() || strValue.size() > 4) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("Malformed EtherType:"); LOG_STRING(strEtherType.c_str()));
                return false;
            }

            unsigned int uiEtherType = 0;
            const int iParsed = std::sscanf(strValue.c_str(), "%x", &uiEtherType);
            if (iParsed != 1 || uiEtherType > 0xFFFFU) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("Unparsable EtherType:"); LOG_STRING(strEtherType.c_str()));
                return false;
            }

            m_u16EtherType = static_cast<uint16_t>(uiEtherType);
            return true;
        }

        /**
          * \brief get the promiscuous-mode flag
        */
        bool getPromiscuous (void) const
        {
            return m_bPromiscuous;
        }

        /**
          * \brief set the promiscuous-mode flag from "0"/"1" (any nonzero value enables it)
        */
        bool setPromiscuous (const std::string& strFlag) const
        {
            uint32_t u32Value = 0U;
            if (false == numeric::str2uint32(strFlag, u32Value)) {
                return false;
            }
            m_bPromiscuous = (u32Value != 0U);
            return true;
        }

        /**
          * \brief set RawEth read timeout (milliseconds, 0 = use driver default)
        */
        bool setReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set RawEth write timeout (milliseconds, 0 = use driver default)
        */
        bool setWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set RawEth read buffer size
          * \note Valid range is 1-RAWETH_MAX_BUFLENGTH bytes (delimiter/token
          *       modes assemble into a buffer of this size; Exact mode caps
          *       a single frame's payload copy at this size too).
        */
        bool setRawEthReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t RAWETH_BUF_MAX = static_cast<uint32_t>(RawEth::RAWETH_MAX_BUFLENGTH);
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > RAWETH_BUF_MAX) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("RAWETH |");
                          LOG_STRING("ReadBufSize out of range [1-256]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32ReadBufferSize = u32Size;
            return true;
        }

    private:

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief helper: open a fresh RawEth driver instance against the
          *        configured interface/destination/EtherType. Returns nullptr
          *        (and logs) on failure, mirroring the way
          *        m_TCPIP_CMD/m_TCPIP_SCRIPT open a fresh TCPIP driver per
          *        invocation in the TCPIP plugin.
          * \note  Returns the concrete RawEth type (rather than ICommDriver)
          *        so it can be handed directly to
          *        CommScriptCommandInterpreter<RawEth> / CommScriptClient<RawEth>.
        */
        std::shared_ptr<RawEth> m_OpenDriver (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<RawEthPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;


        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "RAWETH" or "RAWETH:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
        /**
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

        /**
          * \brief when true, CMD returns the raw received bytes as-is instead of
          *        hexlifying them (see ucmdexec::generic_cmd()'s bRawResult parameter);
          *        settable via the ini file's RAW_RESULT key or the CONFIG command's
          *        raw= token (see ucmdexec::RAW_RESULT_INI_KEY / RAW_RESULT_CONFIG_KEY)
        */
        mutable bool m_bRawResult;

        /**
          * \brief CYCLIC caching mode: true (default) validates/parses each CYCLIC entry's
          *        command exactly once for the whole session; false re-resolves and re-validates
          *        every due entry on every tick, needed to track a volatile ("?=") macro used as
          *        one entry's val/id - settable via the ini file's CYCLIC_CACHED key or the CONFIG
          *        command's cached= token (see ucmdexec::CYCLIC_CACHED_INI_KEY / CYCLIC_CACHED_CONFIG_KEY
          *        and ucmdexec::generic_send_cyclic()'s bCached parameter)
        */
        mutable bool m_bCyclicCached;

        /**
          * \brief plugin initialization status
        */
        bool m_bIsInitialized;

        /**
          * \brief plugin enabling status
        */
        bool m_bIsEnabled;

        /**
          * \brief plugin fault tolerant mode
        */
        bool m_bIsFaultTolerant;

        /**
          * \brief plugin is privileged
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief configured interface name (e.g. "eth0")
        */
        mutable std::string m_strIface;

        /**
          * \brief default destination MAC address used for writes
        */
        mutable RawEth::MacAddr m_destMac;

        /**
          * \brief EtherType (0 = use RawEth::RAWETH_DEFAULT_ETHERTYPE)
        */
        mutable uint16_t m_u16EtherType;

        /**
          * \brief promiscuous-mode flag applied at open()
        */
        mutable bool m_bPromiscuous;

        /**
          * \brief RawEth read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief RawEth write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for RawEth read operations (max RawEth::RAWETH_MAX_BUFLENGTH bytes)
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define RAWETH_PLUGIN_CMD_RECORD(a, ...)  bool m_RAWETH_##a ( const std::string& args, std::stop_token st ) const;
        RAWETH_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  RAWETH_PLUGIN_CMD_RECORD
};

#endif /* RAWETH_PLUGIN_HPP */
