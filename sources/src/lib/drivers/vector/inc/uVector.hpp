#ifndef U_VECTOR_DRIVER_H
#define U_VECTOR_DRIVER_H

#include "ICommDriver.hpp"
#include "ITransportProtocol.hpp"
#include "TpFactory.hpp"
#include "TpConfig.hpp"
#include "uGuiNotify.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stop_token>

#if !defined(_WIN32)
#  error "uVector: Vector Informatik's XL Driver Library (vxlapi) ships for Windows only. \
This driver cannot be built for this target platform."
#endif

#include <windows.h>

// XL Driver Library (XL-API) header — supplied by Vector Informatik alongside
// the "Vector Driver Setup" installer (normally under
// "Vector XL Driver Library\bin\vxlapi.h" once installed, or in the vendor's
// XL Driver Library SDK download).
//
// The copy under third_party/vxlapi/include/vxlapi.h in this repository is
// NOT Vector's own header: it is a minimal, independently-written
// compatibility declaration covering only the handful of XL-API entry
// points this driver calls. Vector's XL Driver Library is proprietary and
// its redistribution terms don't permit vendoring the real SDK the way the
// PCAN-Basic SDK is vendored for the PCAN driver, so it isn't bundled here.
// If you have the official SDK installed, prefer pointing
// VECTOR_XLAPI_INCLUDE_DIR / VECTOR_XLAPI_LIB_DIR (see this component's
// CMakeLists.txt) at it instead of using the bundled stub.
#include <vxlapi.h>


/**
 * @brief Vector XL-API driver wrapper implementing ICommDriver.
 *
 * Talks to Vector Informatik CAN interfaces (VN16xx, VN89xx, VX1xxx, ...)
 * through the XL Driver Library (vxlapi64.dll). Windows only — see the
 * platform guard above. Structurally this mirrors the uPcan driver almost
 * exactly (same ICommDriver framing, same TpFactory-based multi-frame
 * transport-protocol dispatch, same RawIo indirection) with the frame-level
 * primitives swapped for XL-API calls.
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
 * xlOpenDriver()/xlCloseDriver() are process-wide, not per-channel — calling
 * xlOpenDriver() a second time while a first port is still open is legal
 * (XL-API reference-counts it internally) but this class still keeps its own
 * process-wide refcount (s_iDriverRefCount) so the very first Vector
 * instance to open a port calls xlOpenDriver() and the last one to close
 * calls xlCloseDriver(), rather than relying on that undocumented internal
 * behaviour.
 *
 * Writing
 * -------
 *  - Bytes are packed into consecutive classic-CAN frames, up to 8 bytes each.
 *  - The CAN ID used for every outgoing frame is m_u32DefaultTxId unless the
 *    caller passes a non-empty xtra_params string (decimal or 0x-prefixed
 *    hex CAN ID, e.g. "0x18FF50E5" or "123") — same SocketCAN canid_t
 *    convention (bit 31 = extended-frame flag) the PCAN/KVCAN/SLCAN drivers
 *    use.
 *  - CAN FD is NOT implemented by this driver (the VN1610 and most of the
 *    VN16xx family this plugin targets are classic-CAN only). open() with
 *    bFD=true fails with Status::INVALID_PARAM rather than silently opening
 *    a classic-CAN channel with an FD flag nobody honours.
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
 * entry points or re-locks m_mutex.
 */
class Vector : public ICommDriver
{

    public:

        // ------------------------------------------------------------------ //
        //  Constants                                                           //
        // ------------------------------------------------------------------ //

