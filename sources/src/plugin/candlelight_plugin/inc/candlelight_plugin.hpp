#ifndef CANDLELIGHT_PLUGIN_HPP
#define CANDLELIGHT_PLUGIN_HPP

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

#include "uCandlelight.hpp"
#include "candlelight_frame_driver.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

// Generic, driver-independent multi-frame transport library — see
// can_tp/README.md. CandlelightFrameDriver already does the actual
// dispatching (tout_write()/tout_read() are the only entry points
// CommScriptCommandInterpreter calls); the plugin only needs to resolve
// CONFIG/INI settings and push them onto the driver in m_OpenAndConfigure(),
// same as bitrate/mode/filters today.
#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <memory>
#include <optional>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define CANDLELIGHT_PLUGIN_VERSION    "1.0.0.0"
#define CANDLELIGHT_PLUGIN_NAME       "CANDLELIGHT |"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// CANDLELIGHT_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef CANDLELIGHT_GET_BLOCKING
#define CANDLELIGHT_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define CANDLELIGHT_PLUGIN_COMMANDS_CONFIG_TABLE    \
CANDLELIGHT_PLUGIN_CMD_RECORD( INFO               ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CONFIG             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( FILTER             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CMD                ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( SCRIPT             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CYCLIC             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief Candlelight plugin class definition.
  *
  * Wraps the Candlelight (gs_usb) driver — a native-USB CAN/CAN-FD adapter
  * protocol (control transfers for setup, bulk transfers for frames — see
  * uCandlelight.hpp for the full protocol reference and its relationship to
  * the CANable/candleLight-fw ecosystem, including Elmue's CANable 2.5
  * firmware this plugin was written against) — and exposes it through the
  * standard PluginInterface dispatch model. Same overall command surface
  * (INFO/CONFIG/FILTER/CMD/SCRIPT/CYCLIC) as SLCAN/UCAN, but the CONFIG
  * keys and FILTER semantics differ in ways that follow directly from the
  * underlying protocol being fundamentally different:
  *
  *   - **No serial device** — there is no UART underneath at all. CONFIG's
  *     device-selection keys are vid=/pid=/idx= (USB vendor id, product id,
  *     and which Nth matching device to open), not a device path + baud rate.
  *
  *   - **Bit timing is registers, not a preset table** — SLCAN/UCAN each
  *     offer a small enumerated list of standard bit rates (S0-SD / 0-13).
  *     gs_usb instead exposes the adapter's raw clock rate and bit-timing
  *     register limits (queried once via BT_CONST — see
  *     Candlelight::bt_const()) and expects the *host* to compute
  *     prop_seg/phase_seg1/phase_seg2/sjw/brp for whatever bit rate it
  *     wants. CONFIG's b=/sp= keys (bit rate + sample point) drive
  *     Candlelight::set_bitrate()'s built-in calculator; the raw ps=/p1=/
  *     p2=/sw=/bp= keys (and dps=/dp1=/dp2=/dsw=/dbp= for the CAN-FD data
  *     phase) bypass that calculator entirely for callers who already know
  *     the exact register values they want (e.g. matching another tool's
  *     output bit-for-bit).
  *
  *   - **No on-device acceptance filtering at all** — SLCAN has one std +
  *     one ext hardware filter slot; UCAN mirrors that. gs_usb has no
  *     filtering USB request whatsoever (see uCandlelight.hpp's "No
  *     on-device acceptance filtering") — every frame the bus carries
  *     reaches the host, always. FILTER here therefore configures a
  *     **software** filter list (see m_ParseFilters(), CandlelightFrameDriver
  *     ::set_filters()) applied after each frame is received, and — unlike
  *     SLCAN/UCAN's single-slot-per-kind limitation — accepts an arbitrary
  *     number of (id, mask) entries, mirroring SocketCAN's CAN_RAW_FILTER
  *     list semantics exactly (just evaluated in software instead of by the
  *     kernel/hardware).
  *
  *   - **Mode is a flag bitmask, not two separate enums** — SLCAN/UCAN
  *     split "silent vs normal" (mode) and "auto-retransmit" into two
  *     separate settings. gs_usb combines every bus-mode option (listen-
  *     only, loopback, triple-sample, one-shot, CAN-FD, pad-to-max-packet,
  *     bus-error reporting) into one GS_CAN_MODE_* bitmask sent together
  *     with "open the channel" (see Candlelight::open_channel()). CONFIG's
  *     m= key takes that bitmask directly.
  *
  * Because there is no on-device filter to reprogram, unlike UCAN/SLCAN
  * there is also no "channel must be closed to change this" restriction on
  * FILTER — but bit timing and mode still can only be set before
  * open_channel(), so CONFIG still needs to be issued before, not during, a
  * CMD/SCRIPT/CYCLIC call that changes them, same rationale as SLCAN/UCAN.
  *
  * TX/RX id defaults and per-call overrides (see also CandlelightFrameDriver):
  *   As with KVCAN/UCAN, setCanTxId() (the CONFIG command's "x=" key) sets
  *   both the default TX id and a matching default RX filter entry, and a
  *   CMD/SCRIPT's "~ id" xtra_params suffix overrides that id for a single
  *   call. Because filtering is done entirely in software here (see above),
  *   an override id NOT already covered by FILTER still works for TX (there
  *   is no hardware restriction on which id can be transmitted), and for RX
  *   it is checked directly against the frame — no dependency on whatever
  *   FILTER happens to be configured, unlike UCAN.
  *
  * Multi-frame transport protocols (t: / CAN_TP_PROTOCOL, v: / CAN_RX_ID):
  *   Payloads longer than one frame can be segmented via a selectable
  *   transport protocol (see can_tp/TpCommon.hpp): "none" (default, today's
  *   behaviour — longer payloads are rejected), "isotp" (ISO 15765-2), or
  *   "j1939" (SAE J1939-21). The dispatching itself lives in
  *   CandlelightFrameDriver::tout_write()/tout_read() — the only two methods
  *   CommScriptCommandInterpreter ever calls — so this plugin's only job is
  *   resolving t=/v= (CONFIG) and CAN_TP_PROTOCOL/CAN_RX_ID (INI) into
  *   m_eTpProtocol/m_u32CanRxId and pushing them onto the driver in
  *   m_OpenAndConfigure(), same as every other CONFIG-driven setting here.
*/
class CandlelightPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        CandlelightPlugin() : m_strVersion(CANDLELIGHT_PLUGIN_VERSION)
                    , m_strInstanceName("CANDLELIGHT")
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_u16UsbVid(0x1209U)   // candleLight-fw / CANable Candlelight default — see uCandlelight.hpp
                    , m_u16UsbPid(0x2323U)
                    , m_u32UsbDeviceIndex(0U)
                    , m_u32Bitrate(500000U)
                    , m_dSamplePoint(0.875)
                    , m_u32FdBitrate(2000000U)
                    , m_dFdSamplePoint(0.75)
                    , m_bFdBrs(true)
                    , m_u32ModeFlags(GS_CAN_MODE_NORMAL)
                    , m_bRawTimingSet(false)
                    , m_u32PropSeg(0U), m_u32PhaseSeg1(0U), m_u32PhaseSeg2(0U), m_u32Sjw(0U), m_u32Brp(0U)
                    , m_bFdRawTimingSet(false)
                    , m_u32FdPropSeg(0U), m_u32FdPhaseSeg1(0U), m_u32FdPhaseSeg2(0U), m_u32FdSjw(0U), m_u32FdBrp(0U)
                    , m_u32CanTxId(0U)
                    , m_bCanRxIdSet(false)
                    , m_u32CanRxId(0U)
                    , m_eTpProtocol(TpProtocol::NONE)
                    , m_sTpConfig()
                    , m_u32ReadTimeout(1000U)
                    , m_u32WriteTimeout(1000U)
                    , m_u32ReadBufferSize(8U)
        {
            #define CANDLELIGHT_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<CandlelightPlugin>{&CandlelightPlugin::m_CANDLELIGHT_##a, CANDLELIGHT_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            CANDLELIGHT_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  CANDLELIGHT_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~CandlelightPlugin() = default;

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

            if (true == generic_setparams<CandlelightPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<CandlelightPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<CandlelightPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<CandlelightPlugin> *getMap(void) const
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
          * \brief set the USB vendor id of the adapter to open (hex or decimal)
          * \note Defaults to 0x1209 (candleLight-fw / CANable Candlelight — see uCandlelight.hpp)
        */
        bool setUsbVid (const std::string& strVid) const
        {
            uint32_t u32Val = 0U;
            if (false == numeric::str2uint32(strVid, u32Val) || u32Val > 0xFFFFU) {
                return false;
            }
            m_u16UsbVid = static_cast<uint16_t>(u32Val);
            return true;
        }

        /**
          * \brief set the USB product id of the adapter to open (hex or decimal)
          * \note Defaults to 0x2323 (candleLight-fw / CANable Candlelight — see uCandlelight.hpp)
        */
        bool setUsbPid (const std::string& strPid) const
        {
            uint32_t u32Val = 0U;
            if (false == numeric::str2uint32(strPid, u32Val) || u32Val > 0xFFFFU) {
                return false;
            }
            m_u16UsbPid = static_cast<uint16_t>(u32Val);
            return true;
        }

        /**
          * \brief select which Nth device matching vid/pid to open, if more
          *        than one compatible adapter is plugged in (0 = first)
        */
        bool setUsbDeviceIndex (const std::string& strIdx) const
        {
            return numeric::str2uint32(strIdx, m_u32UsbDeviceIndex);
        }

        /**
          * \brief set the target nominal CAN bit rate in bit/s, e.g. 500000
          * \note Combined with setCanSamplePoint() to derive
          *       prop_seg/phase_seg1/phase_seg2/sjw/brp via
          *       Candlelight::set_bitrate()'s calculator. Overridden entirely
          *       if a raw ps=/p1=/p2=/sw=/bp= override is also given — see
          *       this class's doc comment.
        */
        bool setCanBitrate (const std::string& strBitrate) const
        {
            return numeric::str2uint32(strBitrate, m_u32Bitrate);
        }

        /**
          * \brief set the target nominal sample point, 0.0-1.0 (default 0.875 = 87.5%)
        */
        bool setCanSamplePoint (const std::string& strSamplePoint) const
        {
            try {
                m_dSamplePoint = std::stod(strSamplePoint);
            } catch (...) {
                return false;
            }
            return (m_dSamplePoint > 0.0) && (m_dSamplePoint < 1.0);
        }

        /**
          * \brief set the target CAN-FD data-phase bit rate in bit/s, e.g. 2000000
          * \note Only meaningful for frames sent with the CAN-FD BRS flag (see setCanFdBrs).
        */
        bool setCanFdBitrate (const std::string& strFdBitrate) const
        {
            return numeric::str2uint32(strFdBitrate, m_u32FdBitrate);
        }

        /**
          * \brief set the target CAN-FD data-phase sample point, 0.0-1.0 (default 0.75 = 75%)
        */
        bool setCanFdSamplePoint (const std::string& strSamplePoint) const
        {
            try {
                m_dFdSamplePoint = std::stod(strSamplePoint);
            } catch (...) {
                return false;
            }
            return (m_dFdSamplePoint > 0.0) && (m_dFdSamplePoint < 1.0);
        }

        /**
          * \brief enable/disable the Bit Rate Switch flag on outgoing CAN-FD frames
          * \note Ignored for classic CAN frames (payload <= 8 bytes); 0 = off, 1 = on (default)
        */
        bool setCanFdBrs (const std::string& strBrs) const
        {
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strBrs, u32Val)) || (u32Val > 1U)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("CANDLELIGHT |");
                          LOG_STRING("FdBrs must be 0 (off) or 1 (on):"); LOG_UINT32(u32Val));
                return false;
            }
            m_bFdBrs = (1U == u32Val);
            return true;
        }

        /**
          * \brief set the GS_CAN_MODE_* bus-mode bitmask applied when the channel is
          *        next opened (see uCandlelight.hpp's GsCanFeature enum for the bit values:
          *        listen-only=1, loopback=2, triple-sample=4, one-shot=8, FD=256,
          *        pad-to-max=128, berr-reporting=4096). Accepts decimal or 0x-prefixed hex.
        */
        bool setCanModeFlags (const std::string& strMode) const
        {
            return numeric::str2uint32(strMode, m_u32ModeFlags);
        }

        // ---- Raw bit-timing override (power users) ------------------------------
        // Bypasses setCanBitrate()/setCanSamplePoint()'s calculator entirely once
        // ALL FIVE of these have been set — see m_OpenAndConfigure().

        bool setCanPropSeg (const std::string& strVal) const
        { m_bRawTimingSet = true; return numeric::str2uint32(strVal, m_u32PropSeg); }

        bool setCanPhaseSeg1 (const std::string& strVal) const
        { m_bRawTimingSet = true; return numeric::str2uint32(strVal, m_u32PhaseSeg1); }

        bool setCanPhaseSeg2 (const std::string& strVal) const
        { m_bRawTimingSet = true; return numeric::str2uint32(strVal, m_u32PhaseSeg2); }

        bool setCanSjw (const std::string& strVal) const
        { m_bRawTimingSet = true; return numeric::str2uint32(strVal, m_u32Sjw); }

        bool setCanBrp (const std::string& strVal) const
        { m_bRawTimingSet = true; return numeric::str2uint32(strVal, m_u32Brp); }

        bool setCanFdPropSeg (const std::string& strVal) const
        { m_bFdRawTimingSet = true; return numeric::str2uint32(strVal, m_u32FdPropSeg); }

        bool setCanFdPhaseSeg1 (const std::string& strVal) const
        { m_bFdRawTimingSet = true; return numeric::str2uint32(strVal, m_u32FdPhaseSeg1); }

        bool setCanFdPhaseSeg2 (const std::string& strVal) const
        { m_bFdRawTimingSet = true; return numeric::str2uint32(strVal, m_u32FdPhaseSeg2); }

        bool setCanFdSjw (const std::string& strVal) const
        { m_bFdRawTimingSet = true; return numeric::str2uint32(strVal, m_u32FdSjw); }

        bool setCanFdBrp (const std::string& strVal) const
        { m_bFdRawTimingSet = true; return numeric::str2uint32(strVal, m_u32FdBrp); }

        /**
          * \brief set the CAN ID stamped on outgoing frames, and mirror it onto the
          *        software filter list as a single exact-match entry (see FILTER for
          *        adding more).
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention, same as
          *        the KVCAN/UCAN plugins:
          *          - 11-bit standard IDs (<=0x7FF): stored as-is.
          *          - 29-bit extended IDs (>0x7FF) : CAN_EFF_FLAG (0x80000000)
          *            is set automatically if the caller did not set it already,
          *            so both "x=0x18DAF100" and "x=0x98DAF100" select EFF mode.
          *
          *        \note Unlike UCAN/SLCAN, this filter mirroring is purely a
          *        convenience default — since filtering is entirely software-side
          *        here (see uCandlelight.hpp's "No on-device acceptance filtering"),
          *        a per-call xtra_params RX id override in
          *        CandlelightFrameDriver::tout_read() always works regardless of
          *        what FILTER/setCanTxId() last configured.
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

            const bool bExt = (u32Id & CAN_EFF_FLAG) != 0U;
            m_vecFilters.clear();
            m_vecFilters.push_back({ u32Id & (bExt ? CAN_EFF_MASK : CAN_SFF_MASK),
                                      bExt ? CAN_EFF_MASK : CAN_SFF_MASK, bExt });

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
          *        (see CandlelightFrameDriver::resolveRxId()), preserving
          *        today's single-id behaviour.
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
          *        CandlelightFrameDriver's tout_write()/tout_read() use for
          *        payloads that don't fit in a single CAN/CAN-FD frame.
          *
          *        Accepted values (case-insensitive): "none" (default,
          *        current single-frame-only behaviour), "isotp" (ISO
          *        15765-2), "j1939" (SAE J1939-21 BAM/RTS-CTS).
        */
        bool setCanTpProtocol (const std::string& strProtocol) const
        {
            TpProtocol eProto = TpProtocol::NONE;
            if (false == tp_protocol_from_string(strProtocol, eProto)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("CANDLELIGHT |");
                          LOG_STRING("Unknown CAN transport protocol:"); LOG_STRING(strProtocol.c_str()));
                return false;
            }
            m_eTpProtocol = eProto;
            return true;
        }

        // ---- TpConfig tuning parameters -----------------------------------------
        // Same field set / semantics as the KVCAN/PCAN/SLCAN/UCAN plugins; tunes
        // whichever protocol setCanTpProtocol() selected above. A field a given
        // protocol doesn't use is ignored by it.

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
          * \brief set Candlelight read timeout
        */
        bool setCanReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set Candlelight write timeout
        */
        bool setCanWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set Candlelight read buffer size
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
                LOG_PRINT(LOG_ERROR, LOG_STRING("CANDLELIGHT |");
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
          * \brief helper: parse a comma-separated filter list string into the
          *        software filter list (see CandlelightFrameDriver::set_filters()).
          *        Each entry has the form "<id>:<mask>" (hex or decimal).
          *        Example: "0x100:0x7FF,0x18DAF100:0x1FFFFFFF"
          *
          *        Unlike SLCAN/UCAN (one hardware standard + one hardware
          *        extended filter slot), Candlelight has no on-device filter
          *        at all (see uCandlelight.hpp's "No on-device acceptance
          *        filtering"), so this accepts an arbitrary number of entries
          *        of either kind — same as SocketCAN's CAN_RAW_FILTER list.
          *        An id > 0x7FF without CAN_EFF_FLAG set triggers a warning
          *        and is treated as extended automatically (mirrors setCanTxId).
        */
        bool m_ParseFilters (const std::string& strFilters) const;

        /**
          * \brief Open the USB device, push bit timing/mode (channel must be closed for
          *        these), install the software filter list, then open the CAN channel.
          *        Returns a ready-to-use CandlelightFrameDriver (compatible with
          *        CommScriptClient and CommScriptCommandInterpreter), or nullptr if any
          *        step failed (already logged).
        */
        std::shared_ptr<CandlelightFrameDriver> m_OpenAndConfigure (void) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<CandlelightPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "CANDLELIGHT" or "CANDLELIGHT:1" -- see PluginDataSet::strInstanceName).
          *        Falls back to plain "CANDLELIGHT" when unset.
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
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief USB vendor id of the adapter to open — see setUsbVid()
        */
        mutable uint16_t m_u16UsbVid;

        /**
          * \brief USB product id of the adapter to open — see setUsbPid()
        */
        mutable uint16_t m_u16UsbPid;

        /**
          * \brief which Nth device matching vid/pid to open — see setUsbDeviceIndex()
        */
        mutable uint32_t m_u32UsbDeviceIndex;

        /**
          * \brief target nominal CAN bit rate in bit/s — see setCanBitrate()
        */
        mutable uint32_t m_u32Bitrate;

        /**
          * \brief target nominal sample point — see setCanSamplePoint()
        */
        mutable double m_dSamplePoint;

        /**
          * \brief target CAN-FD data-phase bit rate in bit/s — see setCanFdBitrate()
        */
        mutable uint32_t m_u32FdBitrate;

        /**
          * \brief target CAN-FD data-phase sample point — see setCanFdSamplePoint()
        */
        mutable double m_dFdSamplePoint;

        /**
          * \brief whether outgoing CAN-FD frames request the Bit Rate Switch
        */
        mutable bool m_bFdBrs;

        /**
          * \brief GS_CAN_MODE_* bus-mode bitmask applied before every channel open
        */
        mutable uint32_t m_u32ModeFlags;

        /**
          * \brief true once any of setCanPropSeg/PhaseSeg1/PhaseSeg2/Sjw/Brp has been
          *        called — see those setters' doc comments and m_OpenAndConfigure().
        */
        mutable bool m_bRawTimingSet;
        mutable uint32_t m_u32PropSeg, m_u32PhaseSeg1, m_u32PhaseSeg2, m_u32Sjw, m_u32Brp;

        /** \brief same as above, for the CAN-FD data phase. */
        mutable bool m_bFdRawTimingSet;
        mutable uint32_t m_u32FdPropSeg, m_u32FdPhaseSeg1, m_u32FdPhaseSeg2, m_u32FdSjw, m_u32FdBrp;

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
          * \brief Candlelight read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief Candlelight write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for Candlelight read operations (max 64 bytes for CAN FD)
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief software acceptance filter list — id/mask/is_extended — applied
          *        to every received frame (see m_ParseFilters(), CandlelightFrameDriver::set_filters())
        */
        mutable std::vector<CandlelightFrameDriver::FilterEntry> m_vecFilters;

        /**
          * \brief functions associated to the plugin commands
        */
        #define CANDLELIGHT_PLUGIN_CMD_RECORD(a, ...)  bool m_CANDLELIGHT_##a ( const std::string& args, std::stop_token st ) const;
        CANDLELIGHT_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  CANDLELIGHT_PLUGIN_CMD_RECORD
};

#endif /* CANDLELIGHT_PLUGIN_HPP */
