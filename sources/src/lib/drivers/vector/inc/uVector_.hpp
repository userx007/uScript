#ifndef U_VECTOR_DRIVER_H
#define U_VECTOR_DRIVER_H

#include "ICommDriver.hpp"
#include "ITransportProtocol.hpp"
#include "TpFactory.hpp"
#include "TpConfig.hpp"
#include "uGuiNotify.hpp"
#include "uVectorDriverHandle.hpp"
#include "uVectorNotifyWaiter.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <span>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stop_token>

// XL Driver Library (XL-API) header — supplied by Vector Informatik alongside
// the "Vector Driver Setup" installer on Windows (normally under
// "Vector XL Driver Library\bin\vxlapi.h" once installed, or in the vendor's
// XL Driver Library SDK download), or the unofficial Linux port's header on
// Linux — see vxlapi_linux.h's own header comment and
// third_party/CMakeLists.txt for what that is, how it was derived, and the
// expected vendoring layout. vxlapi_platform.hpp selects the right one and
// pulls in whatever else that platform's declarations need (windows.h /
// unistd.h); see that header for details.
//
// Both vendored headers are Vector's/the port's own, unmodified beyond what
// each file's own header comment documents (cover CAN/CAN-FD, LIN, FlexRay,
// MOST, Ethernet, A429, K-Line, DAIO — this driver only uses the CAN/CAN-FD
// subset, identical on both platforms). If you have a newer/older SDK
// installed, prefer pointing VECTOR_XLAPI_INCLUDE_DIR / VECTOR_XLAPI_LIB_DIR
// (see this component's CMakeLists.txt) at it instead of using the vendored
// copy, same as the PCAN driver's PCAN-Basic vendoring.
#include "vxlapi_platform.hpp"


