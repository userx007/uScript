#ifndef SLCAN_FRAME_DRIVER_HPP
#define SLCAN_FRAME_DRIVER_HPP

/**
 * \file  slcan_frame_driver.hpp
 * \brief Adapter that makes an SLCAN channel look like a generic ICommDriver
 *        to CommScriptClient / CommScriptCommandInterpreter.
 *
 * Background
 * ----------
 * CommScriptCommandInterpreter<TDriver> holds shared_ptr<const TDriver> and
 * calls only three methods:
 *   - is_open()
 *   - tout_write(uint32_t timeout, span<const uint8_t>, string_view) const
 *   - tout_read (uint32_t timeout, span<uint8_t>, const ReadOptions&, string_view) const
 *
 * SLCAN::tout_write / tout_read are a raw ASCII passthrough — they do not
 * build or parse a CanFrame.  Moreover they are NOT virtual in SLCAN itself
 * (only in ICommDriver), so inheriting from SLCAN and marking them 'override'
 * fails at compile time.
 *
 * Solution: inherit from ICommDriver directly, hold SLCAN by composition,
 * implement the three ICommDriver pure virtuals by delegating to the typed
 * SLCAN::send_frame() / receive_frame() API.
 *
 * Timeout handling
 * ----------------
 * send_frame(frame, timeout_ms) and receive_frame(frame, timeout_ms) both
 * propagate timeout_ms all the way down to uart_write / uart_read_line /
 * m_uart->tout_read, so the u32Timeout supplied by the interpreter on each
 * call is correctly honoured for both TX (including the ACK wait) and RX.
 *
 * Per-call CAN id (xtra_params parameter)
 * ----------------------------------------
 * Both tout_read() and tout_write() accept an optional xtra_params string,
 * parsed as a CAN id (decimal or "0x"-prefixed hex, e.g. "0x1A0" or "416";
 * CAN_EFF_FLAG bit 31 set → extended) and used for that single call only:
 *   - tout_write(): overrides the configured TX id for this one frame.
 *   - tout_read():  loops (within the overall timeout budget), discarding
 *                   any frame that doesn't match, until one with this id
 *                   arrives or time runs out.
 * An empty (or unparsable) xtra_params falls back to the driver defaults,
 * exactly like KVCAN.
 *
 * Unlike KVCAN, this id check on the RX side is done entirely in software,
 * AFTER the adapter's own std/ext acceptance filter has already let the
 * frame through — the adapter's f/F filter commands can only be sent while
 * the CAN channel is closed (see uSlcan.cpp), so there is no way to
 * transiently install/restore a filter per call the way KVCAN::tout_read()
 * does on an already-open SocketCAN socket. Practically: an xtra_params id
 * that isn't already covered by whichever filter is active for this channel
 * (see SLCANPlugin::setCanTxId() / the FILTER command) will simply never be
 * seen — the adapter drops it before it reaches the UART — and tout_read()
 * will time out waiting for it, no matter how patient the software loop is.
 *
 * Multi-frame transport protocols (can_tp)
 * -----------------------------------------
 * CommScriptCommandInterpreter<TDriver> — the engine that actually executes
 * CMD/SCRIPT lines — only ever calls is_open()/tout_write()/tout_read() on
 * the driver (see the class comment above); there is no plugin-level hook
 * it goes through instead. That means the ONLY place a segmented transport
 * protocol (ISO-TP, J1939-21, ...) can be inserted so CMD/SCRIPT actually
 * benefit from it is inside tout_write()/tout_read() themselves.
 *
 * To do that without tout_write() recursively calling itself when the
 * transport protocol turns around and sends single ≤8-byte SF/FF/CF/FC
 * frames, this class keeps two layers:
 *   - raw_tout_write()/raw_tout_read(): the ORIGINAL single-frame framing
 *     (exactly what tout_write()/tout_read() used to do directly), exposed
 *     to the can_tp library through the private RawIo adapter below.
 *   - tout_write()/tout_read(): when TpProtocol::NONE (default), these
 *     forward straight to raw_tout_write()/raw_tout_read() — byte-for-byte
 *     today's behaviour. When a protocol is selected (see set_tp_protocol()),
 *     they instead build one via make_transport_protocol() and call its
 *     send()/receive() with RawIo as the frame-level ICommDriver, so the
 *     protocol's own SF/FF/CF/FC frames go through raw_tout_write()/
 *     raw_tout_read() — never back through the TP-aware entry points.
 */

