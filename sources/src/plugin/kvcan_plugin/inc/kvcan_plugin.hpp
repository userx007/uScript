#ifndef KVCAN_PLUGIN_HPP
#define KVCAN_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uKVCan.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define KVCAN_PLUGIN_VERSION    "1.0.0.0"
#define KVCAN_PLUGIN_NAME       "KVCAN"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// KVCAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef KVCAN_GET_BLOCKING
#define KVCAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define KVCAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
KVCAN_PLUGIN_CMD_RECORD( INFO               ) \
KVCAN_PLUGIN_CMD_RECORD( CONFIG             ) \
KVCAN_PLUGIN_CMD_RECORD( FILTER             ) \
KVCAN_PLUGIN_CMD_RECORD( CMD                ) \
KVCAN_PLUGIN_CMD_RECORD( SCRIPT             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief KVCAN plugin class definition.
  *
  * Wraps the KVCAN SocketKVCAN driver and exposes it through the standard
  * PluginInterface dispatch model.  Works with any SocketKVCAN interface
  * (physical canN or virtual vcanN).
  *
  * Extra command vs UART/I2C/SPI plugins:
  *   FILTER — installs KVCAN hardware acceptance filters at runtime
  *             without reopening the socket.
*/
class KVCANPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        KVCANPlugin() : m_strVersion(KVCAN_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_u32CanTxId(0U)
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32CanReadBufferSize(8U)
        {
            #define KVCAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<KVCANPlugin>{&KVCANPlugin::m_KVCAN_##a, KVCAN_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            KVCAN_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  KVCAN_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~KVCANPlugin() = default;

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

            if (true == generic_setparams<KVCANPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<KVCANPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<KVCANPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<KVCANPlugin> *getMap(void) const
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
          * \brief get SocketKVCAN interface name
        */
        const char *getCanIface (void) const
        {
            return m_strCanIface.c_str();
        }

        /**
          * \brief set SocketKVCAN interface name (e.g. "vcan0", "can1")
        */
        void setCanIface (const std::string& strCanIface) const
        {
            m_strCanIface.assign(strCanIface);
        }

        /**
          * \brief set the KVCAN ID stamped on outgoing frames.
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention:
          *          - 11-bit standard IDs (<=0x7FF): stored as-is.
          *          - 29-bit extended IDs (>0x7FF) : CAN_EFF_FLAG (0x80000000)
          *            is set automatically if the caller did not set it already,
          *            so both "x:0x18DAF100" and "x:0x98DAF100" select EFF mode.
        */
        bool setCanTxId (const std::string& strTxId) const
        {
            static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
            static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
            static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

            uint32_t u32Id = 0U;
            if (false == numeric::str2uint32(strTxId, u32Id)) {
                return false;
            }

            // Auto-set EFF flag when the id exceeds the 11-bit SFF range
            // and the caller did not already set the flag explicitly.
            if (!(u32Id & CAN_EFF_FLAG) && ((u32Id & CAN_EFF_MASK) > CAN_SFF_MASK)) {
                u32Id |= CAN_EFF_FLAG;
            }

            // Clamp data bits to the legal range for the chosen frame format.
            // Mirrors the fixup in m_ParseFilters so INI-file and CONFIG-command
            // IDs are treated identically and no junk bits reach the driver.
            if (u32Id & CAN_EFF_FLAG) {
                u32Id &= (CAN_EFF_FLAG | CAN_EFF_MASK); // preserve flag + 29 data bits
            } else {
                u32Id &= CAN_SFF_MASK;                  // keep only 11 data bits
            }

            m_u32CanTxId = u32Id;
            return true;
        }

        /**
          * \brief set KVCAN read timeout
        */
        bool setCanReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set KVCAN write timeout
        */
        bool setCanWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set KVCAN read buffer size
          * \note Valid range is 1–64 bytes (maximum CAN FD payload).
        */
        bool setCanReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t CAN_FD_MAX_DLEN = 64U;
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > CAN_FD_MAX_DLEN) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KVCAN |");
                          LOG_STRING("ReadBufSize out of range [1-64]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32CanReadBufferSize = u32Size;
            return true;
        }

    private:

        /**
          * \brief message sender
        */
        bool m_Send (std::span<const uint8_t> data, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief message receiver
        */
        bool m_Receive (std::span<uint8_t> data, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief helper: parse a comma-separated filter list string into KVCAN::CanFilter entries.
          *        Each entry has the form "<id>:<mask>" (hex or decimal).
          *        Example: "0x100:0x7FF,0x200:0x7FF"
          *
          *        The function automatically propagates CAN_EFF_FLAG / CAN_RTR_FLAG /
          *        CAN_ERR_FLAG from can_id into can_mask so SocketCAN's kernel filter
          *        comparison ((frame_id & mask) == (id & mask)) is unambiguous.
          *        An id > 0x7FF without CAN_EFF_FLAG set triggers a warning and the
          *        flag is added automatically.
        */
        bool m_ParseFilters (const std::string& strFilters, std::vector<KVCAN::CanFilter>& vFilters) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<KVCANPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

        /**
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

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
          * \brief SocketKVCAN interface name (e.g. "vcan0", "can1")
        */
        mutable std::string m_strCanIface;

        /**
          * \brief KVCAN ID stamped on every outgoing frame
        */
        mutable uint32_t m_u32CanTxId;

        /**
          * \brief KVCAN read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief KVCAN write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for KVCAN read operations (max 64 bytes for KVCAN FD)
        */
        mutable uint32_t m_u32CanReadBufferSize;

        /**
          * \brief acceptance filters applied to the open socket (empty = accept all)
        */
        mutable std::vector<KVCAN::CanFilter> m_vFilters;

        /**
          * \brief functions associated to the plugin commands
        */
        #define KVCAN_PLUGIN_CMD_RECORD(a, ...)  bool m_KVCAN_##a ( const std::string& args, std::stop_token st ) const;
        KVCAN_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  KVCAN_PLUGIN_CMD_RECORD
};

#endif /* KVCAN_PLUGIN_HPP */
