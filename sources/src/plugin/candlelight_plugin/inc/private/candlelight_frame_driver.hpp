#ifndef CANDLELIGHT_FRAME_DRIVER_HPP
#define CANDLELIGHT_FRAME_DRIVER_HPP

/**
 * \file  candlelight_frame_driver.hpp
 * \brief Adapter that makes a Candlelight (gs_usb) channel look like a
 *        generic ICommDriver to CommScriptClient / CommScriptCommandInterpreter.
 *
 * Background
 * ----------
 * CommScriptCommandInterpreter<TDriver> holds shared_ptr<const TDriver> and
 * calls only three methods:
 *   - is_open()
 *   - tout_write(uint32_t timeout, span<const uint8_t>, string_view) const
 *   - tout_read (uint32_t timeout, span<uint8_t>, const ReadOptions&, string_view) const
 *
 * Candlelight::tout_write / tout_read are a raw bulk-transfer passthrough —
 * they do not build or parse a CanFrame. Moreover they are NOT virtual in
 * Candlelight itself (only in ICommDriver), so inheriting from Candlelight
 * and marking them 'override' fails at compile time.
 *
 * Solution: inherit from ICommDriver directly, hold Candlelight by
 * composition, implement the three ICommDriver pure virtuals by delegating
 * to the typed Candlelight::send_frame() / receive_frame() API — the exact
 * same shape SLCANFrameDriver/UCANFrameDriver use for their own underlying
 * drivers.
 *
 * Software acceptance filtering (the one real difference from UCAN)
 * --------------------------------------------------------------------
 * gs_usb has no on-device filtering request at all — see uCandlelight.hpp's
 * "No on-device acceptance filtering". Every frame the bus carries reaches
 * this driver's raw_tout_read(), so FILTER (see set_filters() below) and
 * the per-call xtra_params id override are BOTH applied here in software,
 * in addition to each other: xtra_params (when present) demands an exact
 * id match for this one call; m_filters (when non-empty and no xtra_params
 * override applies) demands the frame match at least one configured
 * (id, mask, is_extended) entry. Either way, unlike UCAN, there is no
 * "the adapter already dropped it before we ever saw it" failure mode —
 * every frame on the bus is available to filter against; it's purely a
 * question of how much host-side CPU repeatedly discarding unwanted
 * frames costs.
 *
 * Timeout handling
 * ----------------
 * send_frame(frame, timeout_ms) and receive_frame(frame, timeout_ms) both
 * propagate timeout_ms all the way down to the USB control/bulk transfer
 * calls, so the u32Timeout supplied by the interpreter on each call is
 * correctly honoured for both TX (including the TX-complete echo wait) and RX.
 *
 * Multi-frame transport protocols (can_tp)
 * -----------------------------------------
 * Same two-layer raw_tout_write()/raw_tout_read() vs. tout_write()/
 * tout_read() split as UCANFrameDriver — see that file's class comment for
 * the full rationale; it applies here unchanged.
 */

#include "uCandlelight.hpp"
#include "ICommDriver.hpp"
#include "uNumeric.hpp"
#include "uGuiNotify.hpp"

// Generic, driver-independent multi-frame transport library (see
// can_tp/README.md). Only depends on ICommDriver, so it's reused verbatim
// here — the same headers/objects already back the KVCAN/SLCAN/UCAN plugins.
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
#include <vector>

class CandlelightFrameDriver : public ICommDriver
{
public:

    /// SocketCAN canid_t convention, shared by the TX id and the xtra_params
    /// override parsing in both tout_write() and tout_read() — same
    /// convention gs_usb itself uses for struct gs_host_frame::can_id
    /// (see uCandlelight.hpp's GS_CAN_EFF_FLAG etc.).
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;

    /// One software acceptance-filter entry — see set_filters().
    struct FilterEntry {
        uint32_t id;
        uint32_t mask;
        bool     is_extended;
    };