#include "uSlcan.hpp"
#include "ICommDriver.hpp"
#include "uNumeric.hpp"
#include "uGuiNotify.hpp"

// Generic, driver-independent multi-frame transport library (see
// can_tp/README.md). Only depends on ICommDriver, so it's reused verbatim
// here — the same headers/objects already back the KVCAN plugin.
#include "ITransportProtocol.hpp"
#include "TpFactory.hpp"
#include "TpConfig.hpp"

#include <span>
#include <string_view>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <memory>

class SLCANFrameDriver : public ICommDriver
{
public:

    /// SocketCAN canid_t convention, shared by the TX id and the xtra_params
    /// override parsing in both tout_write() and tout_read().
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;

    /**
     * \param strDevice        Serial device path (e.g. "/dev/ttyACM0")
     * \param u32UartBaud       UART baud rate
     * \param u32TxId          CAN TX frame ID (SocketCAN canid_t convention:
     *                         CAN_EFF_FLAG bit 31 set → 29-bit extended frame)
     * \param bFdBrs           BRS flag for outgoing CAN-FD frames
     * \param strIdentityLabel Display text for the GUI comm-dump panel (see
     *                         describeConnection()), supplied separately from
     *                         strDevice — e.g. "/dev/ttyACM0" or a friendlier name.
     */
    SLCANFrameDriver(const std::string& strDevice,
                     uint32_t           u32UartBaud,
                     uint32_t           u32TxId,
                     bool               bFdBrs,
                     const std::string& strIdentityLabel = {})
        : m_slcan(strDevice, u32UartBaud)
        , m_u32TxId(u32TxId)
        , m_bFdBrs(bFdBrs)
        , m_strIdentityLabel(strIdentityLabel)
    {}

    // -------------------------------------------------------------------------
    // ICommDriver pure virtuals
    // -------------------------------------------------------------------------

    bool is_open() const override
    {
        return m_slcan.is_open();
    }

private:

    /**
     * \brief Build a CanFrame from the effective TX id and the payload bytes,
     *        then delegate to SLCAN::send_frame(frame, u32Timeout).
     *
     *        This is the original single-frame framing, unchanged from
     *        before can_tp existed. It is what tout_write() calls directly
     *        when TpProtocol::NONE, and what the RawIo adapter exposes to
     *        the transport-protocol library for its own SF/FF/CF/FC frames
     *        (see the class-level comment above).
     *
     *        u32Timeout is forwarded verbatim: send_frame() uses it for both
     *        the UART write and the subsequent CR/BEL ACK read.
     *
     *        xtra_params, when non-empty, is parsed as a CAN id (decimal or
     *        "0x"-prefixed hex, e.g. "0x1A0" or "416", CAN_EFF_FLAG bit 31 set
     *        → extended) and overrides the configured TX id for this single
     *        frame only — it is never persisted back to m_u32TxId.  An empty
     *        (or unparsable) xtra_params falls back to m_u32TxId.
     *
     *        \note Unlike KVCAN, this override never needs to touch an
     *        acceptance filter to be safely transmitted: the adapter places
     *        no restriction on which id we transmit with. Whether a REPLY on
     *        that id can be received back is a separate concern — see
     *        raw_tout_read() below.
     *
     *        Must be const — CommScriptCommandInterpreter holds
     *        shared_ptr<const TDriver>; m_slcan is mutable to allow this.
     */
    WriteResult raw_tout_write(uint32_t                 u32Timeout,
                               std::span<const uint8_t> dataSpan,
                               std::string_view         xtra_params = {}) const
    {
        static constexpr size_t CLASSIC_MAX_LEN = 8U;
        static constexpr size_t FD_MAX_LEN      = 64U;

        WriteResult res{};   // default-initialised: status is non-SUCCESS

        if (dataSpan.size() > FD_MAX_LEN) {
            return res;
        }

        uint32_t u32EffectiveTxId = resolveTxId(xtra_params);

        CanFrame frame{};
        frame.is_extended = (u32EffectiveTxId & CAN_EFF_FLAG) != 0U;
        frame.is_remote   = false;
        frame.is_canfd    = dataSpan.size() > CLASSIC_MAX_LEN;
        frame.brs         = frame.is_canfd && m_bFdBrs;
        frame.id          = u32EffectiveTxId & (frame.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.len         = static_cast<uint8_t>(dataSpan.size());
        std::copy(dataSpan.begin(), dataSpan.end(), frame.data.begin());

        const auto status = m_slcan.send_frame(frame, u32Timeout);

        if (ICommDriver::Status::SUCCESS == status) {
            res.bytes_written = dataSpan.size();
            res.status        = ICommDriver::Status::SUCCESS;
            dumpFrame(CommDir::Tx, frame.id, frame.is_extended, dataSpan);
        }

        return res;
    }

public:

    /**
     * \brief Transmit @p dataSpan over the CAN channel.
     *
     *        TpProtocol::NONE (default): forwards straight to
     *        raw_tout_write() — one call, one physical frame, exactly as
     *        before can_tp existed. buffer.size() must be <= 64 bytes.
     *
     *        Any other TpProtocol: segments @p dataSpan (if it doesn't fit
     *        one frame) via the selected transport protocol. xtra_params, if
     *        supplied, overrides the message's tx id (same convention as
     *        the NONE path); the rx id used for the peer's Flow-Control /
     *        handshake frames comes from set_rx_id() (defaults to mirroring
     *        the tx id — see resolveRxId()).
     */
    WriteResult tout_write(uint32_t                 u32Timeout,
                           std::span<const uint8_t> dataSpan,
                           std::string_view         xtra_params = {}) const override
    {
        if (TpProtocol::NONE == m_eTpProtocol) {
            return raw_tout_write(u32Timeout, dataSpan, xtra_params);
        }

        auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
        if (!upTp) {
            // Factory declined (e.g. not-yet-implemented protocol) — fall
            // back to raw framing rather than silently dropping the call.
            return raw_tout_write(u32Timeout, dataSpan, xtra_params);
        }

        char szTxId[16];
        std::snprintf(szTxId, sizeof(szTxId), "0x%X", resolveTxId(xtra_params));
        char szRxId[16];
        std::snprintf(szRxId, sizeof(szRxId), "0x%X", resolveRxId({}));

        return upTp->send(m_rawIo, u32Timeout, dataSpan, szTxId, szRxId);
    }

private:

    /**
     * \brief Delegate to SLCAN::receive_frame(frame, remaining_timeout), looping
     *        (within the overall u32Timeout budget) until a frame matching the
     *        requested id arrives, and copy its decoded payload into the
     *        caller's buffer.
     *
     *        This is the original single-frame framing, unchanged from
     *        before can_tp existed. It is what tout_read() calls directly
     *        when TpProtocol::NONE, and what the RawIo adapter exposes to
     *        the transport-protocol library for its own SF/FF/CF/FC frames
     *        (see the class-level comment above).
     *
     *        xtra_params, when non-empty, is parsed the same way as in
     *        raw_tout_write() and selects the expected reply id for this call
     *        only. Frames that don't match are discarded and the wait
     *        continues until a match arrives or the overall timeout expires.
     *        An empty (or unparsable) xtra_params disables id matching
     *        entirely and returns the first frame received, exactly as
     *        before.
     *
     *        \note Hardware limitation vs KVCAN: the adapter's f/F acceptance
     *        filters can only be changed while the CAN channel is closed (see
     *        set_std_filter()/set_ext_filter() and uSlcan.cpp), so there is no
     *        way to transiently (re)install a filter for this one call the
     *        way KVCAN::tout_read() does. The id check above is therefore
     *        done entirely in software AFTER the adapter's own filter has
     *        already let the frame through. If the requested id is not
     *        already covered by whichever std/ext filter is active for this
     *        channel (see SLCANPlugin::setCanTxId() / FILTER command), the
     *        adapter's firmware drops it before it ever reaches the UART, and
     *        no amount of waiting here will recover it — this call will
     *        simply time out. Configure filters broadly (or leave them
     *        unset) if a script needs to react to ids other than the current
     *        default.
     *
     *        Ignores ReadOptions::mode — each SLCAN line is self-delimited
     *        by the adapter's CR terminator, so receive_frame() always returns
     *        exactly one complete decoded frame regardless of mode.
     */
    ReadResult raw_tout_read(uint32_t           u32Timeout,
                             std::span<uint8_t> dataSpan,
                             std::string_view   xtra_params = {}) const
    {
        ReadResult res{};   // default-initialised: status is non-SUCCESS

        bool     bWantId  = false;
        bool     bWantExt = false;
        uint32_t u32WantId = 0;

        if (!xtra_params.empty())
        {
            uint32_t u32Parsed = 0;
            if (numeric::str2uint32(xtra_params, u32Parsed))
            {
                bWantId   = true;
                bWantExt  = (u32Parsed & CAN_EFF_FLAG) != 0U;
                u32WantId = u32Parsed & (bWantExt ? CAN_EFF_MASK : CAN_SFF_MASK);
            }
        }

        const auto tStart = std::chrono::steady_clock::now();

        while (true)
        {
            uint32_t u32Remaining = u32Timeout;

            if (bWantId)
            {
                const auto i64ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - tStart).count();
                if (i64ElapsedMs >= static_cast<int64_t>(u32Timeout)) {
                    return res; // overall budget exhausted — status stays non-SUCCESS
                }
                u32Remaining = static_cast<uint32_t>(static_cast<int64_t>(u32Timeout) - i64ElapsedMs);
            }

            CanFrame frame{};
            const auto status = m_slcan.receive_frame(frame, u32Remaining);

            if (ICommDriver::Status::SUCCESS != status) {
                return res; // timeout or read error
            }

            if (bWantId && (frame.is_extended != bWantExt || frame.id != u32WantId)) {
                continue; // not the frame we're waiting for — keep polling within budget
            }

            dumpFrame(CommDir::Rx, frame.id, frame.is_extended,
                      std::span<const uint8_t>(frame.data.data(), frame.len));

            const size_t szCopyLen = std::min(static_cast<size_t>(frame.len), dataSpan.size());
            std::copy(frame.data.begin(), frame.data.begin() + szCopyLen, dataSpan.begin());

            res.bytes_read       = szCopyLen;
            res.status           = ICommDriver::Status::SUCCESS;
            res.found_terminator = true;   // one complete frame received

            return res;
        }
    }

public:

