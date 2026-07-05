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
 */

#include "uSlcan.hpp"
#include "ICommDriver.hpp"
#include "uNumeric.hpp"

#include <span>
#include <string_view>
#include <cstdint>
#include <algorithm>
#include <chrono>

class SLCANFrameDriver : public ICommDriver
{
public:

    /// SocketCAN canid_t convention, shared by the TX id and the xtra_params
    /// override parsing in both tout_write() and tout_read().
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;

    /**
     * \param strDevice   Serial device path (e.g. "/dev/ttyACM0")
     * \param u32UartBaud UART baud rate
     * \param u32TxId     CAN TX frame ID (SocketCAN canid_t convention:
     *                    CAN_EFF_FLAG bit 31 set → 29-bit extended frame)
     * \param bFdBrs      BRS flag for outgoing CAN-FD frames
     */
    SLCANFrameDriver(const std::string& strDevice,
                     uint32_t           u32UartBaud,
                     uint32_t           u32TxId,
                     bool               bFdBrs)
        : m_slcan(strDevice, u32UartBaud)
        , m_u32TxId(u32TxId)
        , m_bFdBrs(bFdBrs)
    {}

    // -------------------------------------------------------------------------
    // ICommDriver pure virtuals
    // -------------------------------------------------------------------------

    bool is_open() const override
    {
        return m_slcan.is_open();
    }

    /**
     * \brief Build a CanFrame from the effective TX id and the payload bytes,
     *        then delegate to SLCAN::send_frame(frame, u32Timeout).
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
     *        tout_read() below.
     *
     *        Must be const — CommScriptCommandInterpreter holds
     *        shared_ptr<const TDriver>; m_slcan is mutable to allow this.
     */
    WriteResult tout_write(uint32_t                 u32Timeout,
                           std::span<const uint8_t> dataSpan,
                           std::string_view         xtra_params = {}) const override
    {
        static constexpr size_t CLASSIC_MAX_LEN = 8U;
        static constexpr size_t FD_MAX_LEN      = 64U;

        WriteResult res{};   // default-initialised: status is non-SUCCESS

        if (dataSpan.size() > FD_MAX_LEN) {
            return res;
        }

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
        }

        return res;
    }

    /**
     * \brief Delegate to SLCAN::receive_frame(frame, remaining_timeout), looping
     *        (within the overall u32Timeout budget) until a frame matching the
     *        requested id arrives, and copy its decoded payload into the
     *        caller's buffer.
     *
     *        xtra_params, when non-empty, is parsed the same way as in
     *        tout_write() and selects the expected reply id for this call
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
     *        ReadOptions::mode is ignored — each SLCAN line is self-delimited
     *        by the adapter's CR terminator, so receive_frame() always returns
     *        exactly one complete decoded frame regardless of mode.
     */
    ReadResult tout_read(uint32_t           u32Timeout,
                         std::span<uint8_t> dataSpan,
                         const ReadOptions& /*options*/,
                         std::string_view   xtra_params = {}) const override
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

            const size_t szCopyLen = std::min(static_cast<size_t>(frame.len), dataSpan.size());
            std::copy(frame.data.begin(), frame.data.begin() + szCopyLen, dataSpan.begin());

            res.bytes_read       = szCopyLen;
            res.status           = ICommDriver::Status::SUCCESS;
            res.found_terminator = true;   // one complete frame received

            return res;
        }
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

private:

    mutable SLCAN m_slcan;   ///< Underlying driver (mutable: send/receive_frame are non-const in SLCAN)
    uint32_t      m_u32TxId; ///< CAN TX frame ID (SocketCAN canid_t convention)
    bool          m_bFdBrs;  ///< BRS flag for outgoing CAN-FD frames
};

#endif // SLCAN_FRAME_DRIVER_HPP
