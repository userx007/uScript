#ifndef KCAN_PLUGIN_HPP
#define KCAN_PLUGIN_HPP
#include "ICommDriver.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ITransportProtocol.hpp"
#include "PluginExport.hpp"
#include "PluginOperations.hpp"
#include "TpFactory.hpp"
#include "kcan_parse.hpp"
#include "uBoolEvaluator.hpp"
#include "uCommandExec.hpp"
#include "uKCan.hpp"
#include "uKCanLink.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"

#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define KCAN_PLUGIN_VERSION "1.0.0.0"
#define KCAN_PLUGIN_NAME    "KCAN"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// KCAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef KCAN_GET_BLOCKING
#define KCAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define KCAN_PLUGIN_COMMANDS_CONFIG_TABLE \
    KCAN_PLUGIN_CMD_RECORD(INFO)          \
    KCAN_PLUGIN_CMD_RECORD(CONFIG)        \
    KCAN_PLUGIN_CMD_RECORD(FILTER)        \
    KCAN_PLUGIN_CMD_RECORD(CMD)           \
    KCAN_PLUGIN_CMD_RECORD(SCRIPT)        \
    KCAN_PLUGIN_CMD_RECORD(CYCLIC)        \
    KCAN_PLUGIN_CMD_RECORD(UP)            \
    KCAN_PLUGIN_CMD_RECORD(DOWN)          \
    KCAN_PLUGIN_CMD_RECORD(STATUS)        \
    KCAN_PLUGIN_CMD_RECORD(RESTART)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
 * \brief KCAN plugin class definition.
 *
 * Talks to REAL CAN adapters that the Linux kernel exposes as SocketCAN network
 * interfaces (can0, can1 ... - e.g. a PEAK PCAN-USB running the in-kernel
 * peak_usb driver in "SocketCAN mode"), and to virtual ones (vcan0).
 *
 * It is the KVCAN plugin plus the part a real bus needs and a virtual one does not:
 * the BUS SPEED. The interface has to be given a bitrate (and optionally sample
 * point, CAN FD data rate, listen-only / loopback / ... mode) and be brought up
 * before frames flow - the `ip link set can0 type can bitrate 500000` /
 * `ip link set can0 up` step. KCAN does that itself, over rtnetlink:
 *
 *   KCAN.CONFIG i=can0 b=500000 x=0x123      // remember interface + bitrate + TX id
 *   KCAN.CMD > H"AABB" | H"06"               // first use applies the bitrate and brings can0 up
 *
 * or explicitly, with UP/DOWN/STATUS/RESTART. When the interface is already up
 * with the requested settings nothing is touched (so it works unprivileged on an
 * interface that was set up by the system, e.g. systemd-networkd).
 *
 * Changing bitrate / link state needs CAP_NET_ADMIN (root, or
 * `setcap cap_net_admin+ep <binary>`); plain frame I/O does not.
 */