    /**
     * \brief Receive into @p dataSpan.
     *
     *        TpProtocol::NONE (default): forwards straight to
     *        raw_tout_read() — one call, one physical frame, exactly as
     *        before can_tp existed.
     *
     *        Any other TpProtocol: reassembles a (possibly multi-frame)
     *        message via the selected transport protocol. xtra_params, if
     *        supplied, overrides the message's expected rx id for this call
     *        only; otherwise the rx id from set_rx_id() is used (defaults to
     *        mirroring the tx id). Any Flow-Control / handshake frames sent
     *        back to the peer use the configured tx id.
     *
     *        Same hardware caveat as raw_tout_read(): the rx id — whichever
     *        one ends up in effect — still needs to be covered by the
     *        adapter's active std/ext filter or the underlying frames never
     *        arrive in the first place.
     */
    ReadResult tout_read(uint32_t           u32Timeout,
                         std::span<uint8_t> dataSpan,
                         const ReadOptions& options,
                         std::string_view   xtra_params = {}) const override
    {
        if (TpProtocol::NONE == m_eTpProtocol) {
            return raw_tout_read(u32Timeout, dataSpan, xtra_params);
        }

        auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
        if (!upTp) {
            return raw_tout_read(u32Timeout, dataSpan, xtra_params);
        }

        (void)options; // segmented protocols always reassemble a full message

        char szRxId[16];
        std::snprintf(szRxId, sizeof(szRxId), "0x%X", resolveRxId(xtra_params));
        char szTxId[16];
        std::snprintf(szTxId, sizeof(szTxId), "0x%X", m_u32TxId);

        return upTp->receive(m_rawIo, u32Timeout, dataSpan, szRxId, szTxId);
    }

