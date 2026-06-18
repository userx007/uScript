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
 */

#include "uSlcan.hpp"
#include "ICommDriver.hpp"

#include <span>
#include <string_view>
#include <cstdint>
#include <algorithm>

class SLCANFrameDriver : public ICommDriver
{
public:

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
     * \brief Build a CanFrame from the configured TX id and the payload bytes,
     *        then delegate to SLCAN::send_frame(frame, u32Timeout).
     *
     *        u32Timeout is forwarded verbatim: send_frame() uses it for both
     *        the UART write and the subsequent CR/BEL ACK read.
     *
     *        Must be const — CommScriptCommandInterpreter holds
     *        shared_ptr<const TDriver>; m_slcan is mutable to allow this.
     */
    WriteResult tout_write(uint32_t                 u32Timeout,
                           std::span<const uint8_t> dataSpan,
                           std::string_view         /*xtra_params*/ = {}) const override
    {
        static constexpr uint32_t CAN_EFF_FLAG    = 0x80000000U;
        static constexpr uint32_t CAN_EFF_MASK    = 0x1FFFFFFFU;
        static constexpr uint32_t CAN_SFF_MASK    = 0x000007FFU;
        static constexpr size_t   CLASSIC_MAX_LEN = 8U;
        static constexpr size_t   FD_MAX_LEN      = 64U;

        WriteResult res{};   // default-initialised: status is non-SUCCESS

        if (dataSpan.size() > FD_MAX_LEN) {
            return res;
        }

        CanFrame frame{};
        frame.is_extended = (m_u32TxId & CAN_EFF_FLAG) != 0U;
        frame.is_remote   = false;
        frame.is_canfd    = dataSpan.size() > CLASSIC_MAX_LEN;
        frame.brs         = frame.is_canfd && m_bFdBrs;
        frame.id          = m_u32TxId & (frame.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
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
     * \brief Delegate to SLCAN::receive_frame(frame, u32Timeout) and copy the
     *        decoded payload into the caller's buffer.
     *
     *        u32Timeout is forwarded verbatim: receive_frame() uses it for the
     *        UART readline that reads the incoming ASCII frame.
     *
     *        ReadOptions::mode is ignored — each SLCAN line is self-delimited
     *        by the adapter's CR terminator, so receive_frame() always returns
     *        exactly one complete decoded frame regardless of mode.
     */
    ReadResult tout_read(uint32_t           u32Timeout,
                         std::span<uint8_t> dataSpan,
                         const ReadOptions& /*options*/,
                         std::string_view   /*xtra_params*/ = {}) const override
    {
        ReadResult res{};   // default-initialised: status is non-SUCCESS

        CanFrame frame{};
        const auto status = m_slcan.receive_frame(frame, u32Timeout);

        if (ICommDriver::Status::SUCCESS != status) {
            return res;
        }

        const size_t szCopyLen = std::min(static_cast<size_t>(frame.len), dataSpan.size());
        std::copy(frame.data.begin(), frame.data.begin() + szCopyLen, dataSpan.begin());

        res.bytes_read       = szCopyLen;
        res.status           = ICommDriver::Status::SUCCESS;
        res.found_terminator = true;   // one complete frame received

        return res;
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
