#ifndef SLCAN_PLUGIN_HPP
#define SLCAN_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uSlcan.hpp"
#include "slcan_frame_driver.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <memory>
#include <optional>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define SLCAN_PLUGIN_VERSION    "1.0.0.0"
#define SLCAN_PLUGIN_NAME       "SLCAN       |"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// SLCAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef SLCAN_GET_BLOCKING
#define SLCAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
SLCAN_PLUGIN_CMD_RECORD( INFO               ) \
SLCAN_PLUGIN_CMD_RECORD( CONFIG             ) \
SLCAN_PLUGIN_CMD_RECORD( FILTER             ) \
SLCAN_PLUGIN_CMD_RECORD( CMD                ) \
SLCAN_PLUGIN_CMD_RECORD( SCRIPT             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief SLCAN plugin class definition.
  *
  * Wraps the SLCAN driver (WeActStudio USB2CANFDV1 ASCII protocol over UART)
  * and exposes it through the standard PluginInterface dispatch model.
  *
  * Unlike the SocketCAN-backed KVCAN plugin — where the kernel already knows
  * the bus bit rate and a single setsockopt() call installs filters on a
  * socket that is always "open" at the OS level — an SLCAN adapter only
  * learns its bit rate, FD data rate, bus mode, auto-retransmission setting
  * and acceptance filters from ASCII commands sent over the same serial
  * link, and only while the CAN channel itself is closed. CONFIG therefore
  * carries several extra keys (bit rate, FD data rate, mode, auto-retx,
  * BRS) that have no KVCAN equivalent, and every CMD/SCRIPT call re-applies
  * all of them before opening the channel.
  *
  * Extra command vs UART/I2C/SPI plugins:
  *   FILTER — installs the adapter's standard/extended acceptance filters
  *             (one slot each — see m_ParseFilters) before the next open.
*/
class SLCANPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        SLCANPlugin() : m_strVersion(SLCAN_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_u32UartBaud(115200U)
                    , m_eBitrate(CanBitrate::BR_125K)
                    , m_eFdDataRate(CanFdDataRate::FD_2M)
                    , m_eMode(CanMode::Normal)
                    , m_eAutoRetx(CanAutoRetx::Disabled)
                    , m_bFdBrs(true)
                    , m_u32CanTxId(0U)
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32CanReadBufferSize(8U)
        {
            #define SLCAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<SLCANPlugin>{&SLCANPlugin::m_SLCAN_##a, SLCAN_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  SLCAN_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~SLCANPlugin() = default;

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

            if (true == generic_setparams<SLCANPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<SLCANPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<SLCANPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<SLCANPlugin> *getMap(void) const
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
          * \brief get the UART device path used to reach the SLCAN adapter
        */
        const char *getDevice (void) const
        {
            return m_strDevice.c_str();
        }

        /**
          * \brief set the UART device path (e.g. "/dev/ttyACM0", "COM3")
        */
        void setDevice (const std::string& strDevice) const
        {
            m_strDevice.assign(strDevice);
        }

        /**
          * \brief set the UART baud rate used to talk to the adapter itself
          * \note This is the serial link speed, not the CAN bus bit rate (see setCanBitrate).
        */
        bool setUartBaud (const std::string& strBaud) const
        {
            return numeric::str2uint32(strBaud, m_u32UartBaud);
        }

        /**
          * \brief set the nominal CAN bit rate preset (SLCAN 'S' command)
          * \note Accepts the numeric value of the CanBitrate enum (0-13, i.e. S0-SD).
        */
        bool setCanBitrate (const std::string& strBitrate) const
        {
            uint32_t u32Val = 0U;
            if (false == numeric::str2uint32(strBitrate, u32Val)) {
                return false;
            }
            if (u32Val > static_cast<uint32_t>(CanBitrate::BR_5K)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("Bitrate preset out of range [0-13]:"); LOG_UINT32(u32Val));
                return false;
            }
            m_eBitrate = static_cast<CanBitrate>(u32Val);
            return true;
        }

        /**
          * \brief set the CAN-FD data segment bit rate preset (SLCAN 'Y' command)
          * \note Accepts the numeric value of the CanFdDataRate enum (1-5, i.e. Y1-Y5).
          *       Only meaningful for frames sent with the CAN-FD BRS flag (see setCanFdBrs).
        */
        bool setCanFdDataRate (const std::string& strFdRate) const
        {
            uint32_t u32Val = 0U;
            if (false == numeric::str2uint32(strFdRate, u32Val)) {
                return false;
            }
            if ((u32Val < static_cast<uint32_t>(CanFdDataRate::FD_1M)) ||
                (u32Val > static_cast<uint32_t>(CanFdDataRate::FD_5M))) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("FD data rate preset out of range [1-5]:"); LOG_UINT32(u32Val));
                return false;
            }
            m_eFdDataRate = static_cast<CanFdDataRate>(u32Val);
            return true;
        }

        /**
          * \brief set the bus mode (SLCAN 'M' command): 0 = normal, 1 = silent/listen-only
        */
        bool setCanMode (const std::string& strMode) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strMode, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("Mode must be 0 (normal) or 1 (silent):"); LOG_UINT32(u32Val));
                return false;
            }
            m_eMode = static_cast<CanMode>(u32Val);
            return true;
        }

        /**
          * \brief set auto-retransmission (SLCAN 'A' command): 0 = off (default), 1 = on
        */
        bool setCanAutoRetx (const std::string& strRetx) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strRetx, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("AutoRetx must be 0 (off) or 1 (on):"); LOG_UINT32(u32Val));
                return false;
            }
            m_eAutoRetx = static_cast<CanAutoRetx>(u32Val);
            return true;
        }

        /**
          * \brief enable/disable the Bit Rate Switch flag on outgoing CAN-FD frames
          * \note Ignored for classic CAN frames (payload <= 8 bytes); 0 = off, 1 = on (default)
        */
        bool setCanFdBrs (const std::string& strBrs) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strBrs, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("FdBrs must be 0 (off) or 1 (on):"); LOG_UINT32(u32Val));
                return false;
            }
            m_bFdBrs = (1U == u32Val);
            return true;
        }

        /**
          * \brief set the CAN ID stamped on outgoing frames.
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention so it stays
          *        identical to the KVCAN plugin's setCanTxId:
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
          * \brief set SLCAN read timeout
        */
        bool setCanReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set SLCAN write timeout
        */
        bool setCanWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set SLCAN read buffer size
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
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
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
          * \brief helper: parse a comma-separated filter list string into the adapter's
          *        single standard-filter and single extended-filter slots.
          *        Each entry has the form "<id>:<mask>" (hex or decimal).
          *        Example: "0x100:0x7FF,0x18DAF100:0x1FFFFFFF"
          *
          *        Unlike SocketCAN (arbitrary number of kernel filters), the WeActStudio
          *        adapter exposes exactly one standard (f) and one extended (F) filter slot,
          *        so at most one entry of each kind is accepted; a second entry of the same
          *        kind is a parse error. An id > 0x7FF without CAN_EFF_FLAG set triggers a
          *        warning and is treated as extended automatically (mirrors setCanTxId).
        */
        bool m_ParseFilters (const std::string& strFilters) const;

        /**
          * \brief Open the UART, push bit rate/FD rate/mode/auto-retx/filters (channel must
          *        be closed for all of these), then open the CAN channel.
          *        Returns a ready-to-use SLCANFrameDriver (compatible with CommScriptClient
          *        and CommScriptCommandInterpreter), or nullptr if any step failed (already logged).
        */
        std::shared_ptr<SLCANFrameDriver> m_OpenAndConfigure (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<SLCANPlugin> m_mapCmds;

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
          * \brief UART device path used to reach the SLCAN adapter (e.g. "/dev/ttyACM0")
        */
        mutable std::string m_strDevice;

        /**
          * \brief UART baud rate used to talk to the adapter
        */
        mutable uint32_t m_u32UartBaud;

        /**
          * \brief nominal CAN bit rate preset applied before every channel open
        */
        mutable CanBitrate m_eBitrate;

        /**
          * \brief CAN-FD data segment bit rate preset applied before every channel open
        */
        mutable CanFdDataRate m_eFdDataRate;

        /**
          * \brief bus mode (normal / silent) applied before every channel open
        */
        mutable CanMode m_eMode;

        /**
          * \brief auto-retransmission setting applied before every channel open
        */
        mutable CanAutoRetx m_eAutoRetx;

        /**
          * \brief whether outgoing CAN-FD frames request the Bit Rate Switch
        */
        mutable bool m_bFdBrs;

        /**
          * \brief CAN ID stamped on every outgoing frame
        */
        mutable uint32_t m_u32CanTxId;

        /**
          * \brief SLCAN read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief SLCAN write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for SLCAN read operations (max 64 bytes for CAN FD)
        */
        mutable uint32_t m_u32CanReadBufferSize;

        /**
          * \brief standard (11-bit) acceptance filter — id/mask — applied to the open channel
        */
        mutable std::optional<std::pair<uint16_t, uint16_t>> m_oStdFilter;

        /**
          * \brief extended (29-bit) acceptance filter — id/mask — applied to the open channel
        */
        mutable std::optional<std::pair<uint32_t, uint32_t>> m_oExtFilter;

        /**
          * \brief functions associated to the plugin commands
        */
        #define SLCAN_PLUGIN_CMD_RECORD(a, ...)  bool m_SLCAN_##a ( const std::string& args, std::stop_token st ) const;
        SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  SLCAN_PLUGIN_CMD_RECORD
};

#endif /* SLCAN_PLUGIN_HPP */