    // -------------------------------------------------------------------------
    // SLCAN configuration forwarding
    // Called by SLCANPlugin::m_OpenAndConfigure() before handing the driver
    // to CommScriptCommandInterpreter / CommScriptClient.
    // -------------------------------------------------------------------------

    ICommDriver::Status set_bitrate(CanBitrate bitrate, uint32_t u32Timeout)
    {
        return m_slcan.set_bitrate(bitrate, u32Timeout);
    }

    ICommDriver::Status set_fd_data_rate(CanFdDataRate rate, uint32_t u32Timeout)
    {
        return m_slcan.set_fd_data_rate(rate, u32Timeout);
    }

    ICommDriver::Status set_mode(CanMode mode, uint32_t u32Timeout)
    {
        return m_slcan.set_mode(mode, u32Timeout);
    }

    ICommDriver::Status set_auto_retx(CanAutoRetx retx, uint32_t u32Timeout)
    {
        return m_slcan.set_auto_retx(retx, u32Timeout);
    }

    ICommDriver::Status set_std_filter(uint16_t id, uint16_t mask, uint32_t u32Timeout)
    {
        return m_slcan.set_std_filter(id, mask, u32Timeout);
    }

    ICommDriver::Status set_ext_filter(uint32_t id, uint32_t mask, uint32_t u32Timeout)
    {
        return m_slcan.set_ext_filter(id, mask, u32Timeout);
    }

    ICommDriver::Status open_channel(uint32_t u32Timeout)
    {
        return m_slcan.open_channel(u32Timeout);
    }

    // -------------------------------------------------------------------------
    // Transport-protocol configuration
    // Called by SLCANPlugin::m_OpenAndConfigure(), same as the SLCAN
    // configuration forwarding above — these only take effect for calls made
    // after they're set (no persistence across a fresh SLCANFrameDriver).
    // -------------------------------------------------------------------------

    /**
     * \brief Select the multi-frame transport protocol tout_write()/tout_read()
     *        use for payloads that don't fit in a single frame.
     *        TpProtocol::NONE (default) preserves the original raw framing.
     */
    void set_tp_protocol(TpProtocol eProto)
    {
        m_eTpProtocol = eProto;
    }

    /** \brief Tuning parameters (block size, STmin, timeouts, ...) for set_tp_protocol(). */
    void set_tp_config(const TpConfig& cfg)
    {
        m_sTpConfig = cfg;
    }

    /**
     * \brief Set the id expected for incoming response/handshake frames when
     *        a segmented transport protocol is active. If never called,
     *        resolveRxId() mirrors the tx id (see setCanTxId()'s note on the
     *        plugin side) — the same "single id, both directions" default
     *        KVCAN uses.
     */
    void set_rx_id(uint32_t u32RxId)
    {
        m_u32RxId  = u32RxId;
        m_bRxIdSet = true;
    }

    /**
     * \brief Describe this connection for the GUI comm-dump panel.
     *
     * Reuses resolveTxId() — the exact same resolution tout_write() itself
     * applies — so the label always reflects the CAN ID actually used,
     * including any per-call xtra_params override. (The RX-side match in
     * tout_read() can differ per the software-filter caveat documented
     * above; this reflects the TX-id resolution, same as PCAN/KVCAN.)
     */
    CommDetails describeConnection(std::string_view xtra_params = {}) const override
    {
        const uint32_t id  = resolveTxId(xtra_params);
        const bool     ext = (id & CAN_EFF_FLAG) != 0U;
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                      m_strIdentityLabel.empty() ? "SLCAN" : m_strIdentityLabel.c_str(),
                      id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK), ext ? " (ext)" : "");
        return commdump_details(CommFamily::CAN, label);
    }

