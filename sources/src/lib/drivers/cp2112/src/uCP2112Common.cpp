#include "uCP2112.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>

#define LT_HDR  "CP2112_DRV  |"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

CP2112::Status CP2112::open(uint8_t u8I2CAddress, uint32_t u32ClockHz, uint8_t u8DeviceIndex)
{
    if (u32ClockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock speed"));
        return Status::INVALID_PARAM;
    }

    Status s = open_device(u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    m_u8I2CAddress = u8I2CAddress;

    s = configure_smbus(u32ClockHz);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure SMBus clock"); LOG_UINT32(u32ClockHz));
        CP2112Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CP2112 I2C opened: index="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("I2C addr=0x"); LOG_HEX8(u8I2CAddress);
              LOG_STRING("clock="); LOG_UINT32(u32ClockHz));

    return Status::SUCCESS;
}


CP2112::Status CP2112::close()
{
    if (is_open()) {
        (void)cancel_transfer(); // abort any in-flight I2C transaction
    }
    return CP2112Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
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
        case ReadMode::Exact:
        {
            size_t bytesRead = 0;
            result.status           = i2c_read(buffer, bytesRead, timeout);
            result.bytes_read       = bytesRead;
            result.found_terminator = false;
            break;
        }

        // ------------------------------------------------------------------
        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Buffer too small for delimiter + null terminator"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

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

            size_t matched = 0;
            result.status  = Status::READ_TIMEOUT;

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

            result.bytes_read = 0;
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

    uint32_t timeout    = (u32WriteTimeout == 0) ? CP2112_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;
    size_t   bytesWritten = 0;

    result.status        = i2c_write(buffer, timeout, bytesWritten);
    result.bytes_written = bytesWritten;

    return result;
}


// ============================================================================
// PRIVATE I²C PROTOCOL IMPLEMENTATION
// ============================================================================

CP2112::Status CP2112::configure_smbus(uint32_t u32ClockHz) const
{
    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0]  = RPT_SMBUS_CONFIG;
    report[1]  = static_cast<uint8_t>((u32ClockHz >> 24) & 0xFF);
    report[2]  = static_cast<uint8_t>((u32ClockHz >> 16) & 0xFF);
    report[3]  = static_cast<uint8_t>((u32ClockHz >>  8) & 0xFF);
    report[4]  = static_cast<uint8_t>( u32ClockHz        & 0xFF);
    report[5]  = 0x00; // device address (not used in master mode)
    report[6]  = 0x00; // auto send read: disabled
    report[7]  = 0x00; // write timeout high byte
    report[8]  = 0x00; // write timeout low  byte
    report[9]  = 0x00; // read  timeout high byte
    report[10] = 0x00; // read  timeout low  byte
    report[11] = 0x00; // SCL low timeout: disabled
    report[12] = 0x00; // retry count high byte
    report[13] = 0x03; // retry count low  byte (3 retries)

    return hid_set_feature(report, HID_REPORT_SIZE);
}


CP2112::Status CP2112::i2c_write(std::span<const uint8_t> data,
                                 uint32_t timeoutMs,
                                 size_t& bytesWritten) const
{
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: empty buffer"));
        return Status::INVALID_PARAM;
    }

    bytesWritten = 0;

    while (bytesWritten < data.size()) {
        size_t chunkSize = std::min(data.size() - bytesWritten, MAX_I2C_WRITE_PAYLOAD);
        auto   chunk     = data.subspan(bytesWritten, chunkSize);

        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("i2c_write: chunk offset="); LOG_UINT32(bytesWritten);
                  LOG_STRING("size="); LOG_UINT32(chunkSize));

        Status s = i2c_write_chunk(chunk, timeoutMs);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_write: chunk failed at offset"); LOG_UINT32(bytesWritten));
            return s;
        }

        bytesWritten += chunkSize;
    }

    return Status::SUCCESS;
}