class KCANPlugin : public PluginInterface {
    public:
        /**
         * \brief class constructor
         */
        KCANPlugin()
            : m_strVersion(KCAN_PLUGIN_VERSION)
            , m_strInstanceName(KCAN_PLUGIN_NAME)
            , m_bIsInitialized(false)
            , m_bIsEnabled(false)
            , m_bIsFaultTolerant(false)
            , m_bIsPrivileged(false)
            , m_strResultData()
            , m_bRawResult(false)
            , m_bCyclicCached(true)
            , m_u32CanTxId(0U)
            , m_bCanRxIdSet(false)
            , m_u32CanRxId(0U)
            , m_eTpProtocol(TpProtocol::NONE)
            , m_sTpConfig()
            , m_u32ReadTimeout(1000U)
            , m_u32WriteTimeout(1000U)
            , m_u32ReadBufferSize(8U)
        {
            // clang-format off
#define KCAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert(std::make_pair(#a, \
            PluginCommandEntry<KCANPlugin>{&KCANPlugin::m_KCAN_##a, KCAN_GET_BLOCKING(a, ##__VA_ARGS__, false)}));
            KCAN_PLUGIN_COMMANDS_CONFIG_TABLE
#undef KCAN_PLUGIN_CMD_RECORD
            // clang-format on
        }

        /**
         * \brief class destructor
         */
        ~KCANPlugin() = default;

        /**
         * \brief get the plugin initialization status
         */
        bool isInitialized(void) const
        {
            return m_bIsInitialized;
        }

        /**
         * \brief get enabling status
         */
        bool isEnabled(void) const
        {
            return m_bIsEnabled;
        }

        /**
         * \brief Import external settings into the plugin
         */
        bool setParams(const PluginDataSet *psSetParams)
        {
            bool bRetVal = false;

            if (true == generic_setparams<KCANPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
         * \brief function to retrieve information from plugin
         */
        void getParams(PluginDataGet *psGetParams) const
        {
            generic_getparams<KCANPlugin>(this, psGetParams);
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
         * \brief dispatch commands
         */
        bool doDispatch(const std::string &strCmd, const std::string &strParams, std::stop_token st = {}) const
        {
            return generic_dispatch<KCANPlugin>(this, strCmd, strParams, st);
        }

        /**
         * \brief get a pointer to the plugin map
         */
        const PluginCommandsMap<KCANPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
         * \brief get the plugin version
         */
        const std::string &getVersion(void) const
        {
            return m_strVersion;
        }

        /**
         * \brief get the result data
         */
        const std::string &getData(void) const
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
        bool setRawResult(const std::string &strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /**
         * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
         */
        bool setCyclicCached(const std::string &strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
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
         * \brief get fault tolerant flag status
         */
        bool isFaultTolerant(void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
         * \brief get the privileged status
         */
        bool isPrivileged(void) const
        {
            return m_bIsPrivileged;
        }

        /**
         * \brief get SocketCAN interface name
         */
        const char *getCanIface(void) const
        {
            return m_strCanIface.c_str();
        }

        /**
         * \brief set SocketCAN interface name (e.g. "vcan0", "can1")
         */
        void setCanIface(const std::string &strCanIface) const
        {
            m_strCanIface.assign(strCanIface);
            m_bLinkDirty = true; // a different interface has to be (re)configured
        }

        // ---- link (bus speed) configuration --------------------------------------
        //
        // These only store the wanted state in m_sLinkCfg and flag it dirty; the
        // interface itself is touched when it is next used (KCAN_AUTO_LINK, default on)
        // or when UP is issued explicitly. See m_ApplyLink().

        /** \brief nominal bitrate in bit/s: "500000", "500k", "1M" ... ("" or "0" = leave interface as is) */
        bool setCanBitrate(const std::string &strVal) const
        {
            uint32_t u32Bitrate = 0U;
            if (strVal.empty() || strVal == "0") {
                m_sLinkCfg.bitrate = 0U;
            } else if (kcan_parse::bitrate(strVal, u32Bitrate)) {
                m_sLinkCfg.bitrate = u32Bitrate;
            } else {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |"); LOG_STRING("Invalid bitrate (expected e.g. 500000, 500k, 1M):"); LOG_STRING(strVal));
                return false;
            }
            m_bLinkDirty = true;
            return true;
        }

        /** \brief nominal sample point: 0.875 | 87.5 | 875 (== 87.5 %), "0" = kernel default */
        bool setCanSamplePoint(const std::string &strVal) const
        {
            uint32_t u32Permille = 0U;
            if (strVal.empty() || false == kcan_parse::samplePoint(strVal, u32Permille)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |"); LOG_STRING("Invalid sample point (expected 0.875, 87.5 or 875; range 50-99.9 %):"); LOG_STRING(strVal));
                return false;
            }
            m_sLinkCfg.samplePoint = u32Permille;
            m_bLinkDirty           = true;
            return true;
        }

        /** \brief CAN FD data-phase bitrate in bit/s (same notation as the nominal bitrate) */
        bool setCanDataBitrate(const std::string &strVal) const
        {
            uint32_t u32Bitrate = 0U;
            if (strVal.empty() || strVal == "0") {
                m_sLinkCfg.dataBitrate = 0U;
            } else if (kcan_parse::bitrate(strVal, u32Bitrate)) {
                m_sLinkCfg.dataBitrate = u32Bitrate;
            } else {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |"); LOG_STRING("Invalid data bitrate (expected e.g. 2000000, 2M):"); LOG_STRING(strVal));
                return false;
            }
            m_bLinkDirty = true;
            return true;
        }

        /** \brief CAN FD data-phase sample point (same notation as the nominal one) */
        bool setCanDataSamplePoint(const std::string &strVal) const
        {
            uint32_t u32Permille = 0U;
            if (strVal.empty() || false == kcan_parse::samplePoint(strVal, u32Permille)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |"); LOG_STRING("Invalid data sample point:"); LOG_STRING(strVal));
                return false;
            }
            m_sLinkCfg.dataSamplePoint = u32Permille;
            m_bLinkDirty               = true;
            return true;
        }

        /** \brief bus-off auto recovery delay in ms (0 = off, recover manually with RESTART) */
        bool setCanRestartMs(const std::string &strVal) const
        {
            uint32_t u32Ms = 0U;
            if (false == numeric::str2uint32(strVal, u32Ms)) {
                return false;
            }
            m_sLinkCfg.restartMsSet = true;
            m_sLinkCfg.restartMs    = u32Ms;
            m_bLinkDirty            = true;
            return true;
        }

        /** \brief set/clear one controller-mode bit; shared by the on/off setters below */
        bool setCanCtrlMode(uint32_t u32Bit, const std::string &strVal) const
        {
            bool bOn = false;
            if (false == kcan_parse::onOff(strVal, bOn)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |"); LOG_STRING("Expected on/off (or true/false, 1/0), got:"); LOG_STRING(strVal));
                return false;
            }
            m_sLinkCfg.ctrlModeMask |= u32Bit;
            if (bOn) {
                m_sLinkCfg.ctrlModeFlags |= u32Bit;
            } else {
                m_sLinkCfg.ctrlModeFlags &= ~u32Bit;
            }
            m_bLinkDirty = true;
            return true;
        }

        /** \brief CAN FD mode on/off (needs a data bitrate) */
        bool setCanFd(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_FD, strVal);
        }

        /** \brief listen-only (silent) mode: never transmits, never ACKs - for passive bus sniffing */
        bool setCanListenOnly(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_LISTEN_ONLY, strVal);
        }

        /** \brief controller internal loopback: frames never reach the bus - for self tests without wiring */
        bool setCanLoopback(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_LOOPBACK, strVal);
        }

        /** \brief one-shot: do not retransmit a frame that was not ACKed / lost arbitration */
        bool setCanOneShot(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_ONE_SHOT, strVal);
        }

        /** \brief triple sampling of every bit (slow, noisy buses) */
        bool setCanTripleSampling(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_TRIPLE_SAMPLING, strVal);
        }

        /** \brief report bus errors as error frames (logged by the driver) */
        bool setCanBerrReporting(const std::string &strVal) const
        {
            return setCanCtrlMode(kcan::CTRLMODE_BERR_REPORTING, strVal);
        }

        /** \brief apply the link configuration automatically on first use (default on) */
        bool setCanAutoLink(const std::string &strVal) const
        {
            return kcan_parse::onOff(strVal, m_bAutoLink);
        }

        /** \brief CAN_ERR_* class mask of error frames the driver should log (0 = none, 0x1FFFFFFF = all) */
        bool setCanErrMask(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_u32ErrMask);
        }

        /**
         * \brief set the CAN ID stamped on outgoing frames, and mirror it onto
         *        the default RX acceptance filter.
         *        Accepts decimal or 0x-prefixed hex strings.
         *
         *        The stored value follows the SocketCAN canid_t convention:
         *          - 11-bit standard IDs (<=0x7FF): stored as-is.
         *          - 29-bit extended IDs (>0x7FF) : CAN_EFF_FLAG (0x80000000)
         *            is set automatically if the caller did not set it already,
         *            so both "x=0x18DAF100" and "x=0x98DAF100" select EFF mode.
         *
         *        \note RX/TX default mirroring:
         *        Every time the TX id is (re)configured — whether from the
         *        CONFIG command's "x:" key or from the CAN_TX_ID ini entry —
         *        the default RX acceptance filter (m_vFilters) is replaced
         *        with a single entry that matches exactly this same CAN ID.
         *        m_u32CanTxId and m_vFilters therefore always hold the pair
         *        of "default" Tx/Rx identifiers: the ones applied to a freshly
         *        opened socket (see m_KCAN_CMD / m_KCAN_SCRIPT) and the ones
         *        a per-call xtra_params override is restored back to once
         *        that single call completes. Issue an explicit FILTER command
         *        afterwards if RX needs to listen on a different id than TX.
         */
        bool setCanTxId(const std::string &strTxId) const
        {
            static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
            static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
            static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

            uint32_t u32Id                         = 0U;
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
                u32Id &= CAN_SFF_MASK; // keep only 11 data bits
            }

            m_u32CanTxId              = u32Id;

            // Mirror the same id onto the default RX filter: build a single
            // acceptance filter that matches exactly this CAN ID (same EFF/SFF
            // convention as m_ParseFilters), and make it the new default,
            // replacing whatever filter set was previously in effect.
            KCAN::CanFilter sRxFilter = {};
            sRxFilter.can_id          = u32Id;
            sRxFilter.can_mask        = (u32Id & CAN_EFF_FLAG) ? (CAN_EFF_FLAG | CAN_EFF_MASK)
                                                               : CAN_SFF_MASK;

            m_vFilters.clear();
            m_vFilters.push_back(sRxFilter);

            return true;
        }

