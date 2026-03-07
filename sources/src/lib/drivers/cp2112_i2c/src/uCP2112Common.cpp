#include "uCP2112.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>

#define LT_HDR  "CP2112_DRV |"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// is_open
// ============================================================================

bool CP2112::is_open() const
{
#ifdef _WIN32
    if (m_hDevice == INVALID_HANDLE_VALUE) {
#else
    if (m_hDevice < 0) {
#endif
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

CP2112::ReadResult CP2112::tout_read(uint32_t u32ReadTimeout,
                                     std::span<uint8_t> buffer,
                                     const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout = (u32ReadTimeout == 0) ? CP2112_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        // ------------------------------------------------------------------
        // Exact: single I2C read for exactly buffer.size() bytes
        // ------------------------------------------------------------------
        case ReadMode::Exact:
        {
            size_t bytesRead = 0;
            result.status       = i2c_read(buffer, bytesRead, timeout);
            result.bytes_read   = bytesRead;
            result.found_terminator = false;
            break;
        }

        // ------------------------------------------------------------------
        // UntilDelimiter: repeated single-byte reads until delimiter found
        // ------------------------------------------------------------------
        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer too small for delimiter + null terminator"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            size_t pos = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte    = 0;
                size_t  got     = 0;
                Status  s       = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                if (byte == options.delimiter) {
                    buffer[pos]             = '\0';
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }

                buffer[pos++] = byte;
            }

            if (pos == buffer.size() - 1 && result.status == Status::READ_TIMEOUT) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
                result.status = Status::BUFFER_OVERFLOW;
            }

            result.bytes_read = pos;
            break;
        }

        // ------------------------------------------------------------------
        // UntilToken: KMP stream match over byte-by-byte I2C reads
        // ------------------------------------------------------------------
        case ReadMode::UntilToken:
        {
            if (options.token.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty token"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            const auto& token = options.token;

            // Build KMP failure table
            std::vector<int> lps(token.size(), 0);
            for (size_t i = 1, len = 0; i < token.size(); ) {
                if (token[i] == token[len]) {
                    lps[i++] = static_cast<int>(++len);
                } else if (len != 0) {
                    len = static_cast<size_t>(lps[len - 1]);
                } else {
                    lps[i++] = 0;
                }
            }

            // Stream-match
            size_t matched    = 0;
            result.status     = Status::READ_TIMEOUT;

            while (true) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                while (matched > 0 && byte != token[matched]) {
                    matched = static_cast<size_t>(lps[matched - 1]);
                }
                if (byte == token[matched]) {
                    ++matched;
                }
                if (matched == token.size()) {
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
            }

            result.bytes_read = 0; // token search does not fill the user buffer
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


CP2112::WriteResult CP2112::tout_write(uint32_t u32WriteTimeout,
                                       std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout = (u32WriteTimeout == 0) ? CP2112_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    size_t bytesWritten  = 0;
    result.status        = i2c_write(buffer, timeout, bytesWritten);
    result.bytes_written = bytesWritten;

    return result;
}


// ============================================================================
// CP2112 PROTOCOL IMPLEMENTATION
// ============================================================================

CP2112::Status CP2112::configure_smbus(uint32_t u32ClockHz) const
{
    // Report 0x06: SMBus Configuration (64-byte feature report)
    //   Byte  0     : Report ID = 0x06
    //   Bytes 1-4   : Clock speed in Hz, big-endian
    //   Byte  5     : Device address (unused in master mode)
    //   Byte  6     : Auto Send Read (0 = disabled)
    //   Bytes 7-8   : Write timeout in ms, big-endian (0 = use device default)
    //   Bytes 9-10  : Read  timeout in ms, big-endian (0 = use device default)
    //   Byte  11    : SCL Low Timeout (0 = disabled)
    //   Bytes 12-13 : Retry count, big-endian
    //   Bytes 14-63 : Reserved (zero)

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0]  = RPT_SMBUS_CONFIG;
    report[1]  = static_cast<uint8_t>((u32ClockHz >> 24) & 0xFF);
    report[2]  = static_cast<uint8_t>((u32ClockHz >> 16) & 0xFF);
    report[3]  = static_cast<uint8_t>((u32ClockHz >>  8) & 0xFF);
    report[4]  = static_cast<uint8_t>( u32ClockHz        & 0xFF);
    report[5]  = 0x00; // device address (not used)
    report[6]  = 0x00; // auto send read: disabled
    report[7]  = 0x00; // write timeout high
    report[8]  = 0x00; // write timeout low
    report[9]  = 0x00; // read timeout high
    report[10] = 0x00; // read timeout low
    report[11] = 0x00; // SCL low timeout: disabled
    report[12] = 0x00; // retry count high
    report[13] = 0x03; // retry count low (3 retries)

    return hid_set_feature(report, HID_REPORT_SIZE);
}


CP2112::Status CP2112::i2c_write(std::span<const uint8_t> data, uint32_t timeoutMs,
                                 size_t& bytesWritten) const
{
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: empty buffer"));
        return Status::INVALID_PARAM;
    }

    bytesWritten = 0;

    // Walk through the data in MAX_I2C_WRITE_PAYLOAD-sized chunks.
    // Each chunk is an independent I2C write transaction on the bus.
    while (bytesWritten < data.size()) {
        size_t chunkSize = std::min(data.size() - bytesWritten, MAX_I2C_WRITE_PAYLOAD);
        auto   chunk     = data.subspan(bytesWritten, chunkSize);

        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("i2c_write: chunk offset="); LOG_UINT32(bytesWritten);
                  LOG_STRING("size="); LOG_UINT32(chunkSize));

        Status s = i2c_write_chunk(chunk, timeoutMs);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_write: chunk failed at offset"); LOG_UINT32(bytesWritten);
                      LOG_STRING("written so far:"); LOG_UINT32(bytesWritten));
            return s;
        }

        bytesWritten += chunkSize;
    }

    return Status::SUCCESS;
}