/**
 * @brief Vector XL-API driver wrapper implementing ICommDriver.
 *
 * Talks to Vector Informatik CAN/CAN-FD interfaces (VN1610/VN16xx, VN5xxx,
 * VN7xxx, VN89xx, VX1xxx, ...) through the XL Driver Library — vxlapi64.dll
 * on Windows, the unofficial libXlApi.so.26.20.14 port on Linux (see
 * vxlapi_platform.hpp).
 *
 * Platform-specific code is confined to two places, both confirmed against
 * the actual exported symbols of libXlApi.so.26.20.14 (checked with a
 * symbol-table dump against that .so, not just header presence — the header
 * declares several functions the Linux port doesn't actually implement):
 *  - The notification wait primitive: VectorNotifyWaiter (Windows HANDLE/
 *    WaitForSingleObject vs. Linux eventfd/poll()).
 *  - Port creation and channel enumeration, in m_OpenWithMask_locked() and
 *    enumerateChannels(): Windows uses xlOpenPort()/xlGetDriverConfig();
 *    Linux has neither and instead builds a port up one channel at a time
 *    (xlCreatePort()/xlAddChannelToPort()/xlFinalizePort()) and enumerates
 *    through a small versioned function-pointer interface
 *    (xlCreateDriverConfig()'s fctGetChannelConfig()/fctGetDeviceConfig()) —
 *    see those two functions' own comments for the full rationale.
 * Every downstream call once a port is open/channels are known — CAN FD
 * config, activate/deactivate, transmit/receive, bitrate — is identical on
 * both platforms, confirmed present in both headers AND both .so/.dll
 * exports. Structurally this mirrors the uPcan driver almost exactly (same
 * ICommDriver framing, same TpFactory-based multi-frame transport-protocol
 * dispatch, same RawIo indirection) with the frame-level primitives swapped
 * for XL-API calls.
 *
 * Device selection
 * ----------------
 * Unlike PCAN-Basic's single numeric channel handle, XL-API resolves a
 * physical channel through Vector Hardware Config: open() takes an
 * application name (as configured in "Vector Hardware Config") and a
 * zero-based index into that application's assigned channels, then calls
 * xlGetApplConfig()/xlGetChannelMask() to turn that into the access mask
 * XL-API's other calls need. If the application name isn't found in Vector
 * Hardware Config, or has no channel at that index, open() fails —  there is
 * no way to address a channel purely by device index the way PCAN's
 * PCAN_USBBUS1 constant does.
 *
 * Driver lifetime
 * ----------------
 * xlOpenDriver()/xlCloseDriver() are process-wide, not per-channel, and
 * shared with every other XL-API driver in this codebase (see
 * VectorDriverHandle) — the very first XL-API channel opened anywhere in the
 * process (CAN via this class, or Ethernet via VectorEth) calls
 * xlOpenDriver(), and the very last one closed calls xlCloseDriver().
 *
 * CAN / CAN FD
 * ------------
 *  - Classic CAN: bytes are packed into consecutive frames, up to
 *    VECTOR_MAX_PAYLOAD (8) bytes each, via the classic xlCanTransmit()/
 *    xlReceive() event pair (XL_INTERFACE_VERSION_V3).
 *  - CAN FD (bFD=true in open()/openDirect()): bytes are packed into
 *    consecutive frames, up to VECTOR_FD_MAX_PAYLOAD (64) bytes each, via
 *    the FD-aware xlCanTransmitEx()/xlCanReceive() event pair
 *    (XL_INTERFACE_VERSION_V4, required for FD). A fragment shorter than 64
 *    bytes that doesn't land exactly on one of the 16 legal CAN-FD DLC
 *    lengths (0-8, 12, 16, 20, 24, 32, 48, 64 — see canFdLenToDlc()) is
 *    padded up to the next legal length with setFdPaddingByte() (default
 *    0x00), same convention the can_tp library uses for classic-CAN
 *    ISO-TP/J1939 padding.
 *  - setFdDataBitrate() configures the CAN FD data-phase bitrate
 *    (xlCanFdSetConfiguration(); the bitrate passed to open()/openDirect()
 *    becomes the arbitration-phase bitrate). setFdIso() selects ISO
 *    11898-1:2015 CAN FD (default) vs the pre-standard Bosch/"non-ISO"
 *    variant (CANFD_CONFOPT_NO_ISO). setFdBrs() controls whether outgoing FD
 *    frames use the data-phase bitrate switch (BRS) — on by default; turning
 *    it off sends FD-framed (EDL) messages at the arbitration bitrate only,
 *    useful against FD-transceiver-only hardware that can't yet do BRS.
 *  - Every read/receive path (readExact/readUntilDelimiter/readUntilToken/
 *    readOneFrame_locked/frameMatchesFilter/dumpFrame) is written once
 *    against the mode-agnostic VectorRxFrame struct recvFrame() produces, so
 *    classic and FD channels share the exact same read-mode code — only
 *    recvFrame()/sendFrame() themselves branch on m_bFD.
 *
 * Reading
 * -------
 *  - Same ReadMode::Exact / UntilDelimiter / UntilToken semantics as every
 *    other CAN driver in this codebase (see PCAN::tout_read()'s doc comment
 *    for the exact per-mode behaviour) — the only thing that differs here is
 *    how a single physical frame is pulled off the bus (recvFrame()).
 *
 * Multi-frame transport protocols (can_tp)
 * -----------------------------------------
 * Same design as uPcan.hpp: the naive fragmentation loop
 * (writeFragmented_locked()/readDispatch_locked()) is the default
 * (TpProtocol::NONE); setTpProtocol(TpProtocol::ISO_TP / ::J1939_TP) routes
 * tout_write()/tout_read() through the shared can_tp library instead, via
 * the same RawIo indirection PCAN uses so a transport protocol's own
 * single-frame SF/FF/CF/FC traffic never re-enters the TP-aware public
 * entry points or re-locks m_mutex. can_tp itself still frames one classic
 * or FD CAN frame at a time through writeFragmented_locked()/
 * readOneFrame_locked() exactly as before — CAN FD simply raises the
 * per-frame ceiling those two already respect.
 */
/// Default CAN FD data-phase bitrate in bps — namespace-scope so VectorFdOptions
/// (below) can use it in a default member initializer without depending on
/// Vector::VECTOR_DEFAULT_FD_DATA_BITRATE, which — being a member of Vector —
/// would not be usable here (see VectorFdOptions's own comment for why).
/// Vector::VECTOR_DEFAULT_FD_DATA_BITRATE (still declared, see below) is just
/// this same value re-exposed as a class member for API-compatibility/doc
/// discoverability; there is exactly one source of truth.
constexpr uint32_t VECTOR_FD_DEFAULT_DATA_BITRATE = 2000000;

