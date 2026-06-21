#ifndef PCAN_PLUGIN_HPP
#define PCAN_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uPcan.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <memory>


///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define PCAN_PLUGIN_VERSION    "1.0.0.0"
#define PCAN_PLUGIN_NAME       "PCAN"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// PCAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef PCAN_GET_BLOCKING
#define PCAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define PCAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
PCAN_PLUGIN_CMD_RECORD( INFO               ) \
PCAN_PLUGIN_CMD_RECORD( CONFIG             ) \
PCAN_PLUGIN_CMD_RECORD( FILTER             ) \
PCAN_PLUGIN_CMD_RECORD( CMD                ) \
PCAN_PLUGIN_CMD_RECORD( SCRIPT             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief PCAN plugin class definition.
  *
  * Wraps the PCAN-Basic driver (PEAK-System PCAN hardware) and exposes it
  * through the standard PluginInterface dispatch model.
  *
  * The command set is intentionally identical to the KVCAN and SLCAN plugins
  * at the command level (INFO / CONFIG / FILTER / CMD / SCRIPT) so that
  * scripts written for those plugins can be reused with minimal changes —
  * only the CONFIG key "i:" changes meaning (PCAN channel handle instead of
  * SocketCAN interface name or serial device path).
  *
  * Key differences vs KVCAN / SLCAN:
  *   - "i:" accepts a PCAN channel handle in decimal or 0x-hex format
  *     (e.g. "0x51" = PCAN_USBBUS1, "81" = same value in decimal).
  *   - "b:" sets the CAN bitrate in bps (e.g. "500000" for 500 kbps).
  *   - "e:" forces 29-bit extended frame format (0 = auto, 1 = force EFF).
  *   - "f:" enables CAN FD mode (0 = classic CAN, 1 = CAN FD).
  *   - FILTER uses a single comma-separated "<id>:<mask>" list, same syntax
  *     as KVCAN.FILTER, but the filter is applied as a software acceptance
  *     filter inside the driver (PCAN-Basic provides a single hardware
  *     acceptance filter; finer-grained filtering is done in software).
  *
  * Extra command vs UART/I2C/SPI plugins:
  *   FILTER — installs a software acceptance filter applied to every received
  *             frame, without reopening the PCAN channel.
*/
class PCANPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        PCANPlugin() : m_strVersion(PCAN_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_strPcanChannel("0x51")   // PCAN_USBBUS1 default
                    , m_u32Bitrate(500000U)
                    , m_bExtended(false)
                    , m_bFd(false)
                    , m_u32CanTxId(PCAN::PCAN_DEFAULT_TX_ID)
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32CanReadBufferSize(8U)
        {
            #define PCAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<PCANPlugin>{&PCANPlugin::m_PCAN_##a, PCAN_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            PCAN_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  PCAN_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~PCANPlugin() = default;

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

            if (true == generic_setparams<PCANPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<PCANPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<PCANPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<PCANPlugin> *getMap(void) const
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
          * \brief get the PCAN channel handle string (e.g. "0x51")
        */
        const char *getPcanChannel (void) const
        {
            return m_strPcanChannel.c_str();
        }

        /**
          * \brief set the PCAN channel handle string (decimal or 0x-hex)
          *        e.g. "0x51" = PCAN_USBBUS1, "81" = same value in decimal
        */
        void setPcanChannel (const std::string& strChannel) const
        {
            m_strPcanChannel.assign(strChannel);
        }

        /**
          * \brief set the CAN bitrate in bps (e.g. "500000" for 500 kbps)
          *        Supported values: 1000000, 800000, 500000, 250000, 125000,
          *                          100000, 95000, 83000, 50000, 47000, 33000,
          *                          20000, 10000, 5000
        */
        bool setPcanBitrate (const std::string& strBitrate) const
        {
            return numeric::str2uint32(strBitrate, m_u32Bitrate);
        }

        /**
          * \brief force 29-bit extended frame format for all outgoing frames
          *        "0" = auto-detect from TX ID (default), "1" = force EFF
        */
        bool setPcanExtended (const std::string& strExtended) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strExtended, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("PCAN |");
                          LOG_STRING("Extended must be 0 (auto) or 1 (force EFF):"); LOG_UINT32(u32Val));
                return false;
            }
            m_bExtended = (1U == u32Val);
            return true;
        }

        /**
          * \brief enable CAN FD mode
          *        "0" = classic CAN (default), "1" = CAN FD
        */
        bool setPcanFd (const std::string& strFd) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strFd, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("PCAN |");
                          LOG_STRING("FD must be 0 (classic) or 1 (FD):"); LOG_UINT32(u32Val));
                return false;
            }
            m_bFd = (1U == u32Val);
            return true;
        }

        /**
          * \brief set the CAN ID stamped on outgoing frames.
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention so it stays
          *        identical to the KVCAN and SLCAN plugin's setCanTxId:
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
            if (u32Id & CAN_EFF_FLAG) {
                u32Id &= (CAN_EFF_FLAG | CAN_EFF_MASK); // preserve flag + 29 data bits
            } else {
                u32Id &= CAN_SFF_MASK;                  // keep only 11 data bits
            }

            m_u32CanTxId = u32Id;
            return true;
        }

        /**
          * \brief set PCAN read timeout (ms)
        */
        bool setCanReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set PCAN write timeout (ms)
        */
        bool setCanWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set PCAN read buffer size
          * \note Valid range is 1-64 bytes (maximum CAN FD payload).
        */
        bool setCanReadBufferSize (const std::string& strReadBufferSize) const
        {
            static constexpr uint32_t CAN_FD_MAX_DLEN = 64U;
            uint32_t u32Size = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > CAN_FD_MAX_DLEN) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("PCAN |");
                          LOG_STRING("ReadBufSize out of range [1-64]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32CanReadBufferSize = u32Size;
            return true;
        }

    private:

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief helper: parse a comma-separated filter list string into a list of id:mask pairs.
          *        Each entry has the form "<id>:<mask>" (hex or decimal).
          *        Example: "0x100:0x7FF,0x18DAF100:0x1FFFFFFF"
          *
          *        Filters are applied in software (inside the driver recv loop) because
          *        PCAN-Basic exposes only one hardware acceptance filter slot.  The stored
          *        list is applied on every CMD/SCRIPT call without reopening the channel.
          *
          *        CAN_EFF_FLAG auto-correction mirrors the KVCAN plugin's m_ParseFilters:
          *        an id > 0x7FF without CAN_EFF_FLAG set triggers a warning and the flag
          *        is added automatically.
        */
        bool m_ParseFilters (const std::string& strFilters,
                             std::vector<std::pair<uint32_t,uint32_t>>& vFilters) const;

        /**
          * \brief Open the PCAN channel with the current configuration parameters.
          *        Returns a ready-to-use PCAN driver instance (compatible with
          *        CommScriptClient and CommScriptCommandInterpreter), or nullptr if
          *        any step failed (already logged).
        */
        std::shared_ptr<PCAN> m_OpenAndConfigure (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<PCANPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

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
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

        /**
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief PCAN channel handle string (decimal or 0x-hex, e.g. "0x51" = PCAN_USBBUS1)
        */
        mutable std::string m_strPcanChannel;

        /**
          * \brief CAN bitrate in bps applied when opening the channel
        */
        mutable uint32_t m_u32Bitrate;

        /**
          * \brief force 29-bit extended frame format for all outgoing frames
        */
        mutable bool m_bExtended;

        /**
          * \brief enable CAN FD mode
        */
        mutable bool m_bFd;

        /**
          * \brief CAN ID stamped on every outgoing frame (SocketCAN canid_t convention)
        */
        mutable uint32_t m_u32CanTxId;

        /**
          * \brief PCAN read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief PCAN write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for PCAN read operations (max 64 bytes for CAN FD)
        */
        mutable uint32_t m_u32CanReadBufferSize;

        /**
          * \brief software acceptance filters: list of (can_id, can_mask) pairs
          *        (empty = accept all)
        */
        mutable std::vector<std::pair<uint32_t,uint32_t>> m_vFilters;

        /**
          * \brief functions associated to the plugin commands
        */
        #define PCAN_PLUGIN_CMD_RECORD(a, ...)  bool m_PCAN_##a ( const std::string& args, std::stop_token st ) const;
        PCAN_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  PCAN_PLUGIN_CMD_RECORD
};

#endif /* PCAN_PLUGIN_HPP */