private:

    /**
     * \brief Resolve the effective TX CAN id for one exchange: xtra_params
     *        (decimal or "0x"-prefixed hex, CAN_EFF_FLAG bit 31 set →
     *        extended) when present and parseable, m_u32TxId otherwise.
     *        Shared by tout_write() and describeConnection() so the two
     *        can never disagree about which id a given call actually used.
     */
    uint32_t resolveTxId(std::string_view xtra_params) const
    {
        uint32_t u32EffectiveTxId = m_u32TxId;

        if (!xtra_params.empty())
        {
            uint32_t u32Override = 0;
            if (numeric::str2uint32(xtra_params, u32Override))
            {
                u32EffectiveTxId = u32Override;
            }
            // else: xtra_params wasn't a valid CAN id — silently fall back to
            // the configured default, same convention as KVCAN::tout_write().
        }

        return u32EffectiveTxId;
    }

    /**
     * \brief Resolve the effective RX CAN id used by the transport protocol
     *        to identify frames belonging to an incoming message: xtra_params
     *        when present and parseable, else m_u32RxId if set_rx_id() was
     *        called, else the tx id (mirrors setCanTxId()'s single-id default).
     */
    uint32_t resolveRxId(std::string_view xtra_params) const
    {
        if (!xtra_params.empty())
        {
            uint32_t u32Override = 0;
            if (numeric::str2uint32(xtra_params, u32Override))
            {
                return u32Override;
            }
        }

        return m_bRxIdSet ? m_u32RxId : m_u32TxId;
    }

    /**
     * \brief Minimal ICommDriver adapter exposing raw_tout_write()/
     *        raw_tout_read() to the can_tp library, so a transport protocol
     *        can drive individual physical frames without recursing back
     *        through the TP-aware tout_write()/tout_read() entry points
     *        (see the class-level comment for why this indirection exists).
     *        Never stores state of its own — just forwards to the owning
     *        SLCANFrameDriver — so it's cheap to keep as a permanent member.
     */
    class RawIo final : public ICommDriver
    {
    public:
        explicit RawIo(const SLCANFrameDriver& owner) : m_owner(owner) {}

        bool is_open() const override { return m_owner.is_open(); }

        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            return m_owner.describeConnection(xtra_params);
        }

        WriteResult tout_write(uint32_t u32Timeout, std::span<const uint8_t> dataSpan,
                               std::string_view xtra_params = {}) const override
        {
            return m_owner.raw_tout_write(u32Timeout, dataSpan, xtra_params);
        }

        ReadResult tout_read(uint32_t u32Timeout, std::span<uint8_t> dataSpan,
                             const ReadOptions& /*options*/, std::string_view xtra_params = {}) const override
        {
            return m_owner.raw_tout_read(u32Timeout, dataSpan, xtra_params);
        }

    private:
        const SLCANFrameDriver& m_owner;
    };

    /**
     * \brief Report one physical CAN frame to the GUI comm-dump panel —
     *        called from raw_tout_write() (Tx) and raw_tout_read() (Rx), so
     *        every physical frame this driver puts on or takes off the wire
     *        gets its own accurate row: the original single-frame path
     *        (TpProtocol::NONE) and a segmented transport's SF/FF/CF/FC
     *        frames (via RawIo, which both funnel through these same two
     *        functions) are covered by the exact same call site, with no
     *        special-casing needed here for which one is active. A no-op
     *        when gui_mode_active() is false.
     */
    void dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const
    {
        if (!gui_mode_active()) {
            return;
        }
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                      m_strIdentityLabel.empty() ? "SLCAN" : m_strIdentityLabel.c_str(),
                      u32Id, bExtended ? " (ext)" : "");
        gui_notify_comm_dump("SLCAN", commdump_details(CommFamily::CAN, label),
                              dir, data.data(), static_cast<uint32_t>(data.size()));
    }

private:

    mutable SLCAN m_slcan;   ///< Underlying driver (mutable: send/receive_frame are non-const in SLCAN)
    uint32_t      m_u32TxId; ///< CAN TX frame ID (SocketCAN canid_t convention)
    bool          m_bFdBrs;  ///< BRS flag for outgoing CAN-FD frames
    std::string   m_strIdentityLabel;  ///< GUI comm-dump display label, see describeConnection()

    TpProtocol m_eTpProtocol = TpProtocol::NONE; ///< see set_tp_protocol()
    TpConfig   m_sTpConfig;                      ///< see set_tp_config()
    bool       m_bRxIdSet = false;               ///< true once set_rx_id() has been called
    uint32_t   m_u32RxId  = 0U;                  ///< see set_rx_id() / resolveRxId()

    RawIo m_rawIo{*this}; ///< frame-level ICommDriver view used by the TP library; see RawIo above
};

#endif // SLCAN_FRAME_DRIVER_HPP