    /**
     * \param u16VendorId      USB VID (see uCandlelight.hpp header comment)
     * \param u16ProductId     USB PID
     * \param u32DeviceIndex   Nth matching device to open (0 = first)
     * \param u32TxId          CAN TX frame ID (SocketCAN canid_t convention:
     *                         CAN_EFF_FLAG bit 31 set → 29-bit extended frame)
     * \param bFdBrs           BRS flag for outgoing CAN-FD frames
     * \param strIdentityLabel Display text for the GUI comm-dump panel (see
     *                         describeConnection())
     * \param strInstanceName  Runtime instance identity for the GUI comm-dump
     *                         panel's "Plugin" column (e.g. "CANDLELIGHT" or
     *                         "CANDLELIGHT:1" — see PluginDataSet::strInstanceName).
     *                         Falls back to plain "CANDLELIGHT" when empty.
     */
    CandlelightFrameDriver(uint16_t            u16VendorId,
                           uint16_t            u16ProductId,
                           uint32_t            u32DeviceIndex,
                           uint32_t            u32TxId,
                           bool                bFdBrs,
                           const std::string&  strIdentityLabel = {},
                           const std::string&  strInstanceName = {})
        : m_candle(u16VendorId, u16ProductId, u32DeviceIndex)
        , m_u32TxId(u32TxId)
        , m_bFdBrs(bFdBrs)
        , m_strIdentityLabel(strIdentityLabel)
        , m_strInstanceName(strInstanceName.empty() ? "CANDLELIGHT" : strInstanceName)
    {}

    // -------------------------------------------------------------------------
    // ICommDriver pure virtuals
    // -------------------------------------------------------------------------

    bool is_open() const override
    {
        return m_candle.is_open();
    }

private:

    /**
     * \brief Build a CanFrame from the effective TX id and the payload bytes,
     *        then delegate to Candlelight::send_frame(frame, u32Timeout).
     *
     *        This is the original single-frame framing. It is what
     *        tout_write() calls directly when TpProtocol::NONE, and what the
     *        RawIo adapter exposes to the transport-protocol library for its
     *        own SF/FF/CF/FC frames (see the class-level comment above).
     *
     *        u32Timeout is forwarded verbatim: send_frame() uses it for both
     *        the bulk-OUT write and the subsequent TX-complete echo wait.
     *
     *        xtra_params, when non-empty, is parsed as a CAN id (decimal or
     *        "0x"-prefixed hex, e.g. "0x1A0" or "416", CAN_EFF_FLAG bit 31 set
     *        → extended) and overrides the configured TX id for this single
     *        frame only — it is never persisted back to m_u32TxId. An empty
     *        (or unparsable) xtra_params falls back to m_u32TxId.
     *
     *        Must be const — CommScriptCommandInterpreter holds
     *        shared_ptr<const TDriver>; m_candle is mutable to allow this.
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

        const auto status = m_candle.send_frame(frame, u32Timeout);

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
     *        raw_tout_write() — one call, one physical frame.
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
     * \brief Delegate to Candlelight::receive_frame(frame, remaining_timeout),
     *        looping (within the overall u32Timeout budget) until a frame
     *        passing BOTH the software filter set and the per-call id check
     *        arrives, and copy its decoded payload into the caller's buffer.
     *
     *        This is the original single-frame framing. It is what
     *        tout_read() calls directly when TpProtocol::NONE, and what the
     *        RawIo adapter exposes to the transport-protocol library for its
     *        own SF/FF/CF/FC frames (see the class-level comment above).
     *
     *        xtra_params, when non-empty, is parsed the same way as in
     *        raw_tout_write() and, if it parses, is the ONLY criterion
     *        applied (an exact single-id match, same as UCAN/KVCAN) — it
     *        supersedes m_filters for this one call. An empty (or
     *        unparsable) xtra_params falls back to m_filters (see
     *        set_filters()); an empty filter set accepts every frame, same
     *        as before FILTER existed.
     *
     *        Unlike UCAN, this is a purely host-side filter over every frame
     *        that arrives from the bus — see uCandlelight.hpp's "No
     *        on-device acceptance filtering" and this file's class comment.
     *
     *        Ignores ReadOptions::mode — each gs_host_frame is self-
     *        delimited (fixed size for its negotiated FD/classic mode), so
     *        receive_frame() always returns exactly one complete decoded
     *        frame regardless of mode.
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

            {
                const auto i64ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - tStart).count();
                if (i64ElapsedMs >= static_cast<int64_t>(u32Timeout)) {
                    return res; // overall budget exhausted — status stays non-SUCCESS
                }
                u32Remaining = static_cast<uint32_t>(static_cast<int64_t>(u32Timeout) - i64ElapsedMs);
            }

            CanFrame frame{};
            const auto status = m_candle.receive_frame(frame, u32Remaining);

            if (ICommDriver::Status::SUCCESS != status) {
                return res; // timeout or read error
            }

            if (bWantId) {
                if (frame.is_extended != bWantExt || frame.id != u32WantId) {
                    continue; // not the frame we're waiting for — keep polling within budget
                }
            } else if (!matchesFilters(frame)) {
                continue; // FILTER is configured and this frame matches none of it
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
     *        raw_tout_read() — one call, one physical frame.
     *
     *        Any other TpProtocol: reassembles a (possibly multi-frame)
     *        message via the selected transport protocol. xtra_params, if
     *        supplied, overrides the message's expected rx id for this call
     *        only; otherwise the rx id from set_rx_id() is used (defaults to
     *        mirroring the tx id). Any Flow-Control / handshake frames sent
     *        back to the peer use the configured tx id.
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
    // Candlelight configuration forwarding
    // Called by CandlelightPlugin::m_OpenAndConfigure() before handing the
    // driver to CommScriptCommandInterpreter / CommScriptClient.
    // -------------------------------------------------------------------------

    ICommDriver::Status set_bitrate(uint32_t u32BitrateBps, double dSamplePoint, uint32_t u32Timeout)
    {
        return m_candle.set_bitrate(u32BitrateBps, dSamplePoint, u32Timeout);
    }

    ICommDriver::Status set_bittiming(uint32_t propSeg, uint32_t phaseSeg1, uint32_t phaseSeg2,
                                      uint32_t sjw, uint32_t brp, uint32_t u32Timeout)
    {
        return m_candle.set_bittiming(propSeg, phaseSeg1, phaseSeg2, sjw, brp, u32Timeout);
    }

    ICommDriver::Status set_fd_data_bitrate(uint32_t u32BitrateBps, double dSamplePoint, uint32_t u32Timeout)
    {
        return m_candle.set_fd_data_bitrate(u32BitrateBps, dSamplePoint, u32Timeout);
    }

    ICommDriver::Status set_fd_data_bittiming(uint32_t propSeg, uint32_t phaseSeg1, uint32_t phaseSeg2,
                                              uint32_t sjw, uint32_t brp, uint32_t u32Timeout)
    {
        return m_candle.set_data_bittiming(propSeg, phaseSeg1, phaseSeg2, sjw, brp, u32Timeout);
    }

    /// @param u32ModeFlags  GS_CAN_MODE_* bitmask (see uCandlelight.hpp) —
    ///                      listen-only, loopback, triple-sample, one-shot,
    ///                      FD, pad-to-max, berr-reporting.
    ICommDriver::Status open_channel(uint32_t u32ModeFlags, uint32_t u32Timeout)
    {
        return m_candle.open_channel(u32ModeFlags, u32Timeout);
    }

    /**
     * \brief Install the software acceptance-filter set applied by
     *        raw_tout_read() when no per-call xtra_params id override is in
     *        effect — see this file's class comment. An empty vector (the
     *        default) accepts every frame.
     */
    void set_filters(std::vector<FilterEntry> filters)
    {
        m_filters = std::move(filters);
    }

    bool is_fd_supported() const { return m_candle.is_fd_supported(); }

