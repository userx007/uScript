#ifndef SLCAN_FRAME_DRIVER_HPP
#define SLCAN_FRAME_DRIVER_HPP

/**
 * \file  slcan_frame_driver.hpp
 * \brief Thin adapter that makes an SLCAN channel look like a generic
 *        ICommDriver to CommScriptClient / CommScriptCommandInterpreter.
 *
 * Background
 * ----------
 * CommScriptCommandInterpreter<TDriver> only ever calls three methods on its
 * driver:
 *   - is_open()
 *   - tout_write(timeout, span<const uint8_t>, xtra_params)
 *   - tout_read (timeout, span<uint8_t>, ReadOptions, xtra_params)
 *
 * The raw SLCAN driver (uSlcan.hpp) exposes those methods as a verbatim byte
 * passthrough — it does not build or parse a CanFrame from them.  Its own
 * documentation explicitly recommends the typed send_frame()/receive_frame()
 * API instead.
 *
 * This adapter bridges the gap: it inherits from SLCAN (so is_open() and all
 * configuration methods are forwarded automatically) and overrides tout_write
 * / tout_read to call send_frame() / receive_frame() internally, building the
 * CanFrame from:
 *   - the TX id stored in the adapter (mirrors KVCANPlugin's set_tx_id / xtra_params usage)
 *   - the payload bytes supplied by the interpreter
 *
 * Usage
 * -----
 *   // In CMD handler:
 *   auto shpDriver = std::make_shared<SLCANFrameDriver>(
 *       m_strDevice, m_u32UartBaud, m_u32CanTxId, m_bFdBrs, m_u32WriteTimeout);
 *   // ... configure bitrate, mode, filters, open_channel on shpDriver ...
 *   CommScriptCommandInterpreter<SLCANFrameDriver> interpreter(
 *       shpDriver, m_u32CanReadBufferSize, m_u32ReadTimeout);
 *   interpreter.interpretCommand(command, m_bIsEnabled);
 *
 *   // In SCRIPT handler:
 *   CommScriptClient<SLCANFrameDriver> client(
 *       strScriptPathName, shpDriver,
 *       m_u32CanReadBufferSize, m_u32ReadTimeout, szDelay);
 *   client.execute(m_bIsEnabled);
 */

#include "uSlcan.hpp"
#include "ICommDriver.hpp"

#include <span>
#include <cstdint>
#include <algorithm>
#include <cstring>

class SLCANFrameDriver : public SLCAN
{
public:

    /**
     * \brief Construct and open the serial port (delegates to SLCAN ctor).
     *
     * \param strDevice      Serial device path (e.g. "/dev/ttyACM0")
     * \param u32UartBaud    UART baud rate
     * \param u32TxId        CAN TX frame ID (SocketCAN canid_t convention;
     *                       CAN_EFF_FLAG bit indicates extended frame)
     * \param bFdBrs         Bit Rate Switch flag for outgoing CAN-FD frames
     * \param u32WriteTout   Write timeout forwarded to send_frame()
     */
    SLCANFrameDriver(const std::string& strDevice,
                     uint32_t           u32UartBaud,
                     uint32_t           u32TxId,
                     bool               bFdBrs,
                     uint32_t           u32WriteTout)
        : SLCAN(strDevice, u32UartBaud)
        , m_u32TxId(u32TxId)
        , m_bFdBrs(bFdBrs)
        , m_u32WriteTout(u32WriteTout)
    {}

    /**
     * \brief Override tout_write: build a CanFrame from the TX id and payload
     *        bytes, then call SLCAN::send_frame().
     *
     * \param u32Timeout   Timeout in milliseconds (replaces the stored write
     *                     timeout so the interpreter's per-command value wins)
     * \param dataSpan     Raw payload bytes (max 8 classic / 64 FD)
     * \param xtra_params  Unused (accepted for interface compatibility)
     */
    WriteResult tout_write(uint32_t                      u32Timeout,
                           std::span<const uint8_t>      dataSpan,
                           const std::string&            /*xtra_params*/ = {}) override
    {
        static constexpr uint32_t CAN_EFF_FLAG    = 0x80000000U;
        static constexpr uint32_t CAN_EFF_MASK    = 0x1FFFFFFFU;
        static constexpr uint32_t CAN_SFF_MASK    = 0x000007FFU;
        static constexpr size_t   CLASSIC_MAX_LEN = 8U;
        static constexpr size_t   FD_MAX_LEN      = 64U;

        WriteResult res{};
        res.bytes_written = 0U;
        res.status        = ICommDriver::Status::ERROR;

        if (dataSpan.size() > FD_MAX_LEN) {
            return res; // caller will log the error
        }

        CanFrame frame{};
        frame.is_extended = (m_u32TxId & CAN_EFF_FLAG) != 0U;
        frame.is_remote   = false;
        frame.is_canfd    = dataSpan.size() > CLASSIC_MAX_LEN;
        frame.brs         = frame.is_canfd && m_bFdBrs;
        frame.id          = m_u32TxId & (frame.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.len         = static_cast<uint8_t>(dataSpan.size());
        std::copy(dataSpan.begin(), dataSpan.end(), frame.data.begin());

        const auto status = send_frame(frame, u32Timeout);

        if (ICommDriver::Status::SUCCESS == status) {
            res.bytes_written = dataSpan.size();
            res.status        = ICommDriver::Status::SUCCESS;
        }

        return res;
    }

    /**
     * \brief Override tout_read: call SLCAN::receive_frame() and copy the
     *        decoded payload into the caller's buffer.
     *
     * \param u32Timeout   Timeout in milliseconds
     * \param dataSpan     Output buffer (sized to m_u32CanReadBufferSize)
     * \param options      ReadOptions (mode is ignored — each SLCAN line is
     *                     self-delimited by the adapter's CR terminator, so
     *                     receive_frame() always returns exactly one decoded frame)
     * \param xtra_params  Unused (accepted for interface compatibility)
     */
    ReadResult tout_read(uint32_t                   u32Timeout,
                         std::span<uint8_t>         dataSpan,
                         const ReadOptions&         /*options*/,
                         const std::string&         /*xtra_params*/ = {}) override
    {
        ReadResult res{};
        res.bytes_read       = 0U;
        res.status           = ICommDriver::Status::ERROR;
        res.found_terminator = false;

        CanFrame frame{};
        const auto status = receive_frame(frame, u32Timeout);

        if (ICommDriver::Status::SUCCESS != status) {
            res.status = status;
            return res;
        }

        const size_t szCopyLen = std::min(static_cast<size_t>(frame.len), dataSpan.size());
        std::copy(frame.data.begin(), frame.data.begin() + szCopyLen, dataSpan.begin());

        res.bytes_read       = szCopyLen;
        res.status           = ICommDriver::Status::SUCCESS;
        res.found_terminator = true; // one complete frame was received

        return res;
    }

private:

    uint32_t m_u32TxId;      ///< CAN TX frame ID (SocketCAN canid_t convention)
    bool     m_bFdBrs;       ///< BRS flag for CAN-FD frames
    uint32_t m_u32WriteTout; ///< Default write timeout (ms)
};

#endif // SLCAN_FRAME_DRIVER_HPP