CP2112::Status CP2112::i2c_write_chunk(std::span<const uint8_t> chunk, uint32_t timeoutMs) const
{
    // Caller guarantees chunk.size() <= MAX_I2C_WRITE_PAYLOAD
    if (chunk.empty() || chunk.size() > MAX_I2C_WRITE_PAYLOAD) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_write_chunk: invalid chunk size:"); LOG_UINT32(chunk.size()));
        return Status::INVALID_PARAM;
    }

    // Report 0x0D: Data Write (Interrupt OUT)
    //   Byte  0     : Report ID = 0x0D
    //   Byte  1     : I2C write address = (7-bit addr << 1), R/W bit = 0
    //   Byte  2     : Payload length (1–61)
    //   Bytes 3-63  : Data (padded with 0x00)

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_DATA_WRITE;
    report[1] = static_cast<uint8_t>(m_u8I2CAddress << 1); // 8-bit write address
    report[2] = static_cast<uint8_t>(chunk.size());
    std::copy(chunk.begin(), chunk.end(), report + 3);

    Status s = hid_interrupt_write(report, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_chunk: HID interrupt write failed"));
        return s;
    }

    // Wait for this chunk's I2C transaction to complete before sending the next
    s = poll_transfer_done(timeoutMs);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_chunk: transfer did not complete"));
        (void)cancel_transfer();
    }

    return s;
}


