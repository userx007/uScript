#include "uDigisparkI2C.hpp"
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

#define LT_HDR   "I2C_BRIDGE  |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

bool I2CBridge::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pDevice != nullptr;
}


// ----------------------------------------------------------------------------
// tout_read — ICommDriver base override
//
// Adapts ICommDriver::ReadOptions to I2CReadOptions.
//
// ReadMode → CMD mapping:
//   Exact          → CMD_READ        (plain slave read)
//   UntilToken     → CMD_WRITE_READ  (register / preamble read)
//   UntilDelimiter → CMD_SCAN        (bus scan, delimiter field unused)
//
// When called through the base interface (without I2CReadOptions):
//   slave_addr ← options.delimiter   (single-byte convenience field)
//   read_len   ← buffer.size()
//   token      ← options.token       (preamble bytes for CMD_WRITE_READ)
// ----------------------------------------------------------------------------
ICommDriver::ReadResult I2CBridge::tout_read(uint32_t                        u32ReadTimeout,
                                             std::span<uint8_t>              buffer,
                                             const ICommDriver::ReadOptions& options,
                                             std::string_view                /*xtra_params*/) const
{

    // Plain ReadOptions: synthesise I2CReadOptions from the available fields.
    I2CReadOptions i2cOpts;
    i2cOpts.mode       = options.mode;
    i2cOpts.delimiter  = options.delimiter;
    i2cOpts.token      = options.token;
    i2cOpts.use_buffer = options.use_buffer;
    i2cOpts.slave_addr = options.delimiter;   // delimiter repurposed as slave address
    i2cOpts.read_len   = buffer.size();

    // Populate preamble from token span for UntilToken mode.
    if (options.mode == ReadMode::UntilToken && !options.token.empty())
    {
        i2cOpts.token = options.token;
    }

    return tout_read(u32ReadTimeout, buffer, i2cOpts);
}


// ----------------------------------------------------------------------------
// tout_read — I2C-specific overload (non-virtual, preferred when concrete type known)
// ----------------------------------------------------------------------------
ICommDriver::ReadResult I2CBridge::tout_read(uint32_t              u32ReadTimeout,
                                             std::span<uint8_t>    buffer,
                                             const I2CReadOptions& options) const
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: null or empty buffer"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    // 0 == infinite timeout: forwarded as-is to hid_pkt_recv(), which maps
    // it to hidapi's native blocking-forever wait.
    uint32_t u32Timeout = u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
            result = priv_cmd_read(u32Timeout, buffer, options);
            break;

        case ReadMode::UntilToken:
            result = priv_cmd_write_read(u32Timeout, buffer, options);
            break;

        case ReadMode::UntilDelimiter:
            // Bus scan — delimiter field unused; apply scan-specific timeout
            // Bus scan: 0 would mean "wait forever for a NACK/ACK on every one
            // of 127 addresses", which is never useful for a scan — keep this
            // one bounded regardless of the caller's read timeout.
            result = priv_cmd_scan(
                (u32ReadTimeout == 0) ? I2C_SCAN_DEFAULT_TIMEOUT : u32ReadTimeout,
                buffer);
            break;

        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: unknown ReadMode"));
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


// ----------------------------------------------------------------------------
// tout_write — ICommDriver base override
//
// Buffer convention:  buffer[0]   = 7-bit slave address
//                     buffer[1..N] = data bytes (max I2C_MAX_WRITE_PAYLOAD)
// ----------------------------------------------------------------------------
ICommDriver::WriteResult I2CBridge::tout_write(uint32_t                 u32WriteTimeout,
                                               std::span<const uint8_t> buffer,
                                               std::string_view         /*xtra_params*/) const
{
    WriteResult result;

    // Expect at least [addr] + [1 byte of data]
    if (buffer.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tout_write: buffer must contain slave_addr + data (min 2 bytes)"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    const uint8_t u8SlaveAddr = buffer[0];
    const auto    data        = buffer.subspan(1);

    return tout_write(u32WriteTimeout, u8SlaveAddr, data);
}


// ----------------------------------------------------------------------------
// tout_write — ergonomic I2C overload with explicit slave address (non-virtual)
// ----------------------------------------------------------------------------
ICommDriver::WriteResult I2CBridge::tout_write(uint32_t                 u32WriteTimeout,
                                               uint8_t                  u8SlaveAddr,
                                               std::span<const uint8_t> buffer) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    WriteResult result;

    if (!m_pDevice)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: device not open"));
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty() || buffer.size() > I2C_MAX_WRITE_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tout_write: invalid buffer size"); LOG_UINT32(buffer.size()));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    // 0 == infinite timeout: forwarded as-is to hid_pkt_recv() (writes wait
    // for the firmware's ack packet, so they share the same primitive).
    uint32_t u32Timeout = u32WriteTimeout;

    result = priv_cmd_write(u32Timeout, u8SlaveAddr, buffer);
    return result;
}