CP2112::Status CP2112::i2c_write_chunk(std::span<const uint8_t> chunk,
                                       uint32_t timeoutMs) const
{
    if (chunk.empty() || chunk.size() > MAX_I2C_WRITE_PAYLOAD) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_write_chunk: invalid size:"); LOG_UINT32(chunk.size()));
        return Status::INVALID_PARAM;
    }

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_DATA_WRITE;
    report[1] = static_cast<uint8_t>(m_u8I2CAddress << 1); // 8-bit write address
    report[2] = static_cast<uint8_t>(chunk.size());
    std::copy(chunk.begin(), chunk.end(), report + 3);

    Status s = hid_interrupt_write(report, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_chunk: interrupt write failed"));
        return s;
    }

    s = poll_transfer_done(timeoutMs);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_chunk: transfer did not complete"));
        (void)cancel_transfer();
    }

    return s;
}


CP2112::Status CP2112::i2c_read(std::span<uint8_t> data,
                                size_t& bytesRead,
                                uint32_t timeoutMs) const
{
    bytesRead = 0;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: empty buffer"));
        return Status::INVALID_PARAM;
    }
    if (data.size() > MAX_I2C_READ_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_read: request exceeds 512 bytes:"); LOG_UINT32(data.size()));
        return Status::INVALID_PARAM;
    }

    uint8_t request[HID_REPORT_SIZE] = {0};
    request[0] = RPT_DATA_READ_REQUEST;
    request[1] = static_cast<uint8_t>((m_u8I2CAddress << 1) | 0x01);
    request[2] = static_cast<uint8_t>((data.size() >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>( data.size()       & 0xFF);

    Status s = hid_set_feature(request, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: failed to send read request"));
        return s;
    }

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

        if (got == 0 || response[0] != RPT_DATA_READ_RESPONSE) continue;

        uint8_t pktStatus = response[1];
        uint8_t pktLen    = response[2];

        if (pktStatus == XFER_ERROR) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: device reported transfer error"));
            return Status::READ_ERROR;
        }

        if (pktLen > 0) {
            size_t toCopy = std::min(static_cast<size_t>(pktLen), data.size() - bytesRead);
            std::copy(response + 3, response + 3 + toCopy, data.data() + bytesRead);
            bytesRead += toCopy;
        }

        if (pktStatus != XFER_BUSY) break;
    }

    if (bytesRead == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: no data received"));
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}


CP2112::Status CP2112::poll_transfer_done(uint32_t timeoutMs) const
{
    uint8_t reqBuf[HID_REPORT_SIZE] = {0};
    uint8_t rspBuf[HID_REPORT_SIZE] = {0};
    uint32_t elapsed = 0;

    while (elapsed < timeoutMs) {
        reqBuf[0] = RPT_TRANSFER_STATUS_REQ;
        reqBuf[1] = 0x01;

        Status s = hid_set_feature(reqBuf, HID_REPORT_SIZE);
        if (s != Status::SUCCESS) return s;

        rspBuf[0] = RPT_TRANSFER_STATUS_RESP;
        s = hid_get_feature(rspBuf, HID_REPORT_SIZE);
        if (s != Status::SUCCESS) return s;

        switch (rspBuf[1]) {
            case XFER_COMPLETE:
            case XFER_IDLE:
                return Status::SUCCESS;

            case XFER_ERROR:
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("poll_transfer_done: error, detail:"); LOG_UINT32(rspBuf[2]));
                return Status::WRITE_ERROR;

            default:
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(STATUS_POLL_INTERVAL_MS));
        elapsed += STATUS_POLL_INTERVAL_MS;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("poll_transfer_done: timeout after"); LOG_UINT32(timeoutMs); LOG_STRING("ms"));
    return Status::WRITE_TIMEOUT;
}


CP2112::Status CP2112::cancel_transfer() const
{
    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_CANCEL_TRANSFER;
    report[1] = 0x01;
    return hid_set_feature(report, HID_REPORT_SIZE);
}