/**
 * @brief CAN FD tuning, applied atomically by open()/openDirect() before the
 *        channel is actually opened (xlCanFdSetConfiguration() needs
 *        u32DataBitrate/bIso at open time; bBrs/u8PaddingByte only affect
 *        later sendFrame() calls and can also be changed afterwards via
 *        setFdBrs()/setFdPaddingByte()). Ignored entirely when bFD=false.
 *
 * Deliberately NOT a nested type of Vector (despite being used only there,
 * as Vector::FdOptions — see the alias just inside the class): a default
 * member initializer of a class nested inside another class cannot be used
 * — even indirectly, via default-constructing the nested type as a default
 * FUNCTION ARGUMENT — until the OUTERMOST enclosing class is itself complete
 * ([class.mem]); Vector's own constructors/open()/openDirect() need exactly
 * that (a default-constructed FdOptions as a default argument) from within
 * Vector's own class body, which is a contradiction a nested definition
 * can't satisfy. Defining it here at namespace scope instead sidesteps the
 * rule entirely: this type is complete (NSDMIs and all) long before class
 * Vector even starts.
 */
struct VectorFdOptions
{
    uint32_t u32DataBitrate = VECTOR_FD_DEFAULT_DATA_BITRATE; ///< Data-phase bitrate in bps.
    bool     bIso           = true;  ///< true = ISO 11898-1:2015, false = Bosch/non-ISO.
    bool     bBrs           = true;  ///< true = outgoing FD frames use the bitrate switch.
    uint8_t  u8PaddingByte  = 0x00;  ///< Fill byte for short fragments (see Vector::canFdLenToDlc()).
};


class Vector : public ICommDriver
{

    public:

        // ------------------------------------------------------------------ //
        //  Constants                                                           //
        // ------------------------------------------------------------------ //

        static constexpr size_t   VECTOR_MAX_PAYLOAD          = 8;      ///< Classic CAN max payload bytes per frame.
        static constexpr size_t   VECTOR_FD_MAX_PAYLOAD        = 64;    ///< CAN FD max payload bytes per frame.
        static constexpr uint32_t VECTOR_READ_DEFAULT_TIMEOUT  = 5000;  ///< Default RX timeout in milliseconds.
        static constexpr uint32_t VECTOR_WRITE_DEFAULT_TIMEOUT = 5000;  ///< Default TX timeout in milliseconds.
        static constexpr uint32_t VECTOR_DEFAULT_TX_ID         = 0x7FF; ///< Default TX CAN ID.
        static constexpr uint32_t VECTOR_DEFAULT_RX_FILTER_ID  = 0x000; ///< 0 = accept all (open filter).
        static constexpr uint32_t VECTOR_RX_QUEUE_SIZE         = 256;   ///< xlOpenPort() RX event queue depth.
        static constexpr uint32_t VECTOR_DEFAULT_FD_DATA_BITRATE = VECTOR_FD_DEFAULT_DATA_BITRATE; ///< Default CAN FD data-phase bitrate.

        /// CAN FD tuning bundle — see VectorFdOptions (namespace scope, just above
        /// this class) for the full doc comment and why it's defined out there
        /// instead of nested here.
        using FdOptions = VectorFdOptions;

        /// SocketCAN canid_t convention used by every CAN plugin in this
        /// codebase (KVCAN, SLCAN, PCAN, Vector) for TX ids, RX filter ids,
        /// and xtra_params overrides: bit 31 set -> 29-bit extended frame.
        /// XL-API's own XLcanMsg::id field has no such flag bit — extended-
        /// ness is carried by the XL_CAN_EXT_MSG_ID bit already OR'd into the
        /// id itself — so values in this convention are normalised (flag
        /// translated, data bits masked) at the sendFrame()/frameMatchesFilter()
        /// boundary, same as PCAN does for its own MSGTYPE-based convention.
        static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
        static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
        static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;

        // ------------------------------------------------------------------ //
        //  CAN FD DLC <-> byte length mapping (ISO 11898-1)                    //
        // ------------------------------------------------------------------ //