        static constexpr size_t   VECTOR_MAX_PAYLOAD          = 8;     ///< Classic CAN max payload bytes per frame.
        static constexpr uint32_t VECTOR_READ_DEFAULT_TIMEOUT  = 5000; ///< Default RX timeout in milliseconds.
        static constexpr uint32_t VECTOR_WRITE_DEFAULT_TIMEOUT = 5000; ///< Default TX timeout in milliseconds.
        static constexpr uint32_t VECTOR_DEFAULT_TX_ID         = 0x7FF; ///< Default TX CAN ID.
        static constexpr uint32_t VECTOR_DEFAULT_RX_FILTER_ID  = 0x000; ///< 0 = accept all (open filter).
        static constexpr uint32_t VECTOR_RX_QUEUE_SIZE         = 256;   ///< xlOpenPort() RX event queue depth.

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
         * @param u32Bitrate       CAN bitrate in bps (e.g. 500000).
         * @param u32TxId          Default TX CAN ID.
         * @param bExtended        Force 29-bit extended frame format (auto-detected when false).
         * @param bFD              CAN FD mode — NOT supported; open() fails if true.
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
                        const std::string& strInstanceName   = {})
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "Vector" : strInstanceName)
        {
            open(strAppName, u32AppChannel, u32Bitrate, u32TxId, bExtended, bFD);
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
                        const std::string& strInstanceName   = {})
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "Vector" : strInstanceName)
        {
            openDirect(sel, u32Bitrate, u32TxId, bExtended, bFD);
        }

        virtual ~Vector()
        {
            close();
        }

        // ------------------------------------------------------------------ //
        //  Lifecycle                                                           //
        // ------------------------------------------------------------------ //

        /**
         * @brief Open and initialise a Vector CAN channel via XL-API.
         * @param strAppName    Application name as configured in "Vector Hardware Config".
         * @param u32AppChannel Zero-based index into that application's assigned channels.
         * @param u32Bitrate    CAN bitrate in bps.
         * @param u32TxId       Default TX CAN ID used when xtra_params is empty.
         * @param bExtended     Force 29-bit extended frame format.
         * @param bFD           CAN FD mode. Not implemented — fails with INVALID_PARAM if true.
         * @return Status::SUCCESS on success, appropriate error code otherwise.
         */
        Status open(const std::string& strAppName,
                    uint32_t           u32AppChannel = 0,
                    uint32_t           u32Bitrate     = 500000,
                    uint32_t           u32TxId        = VECTOR_DEFAULT_TX_ID,
                    bool               bExtended      = false,
                    bool               bFD            = false);

        /**
         * @brief Open and initialise a Vector CAN channel by resolving sel via
         *        matchChannels() directly — bypassing Vector Hardware Config's
         *        application-name/index indirection entirely.
         * @param sel        Selection criteria; must match exactly one CAN-capable channel.
         * @param u32Bitrate CAN bitrate in bps.
         * @param u32TxId    Default TX CAN ID used when xtra_params is empty.
         * @param bExtended  Force 29-bit extended frame format.
         * @param bFD        CAN FD mode. Not implemented — fails with INVALID_PARAM if true.
         * @return Status::SUCCESS on success. Status::INVALID_PARAM if sel matched zero or
         *         more than one channel (both cases are logged with the full candidate list
         *         so the caller can tighten sel), or if the single match isn't CAN-capable.
         */
        Status openDirect(const DeviceSelector& sel,
                          uint32_t           u32Bitrate = 500000,
                          uint32_t           u32TxId    = VECTOR_DEFAULT_TX_ID,
                          bool               bExtended  = false,
                          bool               bFD        = false);

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
            std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                          m_strIdentityLabel.empty() ? "Vector" : m_strIdentityLabel.c_str(),
                          id & CAN_EFF_MASK, ext ? " (ext)" : "");
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

        /** Query the resolved XL-API channel access mask (0 if not open). */
        XLaccess getAccessMask()        const        { return m_xlAccessMask; }

        // ------------------------------------------------------------------ //
        //  Transport-protocol configuration                                    //
        // ------------------------------------------------------------------ //

        void setTpProtocol(TpProtocol eProto)         { m_eTpProtocol = eProto; }
        void setTpConfig(const TpConfig& cfg)         { m_sTpConfig = cfg; }
        void setTpRxId(uint32_t u32Id)                { m_u32TpRxId = u32Id; m_bTpRxIdSet = true; }

    private:

        // ------------------------------------------------------------------ //
        //  Process-wide XL-API driver handle (see class comment)               //
        // ------------------------------------------------------------------ //

        static std::mutex s_driverMutex;
        static uint32_t   s_u32DriverRefCount;

        /** xlOpenDriver() if this is the first live instance; always increments the refcount. */
        static Status s_EnsureDriverOpen();
        /** Decrements the refcount; xlCloseDriver() if it reaches zero. */
        static void   s_ReleaseDriver();

        // ------------------------------------------------------------------ //
        //  State                                                               //
        // ------------------------------------------------------------------ //

        XLportHandle       m_xlPort              = XL_INVALID_PORTHANDLE; ///< XL-API port handle.
        XLaccess           m_xlAccessMask         = 0;                    ///< Resolved channel access mask.
        HANDLE             m_hRxEvent             = nullptr;              ///< Notification event for xlSetNotification().
        bool               m_bOpen               = false;
        bool               m_bExtendedId         = false;
        uint32_t           m_u32DefaultTxId      = VECTOR_DEFAULT_TX_ID;
        uint32_t           m_u32DefaultRxFilterId = VECTOR_DEFAULT_RX_FILTER_ID;
        mutable std::mutex m_mutex;
        std::string        m_strIdentityLabel;
        std::string        m_strInstanceName{"Vector"};

        TpProtocol m_eTpProtocol = TpProtocol::NONE;
        TpConfig   m_sTpConfig;
        bool       m_bTpRxIdSet  = false;
        uint32_t   m_u32TpRxId   = 0U;

        // ------------------------------------------------------------------ //
        //  Internal helpers                                                    //
        // ------------------------------------------------------------------ //

        /**
         * @brief Shared tail end of open()/openDirect(): given an already-resolved
         *        access mask, open the XL-API port, set the bitrate (if permitted),
         *        install the RX notification, and activate the channel.
         *
         * ASSUMES m_mutex IS ALREADY HELD and s_EnsureDriverOpen() has ALREADY been
         * called by the caller (which remains responsible for calling s_ReleaseDriver()
         * if this returns anything other than Status::SUCCESS).
         */
        Status m_OpenWithMask_locked(XLaccess accessMask,
                                     uint32_t u32Bitrate,
                                     uint32_t u32TxId,
                                     bool     bExtended);

        static bool parseUint32(std::string_view sv, uint32_t& out);

        uint32_t resolveTxId(std::string_view xtra_params) const;
        uint32_t resolveRxId(std::string_view xtra_params) const;
        uint32_t resolveTpRxId(std::string_view xtra_params) const;

        void dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const;

        /** Check whether a received XLcanRxEvent matches an RX filter id (SocketCAN canid_t convention). */
        bool frameMatchesFilter(const XLevent& evt, uint32_t u32RxFilterId) const;

        /**
         * @brief True if evt should NOT be treated as real received data.
         *
         * Covers two distinct cases XL-API folds into the same XL_RECEIVE_MSG
         * event stream:
         *   - error/overrun/line-error frames (ERROR_FRAME/OVERRUN/NERR) - not
         *     valid CAN payload.
         *   - XL_CAN_MSG_FLAG_TX_COMPLETED - XL-API echoes every frame THIS
         *     port transmits back through xlReceive() as a confirmation event.
         *     Skipping these is what stops tout_read() from reading back its
         *     own just-sent request instead of waiting for a peer's response.
         *     Confirmed by cross-referencing another open-source XL-API
         *     driver (ETAS/RBEI's BUSMASTER, CAN_Vector_XL.cpp) which
         *     explicitly branches on this same flag.
         */
        static bool m_ShouldSkipRxEvent(const XLevent& evt);

        /** Map an XLstatus return code to ICommDriver::Status. */
        static Status mapXlError(XLstatus sts);

        /**
         * @brief Receive one CAN frame within the given timeout, skipping
         *        non-data events (chip state, bus errors, ...).
         *
         * Blocks on m_hRxEvent (installed by open() via xlSetNotification())
         * for up to u32TimeoutMs milliseconds, then drains xlReceive() until a
         * XL_RECEIVE_MSG event is found or the queue is empty, in which case
         * it waits again for the remaining timeout budget.
         *
         * Cancellation: a std::stop_callback registered for the whole call
         * calls SetEvent(m_hRxEvent) the moment stop_tok is requested, waking
         * WaitForSingleObject() early exactly like a real notification would.
         * Unlike PCAN::recvFrame() (which creates a fresh, call-scoped event
         * every invocation), m_hRxEvent is long-lived and shared across every
         * call for this channel's lifetime — so a spurious wake from our own
         * SetEvent() is indistinguishable from a genuine one at the OS level;
         * stop_tok.stop_requested() is checked immediately after every wait
         * returns (and again at the top of the retry loop) to tell the two
         * apart and return Status::READ_TIMEOUT rather than looping back into
         * xlReceive() on a wakeup that was never a real frame.
         */
        Status recvFrame(uint32_t u32TimeoutMs, XLevent& evt, std::stop_token stop_tok = {}) const;

        /** Transmit one classic-CAN frame with the given payload slice. */
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