        /**
         * \brief set the CAN ID this plugin expects incoming (response)
         *        frames to arrive on, when it differs from the TX id.
         *
         *        Only meaningful once a transport protocol other than
         *        TpProtocol::NONE is selected (see setCanTpProtocol()):
         *        segmented protocols need to distinguish the id we
         *        transmit request frames on (m_u32CanTxId) from the id
         *        the peer's response/handshake frames arrive on
         *        (m_u32CanRxId) — e.g. UDS request 0x7E0 / response 0x7E8.
         *
         *        If this is never called, m_u32CanRxId mirrors
         *        m_u32CanTxId, preserving today's single-id behaviour
         *        (matches the default RX filter installed by setCanTxId()).
         *
         *        Same EFF-flag auto-detection / clamping rules as setCanTxId().
         */
        bool setCanRxId(const std::string &strRxId) const
        {
            static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
            static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
            static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

            uint32_t u32Id                         = 0U;
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
         * \brief select the multi-frame transport protocol used for
         *        payloads that don't fit in a single CAN/CAN-FD frame.
         *
         *        Accepted values (case-insensitive): "none" (default,
         *        current single-frame-only behaviour), "isotp" (ISO
         *        15765-2), "j1939" (SAE J1939-21 BAM/RTS-CTS).
         *
         *        Payloads that already fit in one frame are sent/received
         *        exactly as many physical frames as before regardless of
         *        this setting — it only changes what happens for payloads
         *        that would otherwise be rejected as too long.
         */
        bool setCanTpProtocol(const std::string &strProtocol) const
        {
            TpProtocol eProto = TpProtocol::NONE;
            if (false == tp_protocol_from_string(strProtocol, eProto)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |");
                          LOG_STRING("Unknown CAN transport protocol:"); LOG_STRING(strProtocol.c_str()));
                return false;
            }
            m_eTpProtocol = eProto;
            return true;
        }

