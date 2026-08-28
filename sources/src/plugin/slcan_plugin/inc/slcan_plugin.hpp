#ifndef SLCAN_PLUGIN_HPP
#define SLCAN_PLUGIN_HPP

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

#include "uSlcan.hpp"
#include "slcan_frame_driver.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

// Generic, driver-independent multi-frame transport library — see
// can_tp/README.md. SLCANFrameDriver already does the actual dispatching
// (tout_write()/tout_read() are the only entry points CommScriptCommandInterpreter
// calls); the plugin only needs to resolve CONFIG/INI settings and push them
// onto the driver in m_OpenAndConfigure(), same as bitrate/mode/filters today.
#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <memory>
#include <optional>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define SLCAN_PLUGIN_VERSION    "1.0.0.0"
#define SLCAN_PLUGIN_NAME       "SLCAN"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// SLCAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef SLCAN_GET_BLOCKING
#define SLCAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
SLCAN_PLUGIN_CMD_RECORD( INFO               ) \
SLCAN_PLUGIN_CMD_RECORD( CONFIG             ) \
SLCAN_PLUGIN_CMD_RECORD( FILTER             ) \
SLCAN_PLUGIN_CMD_RECORD( CMD                ) \
SLCAN_PLUGIN_CMD_RECORD( SCRIPT             ) \
SLCAN_PLUGIN_CMD_RECORD( CYCLIC             ) \

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

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
  *
  * TX/RX id defaults and per-call overrides (see also SLCANFrameDriver):
  *   As with KVCAN, setCanTxId() (the CONFIG command's "x=" key) sets both
  *   the default TX id and a matching default RX filter, and a CMD/SCRIPT's
  *   "~ id" xtra_params suffix overrides that id for a single call. Unlike
  *   KVCAN, though, the RX-side override cannot be enforced by transiently
  *   reprogramming a hardware filter — the adapter's f/F commands require the
  *   channel to be closed — so SLCANFrameDriver::tout_read() matches the
  *   requested id in software instead, after the adapter's own (fixed for
  *   the channel's lifetime) filter has already let the frame through. An
  *   override id outside that filter's acceptance will simply time out;
  *   widen FILTER (or leave it unset) if a script needs several different ids.
  *
  * Multi-frame transport protocols (t: / CAN_TP_PROTOCOL, v: / CAN_RX_ID):
  *   Payloads longer than one frame can be segmented via a selectable
  *   transport protocol (see can_tp/TpCommon.hpp): "none" (default, today's
  *   behaviour — longer payloads are rejected), "isotp" (ISO 15765-2), or
  *   "j1939" (SAE J1939-21). The dispatching itself lives in
  *   SLCANFrameDriver::tout_write()/tout_read() — the only two methods
  *   CommScriptCommandInterpreter ever calls — so this plugin's only job is
  *   resolving t=/v= (CONFIG) and CAN_TP_PROTOCOL/CAN_RX_ID (INI) into
  *   m_eTpProtocol/m_u32CanRxId and pushing them onto the driver in
  *   m_OpenAndConfigure(), same as every other CONFIG-driven setting here.
*/
class SLCANPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        SLCANPlugin() : m_strVersion(SLCAN_PLUGIN_VERSION)
                    , m_strInstanceName("SLCAN")
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_bCyclicCached(true)
                    , m_u32UartBaud(115200U)
                    , m_eBitrate(CanBitrate::BR_125K)
                    , m_eFdDataRate(CanFdDataRate::FD_2M)
                    , m_eMode(CanMode::Normal)
                    , m_eAutoRetx(CanAutoRetx::Disabled)
                    , m_bFdBrs(true)
                    , m_u32CanTxId(0U)
                    , m_bCanRxIdSet(false)
                    , m_u32CanRxId(0U)
                    , m_eTpProtocol(TpProtocol::NONE)
                    , m_sTpConfig()
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32ReadBufferSize(8U)
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
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitly after loading the plugin
        */
        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = true;
            return m_bIsInitialized;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitly before closing/freeing the shared library
        */
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
        }

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
          * \brief set the CAN ID stamped on outgoing frames, and mirror it onto the
          *        matching default RX acceptance filter slot.
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention so it stays
          *        identical to the KVCAN plugin's setCanTxId:
          *          - 11-bit standard IDs (<=0x7FF): stored as-is.
          *          - 29-bit extended IDs (>0x7FF) : CAN_EFF_FLAG (0x80000000)
          *            is set automatically if the caller did not set it already,
          *            so both "x=0x18DAF100" and "x=0x98DAF100" select EFF mode.
          *
          *        \note RX/TX default mirroring:
          *        Every time the TX id is (re)configured — whether from the CONFIG
          *        command's "x:" key or from the CAN_TX_ID ini entry — the filter
          *        slot matching this id's frame type (std or ext) is replaced with
          *        an exact-match filter, and the OTHER slot is cleared (reset to
          *        "accept all" for that frame type), mirroring the KVCAN plugin's
          *        "replace the whole filter set with one entry" behaviour.
          *
          *        \note Unlike KVCAN's socket filters — which can be changed at any
          *        time on an already-open socket — the adapter's f/F filter commands
          *        can only be sent while the CAN channel is closed (see uSlcan.cpp).
          *        So this pair (m_u32CanTxId / m_oStdFilter or m_oExtFilter) is only
          *        pushed to the adapter the next time a channel is opened by
          *        m_OpenAndConfigure() (i.e. the next CMD/SCRIPT) — there is no
          *        equivalent of KVCAN's "apply to the already-open socket" here.
          *        A per-call xtra_params id override in tout_read()/tout_write()
          *        (see SLCANFrameDriver) can only succeed for an id that is already
          *        covered by whichever filter is active for that channel; issue an
          *        explicit FILTER command (or leave filters unset for accept-all)
          *        beforehand if a script needs to react to ids other than the
          *        current default.
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

            // Mirror the same id onto the matching default RX filter slot; the
            // adapter's f/F commands carry no flag bits of their own, so the
            // slot itself (std vs ext) is what encodes the frame type.
            if (u32Id & CAN_EFF_FLAG) {
                m_oExtFilter = std::make_pair(static_cast<uint32_t>(u32Id & CAN_EFF_MASK),
                                               static_cast<uint32_t>(CAN_EFF_MASK));
                m_oStdFilter.reset();
            } else {
                m_oStdFilter = std::make_pair(static_cast<uint16_t>(u32Id & CAN_SFF_MASK),
                                               static_cast<uint16_t>(CAN_SFF_MASK));
                m_oExtFilter.reset();
            }

            return true;
        }

        /**
          * \brief set the CAN id this plugin expects incoming (response)
          *        frames to arrive on, when it differs from the TX id.
          *
          *        Only meaningful once a transport protocol other than
          *        TpProtocol::NONE is selected (see setCanTpProtocol()):
          *        segmented protocols need to distinguish the id we
          *        transmit request frames on (m_u32CanTxId) from the id
          *        the peer's response/handshake frames arrive on
          *        (m_u32CanRxId) — e.g. UDS request 0x7E0 / response 0x7E8.
          *
          *        If this is never called, the driver mirrors m_u32CanTxId
          *        (see SLCANFrameDriver::resolveRxId()), preserving today's
          *        single-id behaviour.
          *
          *        \note Same hardware caveat as setCanTxId(): whichever id
          *        ends up in effect still needs to be covered by the
          *        adapter's active std/ext filter (FILTER command) or the
          *        underlying frames never reach tout_read() at all.
          *
          *        Same EFF-flag auto-detection / clamping rules as setCanTxId().
        */
        bool setCanRxId (const std::string& strRxId) const
        {
            static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
            static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
            static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

            uint32_t u32Id = 0U;
            if (false == numeric::str2uint32(strRxId, u32Id)) {
                return false;
            }

            if (!(u32Id & CAN_EFF_FLAG) && ((u32Id & CAN_EFF_MASK) > CAN_SFF_MASK)) {
                u32Id |= CAN_EFF_FLAG;
            }

            if (u32Id & CAN_EFF_FLAG) {
                u32Id &= (CAN_EFF_FLAG | CAN_EFF_MASK);
            } else {
                u32Id &= CAN_SFF_MASK;
            }

            m_u32CanRxId  = u32Id;
            m_bCanRxIdSet = true;

            return true;
        }

        /**
          * \brief select the multi-frame transport protocol
          *        SLCANFrameDriver's tout_write()/tout_read() use for
          *        payloads that don't fit in a single CAN/CAN-FD frame.
          *
          *        Accepted values (case-insensitive): "none" (default,
          *        current single-frame-only behaviour), "isotp" (ISO
          *        15765-2), "j1939" (SAE J1939-21 BAM/RTS-CTS).
          *
          *        Payloads that already fit in one frame are sent/received
          *        as exactly one physical frame regardless of this setting
          *        — it only changes what happens for payloads that would
          *        otherwise be rejected as too long (> 64 bytes today).
        */
        bool setCanTpProtocol (const std::string& strProtocol) const
        {
            TpProtocol eProto = TpProtocol::NONE;
            if (false == tp_protocol_from_string(strProtocol, eProto)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("SLCAN |");
                          LOG_STRING("Unknown CAN transport protocol:"); LOG_STRING(strProtocol.c_str()));
                return false;
            }
            m_eTpProtocol = eProto;
            return true;
        }

        // ---- TpConfig tuning parameters -----------------------------------------
        // Same field set / semantics as the KVCAN/PCAN plugins (see kvcan_plugin.hpp
        // for the full per-field doc); tunes whichever protocol setCanTpProtocol()
        // selected above. A field a given protocol doesn't use is ignored by it.

        bool setTpBlockSize (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.blockSize); }

        bool setTpStMin (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.stMin); }

        bool setTpPadFrames (const std::string& strVal) const
        { BoolExprEvaluator sEvaluator; return sEvaluator.evaluate(strVal, m_sTpConfig.padFrames); }

        bool setTpPaddingByte (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.paddingByte); }

        bool setTpTimeoutNBs (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutNBs_ms); }

        bool setTpTimeoutNCr (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutNCr_ms); }

        bool setTpMaxMessageLen (const std::string& strVal) const
        { return numeric::str2sizet(strVal, m_sTpConfig.maxMessageLen); }

        bool setJ1939UseBam (const std::string& strVal) const
        { BoolExprEvaluator sEvaluator; return sEvaluator.evaluate(strVal, m_sTpConfig.j1939UseBam); }

        bool setJ1939MaxPackets (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.j1939MaxPackets); }

        bool setTpTimeoutT1 (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutT1_ms); }

        bool setTpTimeoutT2 (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutT2_ms); }

        bool setTpTimeoutT3 (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutT3_ms); }

        bool setTpTimeoutTh (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutTh_ms); }

        bool setJ1939MaxMessageLen (const std::string& strVal) const
        { return numeric::str2sizet(strVal, m_sTpConfig.j1939MaxMessageLen); }

        bool setCanOpenIndex (const std::string& strVal) const
        { return numeric::str2uint16(strVal, m_sTpConfig.canOpenIndex); }

        bool setCanOpenSubIndex (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.canOpenSubIndex); }

        bool setCanOpenUseBlock (const std::string& strVal) const
        { BoolExprEvaluator sEvaluator; return sEvaluator.evaluate(strVal, m_sTpConfig.canOpenUseBlock); }

        bool setCanOpenBlockSize (const std::string& strVal) const
        { return numeric::str2uint8(strVal, m_sTpConfig.canOpenBlockSize); }

        bool setTpTimeoutSdo (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutSdo_ms); }

        bool setCanOpenMaxMessageLen (const std::string& strVal) const
        { return numeric::str2sizet(strVal, m_sTpConfig.canOpenMaxMessageLen); }

        bool setTpTimeoutFpInterFrame (const std::string& strVal) const
        { return numeric::str2uint32(strVal, m_sTpConfig.timeoutFpInterFrame_ms); }

        bool setFpMaxMessageLen (const std::string& strVal) const
        { return numeric::str2sizet(strVal, m_sTpConfig.fastPacketMaxMessageLen); }

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
            m_u32ReadBufferSize = u32Size;
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
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "SLCAN" or "SLCAN:1" -- see PluginDataSet::strInstanceName).
          *        Falls back to plain "SLCAN" when unset.
        */
        std::string m_strInstanceName;

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
          * \brief true once setCanRxId() has been called explicitly; until
          *        then the driver mirrors m_u32CanTxId (see setCanRxId()).
        */
        mutable bool m_bCanRxIdSet;

        /**
          * \brief CAN id expected for incoming response/handshake frames
          *        when a segmented transport protocol is active.
        */
        mutable uint32_t m_u32CanRxId;

        /**
          * \brief selected multi-frame transport protocol (see setCanTpProtocol()).
        */
        mutable TpProtocol m_eTpProtocol;

        /**
          * \brief tuning parameters (block size, STmin, timeouts, ...) for
          *        whichever transport protocol m_eTpProtocol selects.
        */
        mutable TpConfig m_sTpConfig;

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
        mutable uint32_t m_u32ReadBufferSize;

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