        /// The 16 legal CAN-FD per-frame data lengths, indexed by DLC code 0-15.
        static constexpr std::array<uint8_t, 16> FD_DLC_LENGTH_TABLE = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
        };

        /** DLC code (0-15) -> byte length (0,1,...,8,12,16,20,24,32,48,64). */
        static constexpr size_t canFdDlcToLen(uint8_t u8Dlc)
        {
            return (u8Dlc < FD_DLC_LENGTH_TABLE.size()) ? FD_DLC_LENGTH_TABLE[u8Dlc] : 64U;
        }

        /** Smallest legal CAN-FD DLC code whose length is >= szLen (szLen must be <= 64). */
        static constexpr uint8_t canFdLenToDlc(size_t szLen)
        {
            for (uint8_t i = 0; i < FD_DLC_LENGTH_TABLE.size(); ++i) {
                if (FD_DLC_LENGTH_TABLE[i] >= szLen) return i;
            }
            return 15U; // szLen > 64 - caller clamps before this is reached
        }

        // ------------------------------------------------------------------ //
        //  Device enumeration / direct selection                               //
        // ------------------------------------------------------------------ //

        /**
         * @brief One XL-API channel as reported by xlGetDriverConfig(), independent
         *        of any Vector Hardware Config application assignment.
         */
        struct ChannelInfo
        {
            std::string strName;            ///< XL-API channel name, e.g. "VN1610 Channel 1".
            uint32_t    u32HwType   = 0;    ///< XL_HWTYPE_* raw value.
            std::string strHwType;          ///< Human-readable name (see hwTypeToString()), or "HWTYPE_<n>" if unknown.
            uint32_t    u32HwIndex  = 0;    ///< Index of the physical hardware (same type), 0,1,...
            uint32_t    u32HwChannel = 0;   ///< Index of the channel (connector) on that hardware, 0,1,...
            uint32_t    u32ChannelIndex = 0; ///< Global XL-API channel index.
            XLaccess    xlChannelMask = 0;  ///< Global access mask (= 1 << u32ChannelIndex).
            uint32_t    u32SerialNumber = 0; ///< Device serial number (0 if not applicable, e.g. XL_HWTYPE_VIRTUAL).
            bool        bIsOnBus     = false; ///< True if some application already activated this channel.
            bool        bSupportsCan = false; ///< True if XL_BUS_ACTIVE_CAP_CAN is set for this channel.
            bool        bSupportsCanFdIso   = false; ///< True if the channel's CAN transceiver/FPGA can do ISO CAN FD.
            bool        bSupportsCanFdBosch = false; ///< True if it can do the pre-standard Bosch/non-ISO CAN FD variant.
            bool        bSupportsEthernet   = false; ///< True if XL_BUS_ACTIVE_CAP_ETHERNET is set (see VectorEth).
            bool        bSupportsLin        = false; ///< True if XL_BUS_ACTIVE_CAP_LIN is set (informational only - no LIN driver in this codebase yet).
        };

        /**
         * @brief Criteria for selecting a physical channel directly via
         *        enumerateChannels(), bypassing Vector Hardware Config entirely.
         *        All set fields must match; unset fields (sentinel default) are
         *        "don't care". At least one of i32HwType / u32SerialNumber /
         *        strChannelName must be set, or every CAN-capable channel in the
         *        system would match and openDirect() would refuse as ambiguous.
         */
        struct DeviceSelector
        {
            int32_t     i32HwType      = -1;  ///< XL_HWTYPE_* value, or parse via hwTypeFromString(). -1 = don't care.
            uint32_t    u32SerialNumber = 0;  ///< 0 = don't care.
            std::string strChannelName;       ///< Exact XL-API channel name match. Empty = don't care.
            int32_t     i32HwIndex     = -1;  ///< Disambiguates multiple boards of the same hwType. -1 = don't care.
            int32_t     i32HwChannel   = -1;  ///< Disambiguates multiple connectors on the same board. -1 = don't care.
        };

        /**
         * @brief Enumerate every channel XL-API currently knows about (opens and
         *        immediately releases the process-wide driver handle — does NOT
         *        require any Vector instance to already have a channel open).
         * @return All reported channels, CAN-capable or not (see ChannelInfo::bSupportsCan).
         */
        static std::vector<ChannelInfo> enumerateChannels();

        /**
         * @brief Enumerate channels and keep only those matching every set field of sel.
         *        Only CAN-capable channels are considered (see openDirect()).
         * @return Zero, one, or many matches — callers needing exactly one (openDirect())
         *         must check size() themselves and report ambiguity/absence distinctly.
         */
        static std::vector<ChannelInfo> matchChannels(const DeviceSelector& sel);

        /** Map an XL_HWTYPE_* value to a short human-readable name, e.g. 55 -> "VN1610". */
        static std::string hwTypeToString(uint32_t u32HwType);

        /** Parse a device type name (case-insensitive, e.g. "VN1610") or a raw numeric
         *  XL_HWTYPE_* value into u32HwType. Returns false if strName matches neither. */
        static bool hwTypeFromString(const std::string& strName, uint32_t& u32HwType);

        // ------------------------------------------------------------------ //
        //  Construction / destruction                                          //
        // ------------------------------------------------------------------ //

        Vector() = default;

        /**
         * @brief Convenience constructor — opens the channel immediately.
         * @param strAppName       Application name as configured in "Vector Hardware Config".
         * @param u32AppChannel    Zero-based index into that application's assigned channels.
         * @param u32Bitrate       CAN bitrate in bps (e.g. 500000). CAN FD: arbitration-phase bitrate.
         * @param u32TxId          Default TX CAN ID.
         * @param bExtended        Force 29-bit extended frame format (auto-detected when false).
         * @param bFD              CAN FD mode — see setFdDataBitrate()/setFdIso()/setFdBrs() for the
         *                         data-phase bitrate and mode tuning (defaults: 2 Mbit/s, ISO, BRS on).
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()), e.g. "VN1610 ch0".
         * @param strInstanceName  Runtime instance identity for the GUI comm-dump panel's
         *                         "Plugin" column. Falls back to plain "Vector" when empty.
         */
        explicit Vector(const std::string& strAppName,
                        uint32_t           u32AppChannel     = 0,
                        uint32_t           u32Bitrate        = 500000,
                        uint32_t           u32TxId           = VECTOR_DEFAULT_TX_ID,
                        bool               bExtended         = false,
                        bool               bFD               = false,
                        const std::string& strIdentityLabel  = {},
                        const std::string& strInstanceName   = {},
                        const FdOptions&   fdOpts             = FdOptions())
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "Vector" : strInstanceName)
        {
            open(strAppName, u32AppChannel, u32Bitrate, u32TxId, bExtended, bFD, fdOpts);
        }

        /**
         * @brief Convenience constructor — resolves sel via matchChannels() and opens it
         *        immediately, bypassing Vector Hardware Config entirely. See openDirect().
         */
        explicit Vector(const DeviceSelector& sel,
                        uint32_t           u32Bitrate        = 500000,
                        uint32_t           u32TxId           = VECTOR_DEFAULT_TX_ID,
                        bool               bExtended         = false,
                        bool               bFD               = false,
                        const std::string& strIdentityLabel  = {},
                        const std::string& strInstanceName   = {},
                        const FdOptions&   fdOpts             = FdOptions())
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "Vector" : strInstanceName)
        {
            openDirect(sel, u32Bitrate, u32TxId, bExtended, bFD, fdOpts);
        }

        virtual ~Vector()
        {
            close();
        }

        // ------------------------------------------------------------------ //
        //  Lifecycle                                                           //
        // ------------------------------------------------------------------ //

        /**
         * @brief Open and initialise a Vector CAN/CAN-FD channel via XL-API.
         * @param strAppName    Application name as configured in "Vector Hardware Config".
         * @param u32AppChannel Zero-based index into that application's assigned channels.
         * @param u32Bitrate    CAN bitrate in bps (CAN FD: arbitration-phase bitrate).
         * @param u32TxId       Default TX CAN ID used when xtra_params is empty.
         * @param bExtended     Force 29-bit extended frame format.
         * @param bFD           CAN FD mode. See setFdDataBitrate()/setFdIso()/setFdBrs() to tune
         *                      before calling this, or call them before the next open() to change
         *                      an already-open channel (they take effect on the next open/openDirect).
         * @return Status::SUCCESS on success, appropriate error code otherwise.
         */
        Status open(const std::string& strAppName,
                    uint32_t           u32AppChannel = 0,
                    uint32_t           u32Bitrate     = 500000,
                    uint32_t           u32TxId        = VECTOR_DEFAULT_TX_ID,
                    bool               bExtended      = false,
                    bool               bFD            = false,
                    const FdOptions&   fdOpts         = FdOptions());

        /**
         * @brief Open and initialise a Vector CAN/CAN-FD channel by resolving sel via
         *        matchChannels() directly — bypassing Vector Hardware Config's
         *        application-name/index indirection entirely.
         * @param sel        Selection criteria; must match exactly one CAN-capable channel.
         * @param u32Bitrate CAN bitrate in bps (CAN FD: arbitration-phase bitrate).
         * @param u32TxId    Default TX CAN ID used when xtra_params is empty.
         * @param bExtended  Force 29-bit extended frame format.
         * @param bFD        CAN FD mode — see open().
         * @return Status::SUCCESS on success. Status::INVALID_PARAM if sel matched zero or
         *         more than one channel (both cases are logged with the full candidate list
         *         so the caller can tighten sel), or if the single match isn't CAN-capable.
         */
        Status openDirect(const DeviceSelector& sel,
                          uint32_t           u32Bitrate = 500000,
                          uint32_t           u32TxId    = VECTOR_DEFAULT_TX_ID,
                          bool               bExtended  = false,
                          bool               bFD        = false,
                          const FdOptions&   fdOpts     = FdOptions());

        /**
         * @brief Deactivate the channel, close the XL-API port, and release the
         *        process-wide driver handle if this was the last open instance.
         * @return Status::SUCCESS always.
         */
        Status close();

        /**
         * @brief Check whether the channel is currently open.
         */
        bool is_open() const override;

        /**
         * @brief Describe this connection for the GUI comm-dump panel.
         *
         * Reuses resolveTxId() — the exact same resolution tout_write() itself
         * applies — so the label always reflects the CAN ID actually used,
         * including any per-call xtra_params override.
         */
        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            const uint32_t id = resolveTxId(xtra_params);
            const bool     ext = (id & CAN_EFF_FLAG) || m_bExtendedId || (id & CAN_EFF_MASK) > CAN_SFF_MASK;
            char label[k_labelSize];
            std::snprintf(label, sizeof(label), "%s id=0x%X%s%s",
                          m_strIdentityLabel.empty() ? "Vector" : m_strIdentityLabel.c_str(),
                          id & CAN_EFF_MASK, ext ? " (ext)" : "", m_bFD ? " (FD)" : "");
            return commdump_details(CommFamily::CAN, label);
        }

        // ------------------------------------------------------------------ //
        //  ICommDriver interface                                               //
        // ------------------------------------------------------------------ //

        ReadResult tout_read(uint32_t           u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view   xtra_params = {},
                             std::stop_token    stop_tok = {}) const override;

        WriteResult tout_write(uint32_t                  u32WriteTimeout,
                               std::span<const uint8_t>  buffer,
                               std::string_view          xtra_params = {},
                               std::stop_token           stop_tok = {}) const override;

        // ------------------------------------------------------------------ //
        //  Configuration helpers                                               //
        // ------------------------------------------------------------------ //

        void setDefaultTxId(uint32_t u32Id)          { m_u32DefaultTxId = u32Id; }
        void setDefaultRxFilterId(uint32_t u32Id)    { m_u32DefaultRxFilterId = u32Id; }
        void setExtendedId(bool bExt)                { m_bExtendedId = bExt; }

        uint32_t getDefaultTxId()       const        { return m_u32DefaultTxId; }
        uint32_t getDefaultRxFilterId() const        { return m_u32DefaultRxFilterId; }
        bool     isFD()                 const        { return m_bFD; }

        /** Query the resolved XL-API channel access mask (0 if not open). */
        XLaccess getAccessMask()        const        { return m_xlAccessMask; }

        // ------------------------------------------------------------------ //
        //  CAN FD configuration (take effect on the NEXT open()/openDirect())  //
        // ------------------------------------------------------------------ //

        /** CAN FD data-phase bitrate in bps. Only meaningful when bFD=true is passed to open(). */
        void setFdDataBitrate(uint32_t u32DataBitrate) { m_u32FdDataBitrate = u32DataBitrate; }
        uint32_t getFdDataBitrate() const              { return m_u32FdDataBitrate; }

        /** true (default) = ISO 11898-1:2015 CAN FD, false = pre-standard Bosch/"non-ISO" CAN FD. */
        void setFdIso(bool bIso)                       { m_bFdIso = bIso; }
        bool getFdIso() const                          { return m_bFdIso; }

        /** true (default) = outgoing FD frames use the data-phase bitrate switch (BRS). */
        void setFdBrs(bool bBrs)                        { m_bFdBrs = bBrs; }
        bool getFdBrs() const                           { return m_bFdBrs; }

        /** Fill byte used to pad a short FD fragment up to the next legal CAN-FD DLC length. */
        void setFdPaddingByte(uint8_t u8Byte)           { m_u8FdPaddingByte = u8Byte; }
        uint8_t getFdPaddingByte() const                { return m_u8FdPaddingByte; }

        // ------------------------------------------------------------------ //
        //  Transport-protocol configuration                                    //
        // ------------------------------------------------------------------ //

        void setTpProtocol(TpProtocol eProto)         { m_eTpProtocol = eProto; }
        void setTpConfig(const TpConfig& cfg)         { m_sTpConfig = cfg; }
        void setTpRxId(uint32_t u32Id)                { m_u32TpRxId = u32Id; m_bTpRxIdSet = true; }

    private:

        // ------------------------------------------------------------------ //
        //  Mode-agnostic received-frame representation                        //
        // ------------------------------------------------------------------ //

        /**
         * @brief One received CAN or CAN-FD frame, normalised out of either the
         *        classic XLevent/XLcanMsg or the FD XLcanRxEvent/XL_CAN_EV_RX_MSG
         *        payload by recvFrame() — every read-mode helper below
         *        (readExact/readUntilDelimiter/readUntilToken/readOneFrame_locked)
         *        is written once against this struct and never touches the
         *        raw XL-API event types itself.
         */
        struct VectorRxFrame
        {
            uint32_t u32Id       = 0;     ///< Raw CAN identifier (11 or 29 bit, EXT flag already stripped).
            bool     bExtended   = false;
            uint8_t  u8Len       = 0;     ///< Actual payload length in bytes (0-8 classic, 0-64 FD).
            std::array<uint8_t, VECTOR_FD_MAX_PAYLOAD> data{};
        };

        // ------------------------------------------------------------------ //
        //  State                                                               //
        // ------------------------------------------------------------------ //

        XLportHandle       m_xlPort              = XL_INVALID_PORTHANDLE; ///< XL-API port handle.
        XLaccess           m_xlAccessMask         = 0;                    ///< Resolved channel access mask.
        VectorNotifyWaiter m_notifyWaiter;                                ///< Cross-platform wrapper around xlSetNotification()'s wait primitive.
        bool               m_bOpen               = false;
        bool               m_bExtendedId         = false;
        bool               m_bFD                 = false;                ///< CAN FD mode, set by the most recent open()/openDirect().
        uint32_t           m_u32DefaultTxId      = VECTOR_DEFAULT_TX_ID;
        uint32_t           m_u32DefaultRxFilterId = VECTOR_DEFAULT_RX_FILTER_ID;
        mutable std::mutex m_mutex;
        std::string        m_strIdentityLabel;
        std::string        m_strInstanceName{"Vector"};

        uint32_t           m_u32FdDataBitrate    = VECTOR_DEFAULT_FD_DATA_BITRATE;
        bool               m_bFdIso              = true;
        bool               m_bFdBrs              = true;
        uint8_t            m_u8FdPaddingByte     = 0x00;

        TpProtocol m_eTpProtocol = TpProtocol::NONE;
        TpConfig   m_sTpConfig;
        bool       m_bTpRxIdSet  = false;
        uint32_t   m_u32TpRxId   = 0U;

        // ------------------------------------------------------------------ //
        //  Internal helpers                                                    //
        // ------------------------------------------------------------------ //

        /**
         * @brief Shared tail end of open()/openDirect(): given an already-resolved
         *        access mask, open the XL-API port (V3 for classic CAN, V4 for CAN
         *        FD), configure the bitrate(s) (if permitted), install the RX
         *        notification, and activate the channel.
         *
         * ASSUMES m_mutex IS ALREADY HELD and VectorDriverHandle::Acquire() has
         * ALREADY been called by the caller (which remains responsible for
         * calling VectorDriverHandle::Release() if this returns anything other
         * than Status::SUCCESS).
         */
        Status m_OpenWithMask_locked(XLaccess accessMask,
                                     uint32_t u32Bitrate,
                                     uint32_t u32TxId,
                                     bool     bExtended,
                                     bool     bFD);

        static bool parseUint32(std::string_view sv, uint32_t& out);

        uint32_t resolveTxId(std::string_view xtra_params) const;
        uint32_t resolveRxId(std::string_view xtra_params) const;
        uint32_t resolveTpRxId(std::string_view xtra_params) const;

        void dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const;

        /** Check whether a received frame matches an RX filter id (SocketCAN canid_t convention). */
        bool frameMatchesFilter(const VectorRxFrame& frame, uint32_t u32RxFilterId) const;

        /** Map an XLstatus return code to ICommDriver::Status. */
        static Status mapXlError(XLstatus sts);

        /**
         * @brief Receive one CAN or CAN-FD data frame within the given timeout,
         *        skipping non-data events (chip state, bus errors, our own TX
         *        echo/ack, ...) and normalising it into out.
         *
         * Branches internally on m_bFD:
         *  - classic: drains xlReceive()/XLevent, accepting only XL_RECEIVE_MSG
         *    events without ERROR_FRAME/OVERRUN/NERR/TX_COMPLETED flags (the
         *    TX_COMPLETED skip is XL-API echoing every frame THIS port
         *    transmits back through xlReceive() as a confirmation event — see
         *    the original rationale in the class's git history, cross-checked
         *    against ETAS/RBEI's BUSMASTER XL-API driver).
         *  - CAN FD: drains xlCanReceive()/XLcanRxEvent, accepting only
         *    XL_CAN_EV_TAG_RX_OK events (there is no ambiguity to resolve here:
         *    XL_CAN_EV_TAG_TX_OK is XL-API's own separate tag for our TX echo,
         *    so unlike the classic path there is no flag to inspect - any
         *    RX_OK event is a genuine frame from the bus).
         *
         * Blocks on m_notifyWaiter (installed by open() via xlSetNotification())
         * for up to u32TimeoutMs milliseconds, then drains the receive call
         * until a data event is found or the queue is empty, in which case it
         * waits again for the remaining timeout budget.
         *
         * Cancellation: a std::stop_callback registered for the whole call
         * invokes m_notifyWaiter.forceWake() the moment stop_tok is
         * requested, waking whichever wait() happens to be blocked exactly
         * like a real notification would (see VectorNotifyWaiter's class
         * comment for why that's safe on both platforms). m_notifyWaiter is
         * long-lived and shared across every call for this channel's
         * lifetime, so a spurious wake from our own forceWake() is
         * indistinguishable from a genuine one at the OS level; stop_tok is
         * checked immediately after every wait returns (and again at the top
         * of the retry loop) to tell the two apart and return
         * Status::READ_TIMEOUT rather than looping back on a wakeup that was
         * never a real frame.
         */
        Status recvFrame(uint32_t u32TimeoutMs, VectorRxFrame& out, std::stop_token stop_tok = {}) const;

        /** Transmit one CAN or CAN-FD frame with the given payload slice (classic: <=8 bytes, FD: <=64 bytes). */
        Status sendFrame(uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const;

        // ------------------------------------------------------------------ //
        //  Read-mode implementations (identical structure to PCAN/KVCAN)       //
        // ------------------------------------------------------------------ //

        Status readExact(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                         size_t& szBytesRead, uint32_t u32RxFilterId,
                         std::stop_token stop_tok = {}) const;

        Status readUntilDelimiter(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                                  uint8_t cDelimiter, size_t& szBytesRead,
                                  uint32_t u32RxFilterId,
                                  std::stop_token stop_tok = {}) const;

        Status readUntilToken(uint32_t u32TimeoutMs,
                              std::span<const uint8_t> token,
                              uint32_t u32RxFilterId,
                              std::stop_token stop_tok = {}) const;

        static void buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps);

        // ------------------------------------------------------------------ //
        //  Transport-protocol dispatch internals (see uPcan.hpp for rationale) //
        // ------------------------------------------------------------------ //

        WriteResult writeFragmented_locked(uint32_t                 u32WriteTimeout,
                                           std::span<const uint8_t> buffer,
                                           std::string_view         xtra_params) const;

        ReadResult readDispatch_locked(uint32_t           u32ReadTimeout,
                                       std::span<uint8_t> buffer,
                                       const ReadOptions& options,
                                       std::string_view   xtra_params,
                                       std::stop_token    stop_tok = {}) const;

        ReadResult readOneFrame_locked(uint32_t           u32TimeoutMs,
                                       std::span<uint8_t> buffer,
                                       std::string_view   xtra_params) const;

        /**
         * Minimal ICommDriver adapter exposing writeFragmented_locked()/
         * readOneFrame_locked() to the can_tp library — see PCAN::RawIo for
         * the full rationale, which applies here unchanged.
         *
         * stop_tok is accepted (required to satisfy the ICommDriver override)
         * but not forwarded any further: ITransportProtocol::send()/receive()
         * don't carry one down to these callbacks — same known gap
         * PCAN::readOneFrame_locked() documents. A segmented Vector transfer
         * (ISO-TP/J1939 with setTpProtocol()) is therefore not yet
         * cancellable via the STOP button; only the non-TP read/write path
         * (the common case) is.
         */
        class RawIo final : public ICommDriver
        {
        public:
            explicit RawIo(const Vector& owner) : m_owner(owner) {}

            bool is_open() const override { return m_owner.m_bOpen; }

            CommDetails describeConnection(std::string_view xtra_params = {}) const override
            {
                return m_owner.describeConnection(xtra_params);
            }

            WriteResult tout_write(uint32_t u32Timeout, std::span<const uint8_t> buffer,
                                   std::string_view xtra_params = {},
                                   std::stop_token /*stop_tok*/ = {}) const override
            {
                return m_owner.writeFragmented_locked(u32Timeout, buffer, xtra_params);
            }

            ReadResult tout_read(uint32_t u32Timeout, std::span<uint8_t> buffer,
                                 const ReadOptions& /*options*/, std::string_view xtra_params = {},
                                 std::stop_token /*stop_tok*/ = {}) const override
            {
                return m_owner.readOneFrame_locked(u32Timeout, buffer, xtra_params);
            }

        private:
            const Vector& m_owner;
        };

        RawIo m_rawIo{*this};
};


#endif // U_VECTOR_DRIVER_H