CP2112::Status CP2112::i2c_read(std::span<uint8_t> data, size_t& bytesRead, uint32_t timeoutMs) const
{
    bytesRead = 0;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: empty buffer"));
        return Status::INVALID_PARAM;
    }
    if (data.size() > MAX_I2C_READ_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_read: request exceeds max (512 bytes):"); LOG_UINT32(data.size()));
        return Status::INVALID_PARAM;
    }

    // Report 0x09: Data Read Request (Feature SET)
    //   Byte  0     : Report ID = 0x09
    //   Byte  1     : I2C read address = (7-bit addr << 1) | 1
    //   Bytes 2-3   : Read length, big-endian (1–512)
    //   Bytes 4-63  : Reserved (zero)

    uint8_t request[HID_REPORT_SIZE] = {0};
    request[0] = RPT_DATA_READ_REQUEST;
    request[1] = static_cast<uint8_t>((m_u8I2CAddress << 1) | 0x01); // 8-bit read address
    request[2] = static_cast<uint8_t>((data.size() >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>( data.size()       & 0xFF);

    Status s = hid_set_feature(request, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: failed to send read request"));
        return s;
    }

    // Collect Data Read Response packets (Report 0x0C, Interrupt IN)
    //   Byte 0  : Report ID = 0x0C
    //   Byte 1  : Transfer status (IDLE=0, BUSY=1, COMPLETE=2, ERROR=3)
    //   Byte 2  : Bytes in this packet (0–61)
    //   Bytes 3-63 : Data
    //
    // The CP2112 pushes these packets as data arrives from the I2C bus.
    // Continue reading until status != BUSY or we have all expected bytes.

    uint8_t response[HID_REPORT_SIZE] = {0};

    while (bytesRead < data.size()) {
        size_t got = 0;
        s = hid_interrupt_read(response, HID_REPORT_SIZE, timeoutMs, got);

        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_read: interrupt read failed after");
                      LOG_UINT32(bytesRead); LOG_STRING("bytes"));
            break;
        }

        if (got == 0 || response[0] != RPT_DATA_READ_RESPONSE) {
            // Not our report; skip
            continue;
        }

        uint8_t pktStatus = response[1];
        uint8_t pktLen    = response[2];

        if (pktStatus == XFER_ERROR) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_read: device reported transfer error"));
            return Status::READ_ERROR;
        }

        if (pktLen > 0) {
            size_t toCopy = std::min(static_cast<size_t>(pktLen), data.size() - bytesRead);
            std::copy(response + 3, response + 3 + toCopy, data.data() + bytesRead);
            bytesRead += toCopy;
        }

        // BUSY means more packets are coming; anything else means done
        if (pktStatus != XFER_BUSY) {
            break;
        }
    }

    if (bytesRead == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: no data received"));
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}


CP2112::Status CP2112::poll_transfer_done(uint32_t timeoutMs) const
{
    // Report 0x0E: Transfer Status Request (Feature SET)
    //   Byte 0 : Report ID = 0x0E
    //   Byte 1 : 0x01 (request trigger)
    //
    // Report 0x0F: Transfer Status Response (Feature GET)
    //   Byte 0 : Report ID = 0x0F
    //   Byte 1 : Status  (0=Idle, 1=Busy, 2=Complete, 3=Error)
    //   Byte 2 : Status detail / error code
    //   Bytes 3-4 : Retry count, big-endian
    //   Bytes 5-6 : Bytes read (big-endian; valid for read transactions)

    uint8_t reqBuf[HID_REPORT_SIZE] = {0};
    uint8_t rspBuf[HID_REPORT_SIZE] = {0};

    uint32_t elapsed = 0;

    while (elapsed < timeoutMs) {
        // Ask the CP2112 to update status
        reqBuf[0] = RPT_TRANSFER_STATUS_REQ;
        reqBuf[1] = 0x01;

        Status s = hid_set_feature(reqBuf, HID_REPORT_SIZE);
        if (s != Status::SUCCESS) return s;

        // Retrieve the status response
        rspBuf[0] = RPT_TRANSFER_STATUS_RESP;
        s = hid_get_feature(rspBuf, HID_REPORT_SIZE);
        if (s != Status::SUCCESS) return s;

        uint8_t xferStatus = rspBuf[1];

        switch (xferStatus) {
            case XFER_COMPLETE:
                return Status::SUCCESS;

            case XFER_IDLE:
                // Idle after a write usually means success
                return Status::SUCCESS;

            case XFER_ERROR:
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("poll_transfer_done: transfer error, detail:"); LOG_UINT32(rspBuf[2]));
                return Status::WRITE_ERROR;

            case XFER_BUSY:
            default:
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(STATUS_POLL_INTERVAL_MS));
        elapsed += STATUS_POLL_INTERVAL_MS;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll_transfer_done: timeout after"); LOG_UINT32(timeoutMs); LOG_STRING("ms"));
    return Status::WRITE_TIMEOUT;
}


CP2112::Status CP2112::cancel_transfer() const
{
    // Report 0x11: Cancel Transfer
    //   Byte 0 : Report ID = 0x11
    //   Byte 1 : 0x01 (trigger)

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_CANCEL_TRANSFER;
    report[1] = 0x01;
    return hid_set_feature(report, HID_REPORT_SIZE);
}