    // -------------------------------------------------------------------------
    // Transport-protocol configuration
    // Called by CandlelightPlugin::m_OpenAndConfigure(), same as the
    // Candlelight configuration forwarding above — these only take effect
    // for calls made after they're set (no persistence across a fresh
    // CandlelightFrameDriver).
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
     *        resolveRxId() mirrors the tx id — the same "single id, both
     *        directions" default KVCAN/UCAN use.
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
     * including any per-call xtra_params override.
     */
    CommDetails describeConnection(std::string_view xtra_params = {}) const override
    {
        const uint32_t id  = resolveTxId(xtra_params);
        const bool     ext = (id & CAN_EFF_FLAG) != 0U;
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                      m_strIdentityLabel.empty() ? "CANDLELIGHT" : m_strIdentityLabel.c_str(),
                      id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK), ext ? " (ext)" : "");
        return commdump_details(CommFamily::CAN, label);
    }

private:

    /**
     * \brief true if @p frame matches at least one configured filter entry,
     *        or the filter set is empty (accept-all). Compares
     *        (frame.id & entry.mask) == (entry.id & entry.mask) within the
     *        matching standard/extended id space — same masked-match
     *        convention SocketCAN's CAN_RAW_FILTER uses, just evaluated
     *        entirely in software (see this file's class comment).
     */
    bool matchesFilters(const CanFrame& frame) const
    {
        if (m_filters.empty()) {
            return true;
        }
        for (const auto& f : m_filters) {
            if (f.is_extended != frame.is_extended) continue;
            const uint32_t mask = f.mask & (frame.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            if ((frame.id & mask) == (f.id & mask)) {
                return true;
            }
        }
        return false;
    }

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
     *        CandlelightFrameDriver — so it's cheap to keep as a permanent member.
     */
    class RawIo final : public ICommDriver
    {
    public:
        explicit RawIo(const CandlelightFrameDriver& owner) : m_owner(owner) {}

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
        const CandlelightFrameDriver& m_owner;
    };

    /**
     * \brief Report one physical CAN frame to the GUI comm-dump panel —
     *        called from raw_tout_write() (Tx) and raw_tout_read() (Rx), so
     *        every physical frame this driver puts on or takes off the wire
     *        gets its own accurate row. A no-op when gui_mode_active() is false.
     */
    void dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const
    {
        if (!gui_mode_active()) {
            return;
        }
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                      m_strIdentityLabel.empty() ? "CANDLELIGHT" : m_strIdentityLabel.c_str(),
                      u32Id, bExtended ? " (ext)" : "");
        gui_notify_comm_dump(m_strInstanceName, commdump_details(CommFamily::CAN, label),
                              dir, data.data(), static_cast<uint32_t>(data.size()));
    }

private:

    mutable Candlelight m_candle; ///< Underlying driver (mutable: send/receive_frame are non-const in Candlelight)
    uint32_t      m_u32TxId; ///< CAN TX frame ID (SocketCAN canid_t convention)
    bool          m_bFdBrs;  ///< BRS flag for outgoing CAN-FD frames
    std::string   m_strIdentityLabel;  ///< GUI comm-dump display label, see describeConnection()
    std::string   m_strInstanceName;   ///< GUI comm-dump "Plugin" column identity, see dumpFrame()

    std::vector<FilterEntry> m_filters; ///< software acceptance filter set, see set_filters()/matchesFilters()

    TpProtocol m_eTpProtocol = TpProtocol::NONE; ///< see set_tp_protocol()
    TpConfig   m_sTpConfig;                      ///< see set_tp_config()
    bool       m_bRxIdSet = false;               ///< true once set_rx_id() has been called
    uint32_t   m_u32RxId  = 0U;                  ///< see set_rx_id() / resolveRxId()

    RawIo m_rawIo{*this}; ///< frame-level ICommDriver view used by the TP library; see RawIo above
};

#endif // CANDLELIGHT_FRAME_DRIVER_HPP
