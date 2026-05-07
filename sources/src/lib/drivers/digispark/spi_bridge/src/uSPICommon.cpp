#include "uDigisparkSPI.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "SPI_BRIDGE  |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

bool SPIBridge::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pDevice != nullptr;
}


SPIBridge::Status SPIBridge::configure(SPIMode eMode, SPIClockDiv eDiv)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_pDevice)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure: device not open"));
        return Status::PORT_ACCESS;
    }

    // Packet: [CMD_SPI_CONFIG][mode][divider][0 0 0 0 0]
    uint8_t txPkt[SPI_PKT_SIZE] = {};
    txPkt[0] = CMD_SPI_CONFIG;
    txPkt[1] = static_cast<uint8_t>(eMode);
    txPkt[2] = static_cast<uint8_t>(eDiv);

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, SPI_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure: send failed"));
        return eSend;
    }

    uint8_t rxPkt[SPI_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, SPI_PKT_SIZE),
                                 SPI_WRITE_DEFAULT_TIMEOUT);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure: recv failed"));
        return eRecv;
    }

    if (rxPkt[0] != CMD_SPI_CONFIG || rxPkt[1] != FW_STATUS_OK)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("configure: firmware error"); LOG_HEX8(rxPkt[1]));
        return Status::WRITE_ERROR;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("configure: mode="); LOG_UINT32(static_cast<uint8_t>(eMode));
              LOG_STRING("div="); LOG_UINT32(static_cast<uint8_t>(eDiv)));

    return Status::SUCCESS;
}


SPIBridge::ReadResult SPIBridge::tout_read(uint32_t              u32ReadTimeout,
                                            std::span<uint8_t>    buffer,
                                            const SPIReadOptions& options) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ReadResult result;

    if (!m_pDevice)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: device not open"));
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: empty buffer"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (options.length == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: length is 0"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    uint32_t u32Timeout = (u32ReadTimeout == 0) ? SPI_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case SPIReadMode::Transfer:
            result = priv_cmd_transfer(u32Timeout, buffer, options);
            break;

        case SPIReadMode::Read:
            result = priv_cmd_read(u32Timeout, buffer, options.length);
            break;

        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: unknown SPIReadMode"));
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


SPIBridge::WriteResult SPIBridge::tout_write(uint32_t                  u32WriteTimeout,
                                              std::span<const uint8_t>  buffer) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    WriteResult result;

    if (!m_pDevice)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: device not open"));
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty() || buffer.size() > SPI_MAX_WRITE_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tout_write: invalid buffer size"); LOG_UINT32(buffer.size()));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    uint32_t u32Timeout = (u32WriteTimeout == 0) ? SPI_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    result = priv_cmd_write(u32Timeout, buffer);
    return result;
}


// ── Convenience helpers ───────────────────────────────────────────────────────

SPIBridge::Status SPIBridge::transfer(uint32_t                 u32Timeout,
                                       std::span<const uint8_t> mosi,
                                       std::span<uint8_t>       miso) const
{
    if (mosi.empty() || mosi.size() > SPI_MAX_TRANSFER_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("transfer: invalid mosi size"); LOG_UINT32(mosi.size()));
        return Status::INVALID_PARAM;
    }

    if (miso.size() < mosi.size())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("transfer: miso buffer too small"));
        return Status::BUFFER_OVERFLOW;
    }

    SPIReadOptions opts;
    opts.mode      = SPIReadMode::Transfer;
    opts.length    = mosi.size();
    opts.mosi_data.assign(mosi.begin(), mosi.end());

    ReadResult rr = tout_read(u32Timeout, miso, opts);
    return rr.status;
}


SPIBridge::Status SPIBridge::write_reg(uint8_t u8Reg, uint8_t u8Value)
{
    const uint8_t buf[2] = { static_cast<uint8_t>(u8Reg & 0x7Fu), u8Value };
    WriteResult wr = tout_write(0, std::span<const uint8_t>(buf, 2));
    return wr.status;
}


SPIBridge::Status SPIBridge::read_reg(uint8_t u8Reg, std::span<uint8_t> buffer)
{
    if (buffer.empty() || buffer.size() > SPI_MAX_TRANSFER_PAYLOAD - 1)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read_reg: buffer size out of range"); LOG_UINT32(buffer.size()));
        return Status::INVALID_PARAM;
    }

    // Build MOSI frame: [reg | 0x80, 0x00 × len]
    std::vector<uint8_t> mosi(buffer.size() + 1, 0x00);
    mosi[0] = u8Reg | 0x80u;

    // Full MISO frame (discard byte 0 which corresponds to the address byte)
    std::vector<uint8_t> miso(mosi.size(), 0x00);

    Status eSt = transfer(0, std::span<const uint8_t>(mosi), std::span<uint8_t>(miso));
    if (eSt == Status::SUCCESS)
    {
        // Skip the dummy byte received while clocking the address out
        for (size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = miso[i + 1];
    }
    return eSt;
}