I2CBridge::ScanResult I2CBridge::scan(uint32_t u32Timeout) const
{
    ScanResult scanResult;

    // Route through the ICommDriver interface using ReadMode::UntilDelimiter → CMD_SCAN
    std::vector<uint8_t> addrBuf(127, 0);

    I2CReadOptions opts;
    opts.mode = ReadMode::UntilDelimiter;

    ReadResult rr = tout_read(u32Timeout, std::span<uint8_t>(addrBuf), opts);

    scanResult.status = rr.status;

    if (rr.status == Status::SUCCESS)
    {
        scanResult.addresses.assign(addrBuf.begin(),
                                    addrBuf.begin() + static_cast<ptrdiff_t>(rr.bytes_read));
    }

    return scanResult;
}


// ============================================================================
// PRIVATE COMMAND IMPLEMENTATIONS  (called with m_mutex already held)
// ============================================================================

ICommDriver::ReadResult I2CBridge::priv_cmd_read(uint32_t u32Timeout, std::span<uint8_t> buffer,
                                                  const I2CReadOptions& opts) const
{
    ReadResult result;

    uint8_t u8ReadLen = static_cast<uint8_t>(
        std::min(opts.read_len, I2C_MAX_READ_PAYLOAD));

    if (u8ReadLen == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: read_len is 0"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (buffer.size() < u8ReadLen)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_read: buffer too small");
                  LOG_UINT32(buffer.size()); LOG_UINT32(u8ReadLen));
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    // Build request packet: [CMD_READ][addr][len][0 0 0 0 0]
    uint8_t txPkt[I2C_PKT_SIZE] = {};
    txPkt[0] = CMD_READ;
    txPkt[1] = opts.slave_addr;
    txPkt[2] = u8ReadLen;

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, I2C_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[I2C_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, I2C_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_read: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_READ)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_read: unexpected CMD in response"); LOG_HEX8(rxPkt[0]));
        result.status = Status::READ_ERROR;
        return result;
    }

    uint8_t n = rxPkt[1];  // bytes actually received by firmware
    for (uint8_t i = 0; i < n && i < I2C_MAX_READ_PAYLOAD; ++i)
        buffer[i] = rxPkt[2 + i];

    result.status           = Status::SUCCESS;
    result.bytes_read       = n;
    result.found_terminator = true;  // successful read → no NACK

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("priv_cmd_read: addr="); LOG_HEX8(opts.slave_addr);
              LOG_STRING("bytes="); LOG_UINT32(n));

    return result;
}


ICommDriver::ReadResult I2CBridge::priv_cmd_write_read(uint32_t u32Timeout, std::span<uint8_t> buffer,
                                                        const I2CReadOptions& opts) const
{
    ReadResult result;

    // Preamble bytes come from the token span (mapped from ICommDriver::ReadOptions::token)
    const size_t szTokenLen = opts.token.size();

    uint8_t u8WLen = static_cast<uint8_t>(
        std::min(szTokenLen, I2C_MAX_WRITE_READ_WLEN));
    uint8_t u8RLen = static_cast<uint8_t>(
        std::min(opts.read_len, I2C_MAX_WRITE_READ_RLEN));

    if (u8WLen == 0 || u8RLen == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write_read: wlen or rlen is 0"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (buffer.size() < u8RLen)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write_read: buffer too small"));
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    // Packet: [CMD_WRITE_READ][addr][wlen][rlen][d0..d3]
    uint8_t txPkt[I2C_PKT_SIZE] = {};
    txPkt[0] = CMD_WRITE_READ;
    txPkt[1] = opts.slave_addr;
    txPkt[2] = u8WLen;
    txPkt[3] = u8RLen;
    for (uint8_t i = 0; i < u8WLen; ++i)
        txPkt[4 + i] = opts.token[i];

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, I2C_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write_read: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[I2C_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, I2C_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write_read: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_WRITE_READ)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write_read: unexpected CMD"); LOG_HEX8(rxPkt[0]));
        result.status = Status::READ_ERROR;
        return result;
    }

    if (rxPkt[1] == FW_STATUS_NACK)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("priv_cmd_write_read: NACK from"); LOG_HEX8(opts.slave_addr));
        result.status           = Status::NACK;
        result.found_terminator = false;  // NACK → terminator not found
        return result;
    }

    if (rxPkt[1] != FW_STATUS_OK)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write_read: firmware error"); LOG_HEX8(rxPkt[1]));
        result.status = Status::READ_ERROR;
        return result;
    }

    uint8_t n = rxPkt[2];
    for (uint8_t i = 0; i < n && i < I2C_MAX_WRITE_READ_RLEN; ++i)
        buffer[i] = rxPkt[3 + i];

    result.status           = Status::SUCCESS;
    result.bytes_read       = n;
    result.found_terminator = true;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("priv_cmd_write_read: addr="); LOG_HEX8(opts.slave_addr);
              LOG_STRING("rx bytes="); LOG_UINT32(n));

    return result;
}