        // ---- TpConfig tuning parameters -----------------------------------------
        //
        // All of these tune the protocol selected by setCanTpProtocol() above;
        // a field a given protocol doesn't use is simply ignored by that
        // protocol's implementation (see TpConfig.hpp). Every setter keeps its
        // own defaults from TpConfig's in-struct initializers until explicitly
        // overridden here, via CONFIG (see kcan_setup.hpp) or via INI
        // (see m_LocalSetParams()).

        /** \brief ISO-TP: BS sent in our Flow Control frames (0 = no limit). */
        bool setTpBlockSize(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.blockSize);
        }

        /** \brief ISO-TP: STmin sent in our Flow Control frames (raw encoded byte). */
        bool setTpStMin(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.stMin);
        }

        /** \brief ISO-TP: pad SF/CF/FC to 8 bytes (classic CAN convention). */
        bool setTpPadFrames(const std::string &strVal) const
        {
            BoolExprEvaluator sEvaluator;
            return sEvaluator.evaluate(strVal, m_sTpConfig.padFrames);
        }

        /** \brief ISO-TP: padding fill byte. */
        bool setTpPaddingByte(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.paddingByte);
        }

        /** \brief ISO-TP: N_Bs — max wait for Flow Control after our First Frame. */
        bool setTpTimeoutNBs(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutNBs_ms);
        }

        /** \brief ISO-TP: N_Cr — max wait for next Consecutive Frame from peer. */
        bool setTpTimeoutNCr(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutNCr_ms);
        }

        /** \brief ISO-TP: classic 12-bit length field limit. */
        bool setTpMaxMessageLen(const std::string &strVal) const
        {
            return numeric::str2sizet(strVal, m_sTpConfig.maxMessageLen);
        }

        /** \brief J1939-21: true = broadcast (BAM), false = peer-to-peer (RTS/CTS). */
        bool setJ1939UseBam(const std::string &strVal) const
        {
            BoolExprEvaluator sEvaluator;
            return sEvaluator.evaluate(strVal, m_sTpConfig.j1939UseBam);
        }

        /** \brief J1939-21: max packets we grant per CTS (RTS/CTS only). */
        bool setJ1939MaxPackets(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.j1939MaxPackets);
        }

        /** \brief J1939-21: T1 — max wait for CTS after RTS. */
        bool setTpTimeoutT1(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutT1_ms);
        }

        /** \brief J1939-21: T2 — max wait for a data packet after CTS. */
        bool setTpTimeoutT2(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutT2_ms);
        }

        /** \brief J1939-21: T3 — max wait for next CTS after a burst. */
        bool setTpTimeoutT3(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutT3_ms);
        }

        /** \brief J1939-21: Th (BAM) — max inter-packet gap on the receive side. */
        bool setTpTimeoutTh(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutTh_ms);
        }

        /** \brief J1939-21: message-size limit. */
        bool setJ1939MaxMessageLen(const std::string &strVal) const
        {
            return numeric::str2sizet(strVal, m_sTpConfig.j1939MaxMessageLen);
        }

        /** \brief CANopen SDO: Object Dictionary index of the entry being transferred. */
        bool setCanOpenIndex(const std::string &strVal) const
        {
            return numeric::str2uint16(strVal, m_sTpConfig.canOpenIndex);
        }

        /** \brief CANopen SDO: Object Dictionary sub-index. */
        bool setCanOpenSubIndex(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.canOpenSubIndex);
        }

        /** \brief CANopen SDO: true = block transfer, false = segmented transfer. */
        bool setCanOpenUseBlock(const std::string &strVal) const
        {
            BoolExprEvaluator sEvaluator;
            return sEvaluator.evaluate(strVal, m_sTpConfig.canOpenUseBlock);
        }

        /** \brief CANopen SDO: block transfer segments per block, 1-127. */
        bool setCanOpenBlockSize(const std::string &strVal) const
        {
            return numeric::str2uint8(strVal, m_sTpConfig.canOpenBlockSize);
        }

        /** \brief CANopen SDO: max wait for each SDO response frame. */
        bool setTpTimeoutSdo(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutSdo_ms);
        }

        /** \brief CANopen SDO: upper bound accepted before even trying. */
        bool setCanOpenMaxMessageLen(const std::string &strVal) const
        {
            return numeric::str2sizet(strVal, m_sTpConfig.canOpenMaxMessageLen);
        }

        /** \brief NMEA 2000 Fast Packet: max gap between consecutive frames. */
        bool setTpTimeoutFpInterFrame(const std::string &strVal) const
        {
            return numeric::str2uint32(strVal, m_sTpConfig.timeoutFpInterFrame_ms);
        }

        /** \brief NMEA 2000 Fast Packet: payload limit (6 + 31*7). */
        bool setFpMaxMessageLen(const std::string &strVal) const
        {
            return numeric::str2sizet(strVal, m_sTpConfig.fastPacketMaxMessageLen);
        }

        /**
         * \brief set KCAN read timeout
         */
        bool setCanReadTimeout(const std::string &strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
         * \brief set KCAN write timeout
         */
        bool setCanWriteTimeout(const std::string &strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
         * \brief set KCAN read buffer size
         * \note Valid range is 1–64 bytes (maximum CAN FD payload).
         */
        bool setCanReadBufferSize(const std::string &strReadBufferSize) const
        {
            static constexpr uint32_t CAN_FD_MAX_DLEN = 64U;
            uint32_t u32Size                          = 0U;
            if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
                return false;
            }
            if (u32Size == 0U || u32Size > CAN_FD_MAX_DLEN) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("KCAN |");
                          LOG_STRING("ReadBufSize out of range [1-64]:"); LOG_UINT32(u32Size));
                return false;
            }
            m_u32ReadBufferSize = u32Size;
            return true;
        }

    private:
        /**
         * \brief message sender — matches PFSEND<KCAN> (see ICommDriver.hpp) so it can be
         *        handed directly to CommScriptCommandInterpreter/CommScriptClient as a pfsend
         *        override (see m_KCAN_CMD/m_KCAN_SCRIPT). Dispatches on m_eTpProtocol:
         *        TpProtocol::NONE keeps today's exact one-call-one-frame behaviour; any other
         *        protocol segments through cantp instead. Either way this function — not the
         *        generic interpreter — is now responsible for the GUI comm-dump row(s), so that
         *        a segmented send shows every physical frame it actually put on the wire.
         */
        ICommDriver::WriteResult m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                        std::shared_ptr<const KCAN> shpDriver, std::string_view xtra_params,
                                        std::stop_token stop_tok = {}) const;

        /**
         * \brief message receiver — matches PFRECV<KCAN>, same idea as m_Send() above.
         *        UntilDelimiter/UntilToken reads always fall back to the driver's legacy
         *        framing (see body) since segmented binary transports have no such concept.
         */
        ICommDriver::ReadResult m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                          const ICommDriver::ReadOptions &options,
                                          std::shared_ptr<const KCAN> shpDriver, std::string_view xtra_params,
                                          std::stop_token stop_tok = {}) const;

        /**
         * \brief processing of the plugin specific settings
         */
        bool m_LocalSetParams(const PluginDataSet *psSetParams);

        /**
         * \brief Make the CAN interface match m_sLinkCfg: bitrate / sample point / FD / modes, then up.
         *        Does nothing (and needs no privileges) when the interface is already up and matches.
         *        Serialised by m_mtxLink so concurrent CYCLIC '&' threads cannot race each other.
         */
        bool m_ApplyLink() const;

        /**
         * \brief Common "open the CAN socket" step for CMD / SCRIPT / CYCLIC: applies the pending link
         *        configuration (if KCAN_AUTO_LINK), opens the socket, installs TX id, filters and error mask.
         * \return the open driver, or nullptr (reason already logged)
         */
        std::shared_ptr<KCAN> m_OpenDriver() const;

        /**
         * \brief helper: parse a comma-separated filter list string into KCAN::CanFilter entries.
         *        Each entry has the form "<id>:<mask>" (hex or decimal).
         *        Example: "0x100:0x7FF,0x200:0x7FF"
         *
         *        The function automatically propagates CAN_EFF_FLAG / CAN_RTR_FLAG /
         *        CAN_ERR_FLAG from can_id into can_mask so SocketCAN's kernel filter
         *        comparison ((frame_id & mask) == (id & mask)) is unambiguous.
         *        An id > 0x7FF without CAN_EFF_FLAG set triggers a warning and the
         *        flag is added automatically.
         */
        bool m_ParseFilters(const std::string &strFilters, std::vector<KCAN::CanFilter> &vFilters) const;

        /**
         * \brief map with association between the command string and the execution function
         */
        PluginCommandsMap<KCANPlugin> m_mapCmds;

        /**
         * \brief plugin version
         */
        std::string m_strVersion;

        /**
         * \brief runtime instance identity used for the GUI comm-dump panel
         *        (e.g. "KCAN" or "KCAN:1" -- see PluginDataSet::strInstanceName).
         *        Falls back to the fixed plugin name macro when unset (e.g.
         *        standalone construction outside the script interpreter).
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
         * \brief SocketCAN interface name (e.g. "vcan0", "can1")
         */
        mutable std::string m_strCanIface;

        /**
         * \brief CAN ID stamped on every outgoing frame
         */
        mutable uint32_t m_u32CanTxId;

        /**
         * \brief true once setCanRxId() has been called explicitly; until
         *        then m_u32CanRxId mirrors m_u32CanTxId (see setCanRxId()).
         */
        mutable bool m_bCanRxIdSet;

        /**
         * \brief CAN ID expected for incoming response/handshake frames
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
         * \brief KCAN read timeout in milliseconds
         */
        mutable uint32_t m_u32ReadTimeout;

        /**
         * \brief KCAN write timeout in milliseconds
         */
        mutable uint32_t m_u32WriteTimeout;

        /**
         * \brief size of the buffer used for KCAN read operations (max 64 bytes for CAN FD)
         */
        mutable uint32_t m_u32ReadBufferSize;

        /**
         * \brief acceptance filters applied to the open socket (empty = accept all)
         */
        mutable std::vector<KCAN::CanFilter> m_vFilters;

        /**
         * \brief wanted interface state (bitrate, sample point, FD, controller modes ...); empty = leave interface alone
         */
        mutable kcan::LinkConfig m_sLinkCfg;

        /**
         * \brief true when m_sLinkCfg / the interface name changed since it was last applied to the kernel
         */
        mutable bool m_bLinkDirty     = false;

        /**
         * \brief apply the link configuration automatically before first use (see setCanAutoLink())
         */
        mutable bool m_bAutoLink      = true;

        /**
         * \brief CAN_ERR_* mask of error frames to receive/log (0 = none)
         */
        mutable uint32_t m_u32ErrMask = 0U;

        /**
         * \brief serialises m_ApplyLink()
         */
        mutable std::mutex m_mtxLink;

/**
 * \brief functions associated to the plugin commands
 */
#define KCAN_PLUGIN_CMD_RECORD(a, ...) bool m_KCAN_##a(const std::string &args, std::stop_token st) const;
        KCAN_PLUGIN_COMMANDS_CONFIG_TABLE
#undef KCAN_PLUGIN_CMD_RECORD
};

#endif /* KCAN_PLUGIN_HPP */