// ============================================================================
// PRIVATE COMMAND IMPLEMENTATIONS  (called with m_mutex already held)
// ============================================================================

SPIBridge::ReadResult SPIBridge::priv_cmd_transfer(uint32_t              u32Timeout,
                                                    std::span<uint8_t>    buffer,
                                                    const SPIReadOptions& opts) const
{
    ReadResult result;

    uint8_t u8Len = static_cast<uint8_t>(
        std::min(opts.mosi_data.size(), SPI_MAX_TRANSFER_PAYLOAD));

    if (u8Len == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_transfer: mosi_data empty"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (buffer.size() < u8Len)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_transfer: miso buffer too small");
                  LOG_UINT32(buffer.size()); LOG_UINT32(u8Len));
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    // Packet: [CMD_SPI_TRANSFER][len][d0..d5]
    uint8_t txPkt[SPI_PKT_SIZE] = {};
    txPkt[0] = CMD_SPI_TRANSFER;
    txPkt[1] = u8Len;
    for (uint8_t i = 0; i < u8Len; ++i)
        txPkt[2 + i] = opts.mosi_data[i];

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, SPI_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_transfer: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[SPI_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, SPI_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_transfer: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_SPI_TRANSFER)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_transfer: unexpected CMD"); LOG_HEX8(rxPkt[0]));
        result.status = Status::READ_ERROR;
        return result;
    }

    uint8_t n = rxPkt[1];
    for (uint8_t i = 0; i < n && i < SPI_MAX_TRANSFER_PAYLOAD; ++i)
        buffer[i] = rxPkt[2 + i];

    result.status     = Status::SUCCESS;
    result.bytes_read = n;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("priv_cmd_transfer: bytes="); LOG_UINT32(n));

    return result;
}


SPIBridge::ReadResult SPIBridge::priv_cmd_read(uint32_t           u32Timeout,
                                                std::span<uint8_t> buffer,
                                                size_t             szLen) const
{
    ReadResult result;

    uint8_t u8Len = static_cast<uint8_t>(
        std::min(szLen, SPI_MAX_READ_PAYLOAD));

    if (u8Len == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: length is 0"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (buffer.size() < u8Len)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: buffer too small"));
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    // Packet: [CMD_SPI_READ][len][0 0 0 0 0 0]
    uint8_t txPkt[SPI_PKT_SIZE] = {};
    txPkt[0] = CMD_SPI_READ;
    txPkt[1] = u8Len;

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, SPI_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[SPI_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, SPI_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_SPI_READ)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_read: unexpected CMD"); LOG_HEX8(rxPkt[0]));
        result.status = Status::READ_ERROR;
        return result;
    }

    uint8_t n = rxPkt[1];
    for (uint8_t i = 0; i < n && i < SPI_MAX_READ_PAYLOAD; ++i)
        buffer[i] = rxPkt[2 + i];

    result.status     = Status::SUCCESS;
    result.bytes_read = n;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("priv_cmd_read: bytes="); LOG_UINT32(n));

    return result;
}


SPIBridge::WriteResult SPIBridge::priv_cmd_write(uint32_t                 u32Timeout,
                                                   std::span<const uint8_t> data) const
{
    WriteResult result;

    uint8_t u8Len = static_cast<uint8_t>(
        std::min(data.size(), SPI_MAX_WRITE_PAYLOAD));

    // Packet: [CMD_SPI_WRITE][len][d0..d5]
    uint8_t txPkt[SPI_PKT_SIZE] = {};
    txPkt[0] = CMD_SPI_WRITE;
    txPkt[1] = u8Len;
    for (uint8_t i = 0; i < u8Len; ++i)
        txPkt[2 + i] = data[i];

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, SPI_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[SPI_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, SPI_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_SPI_WRITE || rxPkt[1] != FW_STATUS_OK)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write: firmware error"); LOG_HEX8(rxPkt[1]));
        result.status = Status::WRITE_ERROR;
        return result;
    }

    result.status        = Status::SUCCESS;
    result.bytes_written = u8Len;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("priv_cmd_write: bytes="); LOG_UINT32(u8Len));

    return result;
}