ICommDriver::ReadResult I2CBridge::priv_cmd_scan(uint32_t u32Timeout, std::span<uint8_t> buffer) const
{
    ReadResult result;

    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_scan: buffer too small"));
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    uint8_t txPkt[I2C_PKT_SIZE] = {};
    txPkt[0] = CMD_SCAN;

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, I2C_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_scan: send failed"));
        result.status = eSend;
        return result;
    }

    size_t  szFound = 0;
    bool    bDone   = false;

    // Firmware sends one packet per found address, then a sentinel [CMD_SCAN][0x00]
    while (!bDone)
    {
        uint8_t rxPkt[I2C_PKT_SIZE] = {};
        Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, I2C_PKT_SIZE), u32Timeout);

        if (eRecv == Status::READ_TIMEOUT)
        {
            // Treat as end-of-scan (firmware may have already sent sentinel)
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("priv_cmd_scan: timeout waiting for response"));
            bDone = true;
            continue;
        }

        if (eRecv != Status::SUCCESS)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_scan: recv error"));
            result.status = eRecv;
            return result;
        }

        if (rxPkt[0] != CMD_SCAN)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("priv_cmd_scan: unexpected CMD"); LOG_HEX8(rxPkt[0]));
            result.status = Status::READ_ERROR;
            return result;
        }

        uint8_t addr = rxPkt[1];

        if (addr == 0x00)
        {
            // Sentinel — scan complete
            bDone = true;
        }
        else
        {
            if (szFound < buffer.size())
                buffer[szFound] = addr;

            ++szFound;
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("priv_cmd_scan: found device at"); LOG_HEX8(addr));
        }
    }

    result.status           = Status::SUCCESS;
    result.bytes_read       = szFound;
    result.found_terminator = true;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("priv_cmd_scan: total devices found"); LOG_UINT32(szFound));

    return result;
}


ICommDriver::WriteResult I2CBridge::priv_cmd_write(uint32_t u32Timeout, uint8_t u8SlaveAddr,
                                                    std::span<const uint8_t> data) const
{
    WriteResult result;

    uint8_t u8Len = static_cast<uint8_t>(
        std::min(data.size(), I2C_MAX_WRITE_PAYLOAD));

    // Packet: [CMD_WRITE][addr][len][d0..d4]
    uint8_t txPkt[I2C_PKT_SIZE] = {};
    txPkt[0] = CMD_WRITE;
    txPkt[1] = u8SlaveAddr;
    txPkt[2] = u8Len;
    for (uint8_t i = 0; i < u8Len; ++i)
        txPkt[3 + i] = data[i];

    Status eSend = hid_pkt_send(std::span<const uint8_t>(txPkt, I2C_PKT_SIZE));
    if (eSend != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write: send failed"));
        result.status = eSend;
        return result;
    }

    uint8_t rxPkt[I2C_PKT_SIZE] = {};
    Status eRecv = hid_pkt_recv(std::span<uint8_t>(rxPkt, I2C_PKT_SIZE), u32Timeout);
    if (eRecv != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("priv_cmd_write: recv failed"));
        result.status = eRecv;
        return result;
    }

    if (rxPkt[0] != CMD_WRITE)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write: unexpected CMD"); LOG_HEX8(rxPkt[0]));
        result.status = Status::WRITE_ERROR;
        return result;
    }

    if (rxPkt[1] == FW_STATUS_NACK)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("priv_cmd_write: NACK from"); LOG_HEX8(u8SlaveAddr));
        result.status = Status::NACK;
        return result;
    }

    if (rxPkt[1] != FW_STATUS_OK)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("priv_cmd_write: firmware error"); LOG_HEX8(rxPkt[1]));
        result.status = Status::WRITE_ERROR;
        return result;
    }

    result.status        = Status::SUCCESS;
    result.bytes_written = u8Len;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("priv_cmd_write: addr="); LOG_HEX8(u8SlaveAddr);
              LOG_STRING("bytes="); LOG_UINT32(u8Len));

    return result;
}
