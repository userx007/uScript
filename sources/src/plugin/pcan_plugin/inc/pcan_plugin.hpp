#ifndef PCAN_PLUGIN_HPP
#define PCAN_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uBoolEvaluator.hpp"
#include "uLogger.hpp"

#include "uPcan.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "ITransportProtocol.hpp"
#include "TpFactory.hpp"
#include "TpConfig.hpp"

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
  * only the CONFIG key "i=" changes meaning (PCAN channel handle instead of
  * SocketCAN interface name or serial device path).
  *
  * Key differences vs KVCAN / SLCAN:
  *   - "i=" accepts a PCAN channel handle in decimal or 0x-hex format
  *     (e.g. "0x51" = PCAN_USBBUS1, "81" = same value in decimal).
  *   - "b=" sets the CAN bitrate in bps (e.g. "500000" for 500 kbps).
  *   - "e=" forces 29-bit extended frame format (0 = auto, 1 = force EFF).
  *   - "f=" enables CAN FD mode (0 = classic CAN, 1 = CAN FD).
  *   - FILTER accepts the same comma-separated "<id>:<mask>" syntax as
  *     KVCAN.FILTER, but — unlike KVCAN's arbitrary-length kernel filter
  *     list — only the FIRST entry is actually enforced: the driver
  *     (PCAN::frameMatchesFilter()) tracks a single active RX filter id,
  *     checked in software against every frame PCAN_Read() already
  *     dequeued (there is no PCAN-Basic hardware acceptance filter
  *     involved). Additional entries are accepted/stored but have no
  *     effect; see m_ParseFilters()'s doc comment.
  *
  * TX/RX id defaults and per-call overrides (see also uPcan.cpp):
  *   As with KVCAN, setCanTxId() (the CONFIG command's "x=" key) replaces
  *   the whole filter list with one entry matching the new TX id, so the
  *   default RX id always mirrors the default TX id unless overridden by
  *   an explicit FILTER command. Because PCAN::resolveTxId()/resolveRxId()
  *   recompute fresh from xtra_params on every tout_write()/tout_read()
  *   call — nothing is ever persisted mid-call — a CMD/SCRIPT's "~ id"
  *   xtra_params override is inherently transient and race-free: unlike
  *   KVCAN's SocketCAN kernel filter or SLCAN's adapter-side filter, there
  *   is no hardware/kernel gate that could silently drop a frame before a
  *   filter change takes effect, so no KVCAN-style "arm before write" or
  *   SLCAN-style "channel must be closed" workaround is needed here.
  *
  * Extra command vs UART/I2C/SPI plugins:
  *   FILTER — installs a software acceptance filter applied to every received
  *             frame, without reopening the PCAN channel.
  *
  * Multi-frame transport protocols (t: / CAN_TP_PROTOCOL, y: / CAN_RX_ID):
  *   Payloads longer than one frame already "work" via PCAN's naive
  *   fragmentation loop (see uPcan.hpp) — that stays the default ("none").
  *   Selecting "isotp" or "j1939" switches PCAN::tout_write()/tout_read()
  *   to the real protocol instead (see can_tp/README.md). The dispatch
  *   lives entirely in the driver — this plugin's only job is resolving
  *   t=/y= (CONFIG) and CAN_TP_PROTOCOL/CAN_RX_ID (INI) into
  *   m_eTpProtocol/m_u32CanRxId and pushing them onto the driver in
  *   m_OpenAndConfigure(), same as every other CONFIG-driven setting here.
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
                    , m_bCanRxIdSet(false)
                    , m_u32CanRxId(0U)
                    , m_eTpProtocol(TpProtocol::NONE)
                    , m_sTpConfig()
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
          * \brief set the CAN ID stamped on outgoing frames, and mirror it onto the
          *        default RX acceptance filter.
          *        Accepts decimal or 0x-prefixed hex strings.
          *
          *        The stored value follows the SocketCAN canid_t convention so it stays
          *        identical to the KVCAN and SLCAN plugin's setCanTxId:
          *          - 11-bit standard IDs (<=0x7FF): stored as-is.
          *          - 29-bit extended IDs (>0x7FF) : CAN_EFF_FLAG (0x80000000)
          *            is set automatically if the caller did not set it already,
          *            so both "x=0x18DAF100" and "x=0x98DAF100" select EFF mode.
          *
          *        \note RX/TX default mirroring:
          *        Every time the TX id is (re)configured — whether from the CONFIG
          *        command's "x:" key or from the CAN_TX_ID ini entry — m_vFilters is
          *        replaced with a single entry matching this same id, mirroring the
          *        KVCAN plugin's "replace the whole filter set with one entry"
          *        behaviour. m_OpenAndConfigure() forwards that entry's id to the
          *        driver's single RX filter slot (see PCAN::setDefaultRxFilterId())
          *        the next time a channel is opened by CMD/SCRIPT.
          *
          *        \note Unlike KVCAN's arbitrary-length kernel filter list, PCAN-Basic
          *        (as wired up here) only ever acts on ONE active RX filter id at a
          *        time — see m_OpenAndConfigure(), which forwards only the first
          *        m_vFilters entry to the driver. A CONFIG's "x=" therefore always
          *        replaces the whole list with that one entry (matching KVCAN's
          *        behaviour exactly), but an explicit FILTER command with several
          *        "id:mask" entries will still only have its first entry actually
          *        enforced — see FILTER's own doc comment for details. A per-call
          *        xtra_params id override in tout_read()/tout_write() replaces the
          *        active filter for that one call only and is never persisted.
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

            // Mirror the same id onto the default RX filter — full exact-match
            // mask, flag bit folded into both id and mask (mirrors m_ParseFilters'
            // own EFF/SFF convention) so PCAN::frameMatchesFilter() normalises it
            // the same way regardless of where the value originated.
            const uint32_t u32Mask = (u32Id & CAN_EFF_FLAG) ? (CAN_EFF_FLAG | CAN_EFF_MASK)
                                                             : CAN_SFF_MASK;
            m_vFilters.clear();
            m_vFilters.emplace_back(u32Id, u32Mask);

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
          *        If this is never called, PCAN::resolveTpRxId() mirrors
          *        the default TX id, preserving today's single-id default.
          *
          *        \note Distinct from FILTER / m_vFilters: those still
          *        govern the legacy naive-fragmentation read path (software
          *        accept-all-or-one-id filter). This id is only consulted
          *        by PCAN::tout_write()/tout_read() once t=tp_protocol
          *        selects ISO-TP or J1939.
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
          *        PCAN::tout_write()/tout_read() use for payloads that
          *        don't fit in a single CAN/CAN-FD frame.
          *
          *        Accepted values (case-insensitive): "none" (default —
          *        keeps today's naive fragmentation, see uPcan.hpp's class
          *        comment), "isotp" (ISO 15765-2), "j1939" (SAE J1939-21
          *        BAM/RTS-CTS).
          *
          *        Payloads that already fit in one frame are sent/received
          *        as exactly one physical frame regardless of this setting.
        */
        bool setCanTpProtocol (const std::string& strProtocol) const
        {
            TpProtocol eProto = TpProtocol::NONE;
            if (false == tp_protocol_from_string(strProtocol, eProto)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("PCAN |");
                          LOG_STRING("Unknown CAN transport protocol:"); LOG_STRING(strProtocol.c_str()));
                return false;
            }
            m_eTpProtocol = eProto;
            return true;
        }

        // ---- TpConfig tuning parameters -----------------------------------------
        // Same field set / semantics as the KVCAN plugin (see its kvcan_plugin.hpp
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
          *        CAN_EFF_FLAG auto-correction mirrors the KVCAN plugin's m_ParseFilters:
          *        an id > 0x7FF without CAN_EFF_FLAG set triggers a warning and the flag
          *        is added automatically.
          *
          *        \note Only the FIRST parsed entry is actually enforced: the underlying
          *        PCAN driver (see PCAN::frameMatchesFilter()) only tracks a single active
          *        RX filter id, forwarded from m_vFilters.front() by m_OpenAndConfigure().
          *        Additional comma-separated entries are accepted and stored here (e.g. so
          *        a FILTER command roundtrips through getParams()/setParams() unchanged),
          *        but are NOT currently matched against incoming frames — unlike KVCAN,
          *        which supports an arbitrary-length kernel filter list. Multi-id filtering
          *        would require extending PCAN::tout_read()/resolveRxId() to accept more
          *        than one id; flagging this here rather than silently relying on entries
          *        that have no effect.
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
          * \brief true once setCanRxId() has been called explicitly; until
          *        then PCAN::resolveTpRxId() mirrors m_u32CanTxId.
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
