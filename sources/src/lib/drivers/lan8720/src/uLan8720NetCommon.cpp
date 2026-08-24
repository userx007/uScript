#include "uLan8720Net.hpp"
#include "uLogger.hpp"
#include <cstring>
#include <chrono>
#include <thread>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "LAN8720_NET  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

// ============================================================================
// PROTOCOL HELPERS
// ============================================================================

Lan8720Net::Status Lan8720Net::receive_packet(std::span<uint8_t> response_buffer, size_t max_len, size_t& bytes_read) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    uint8_t status_byte = 0;
    ssize_t n = ::recv(m_iSocketFd, &status_byte, 1, 0);
    if (n <= 0) {
        return Status::READ_ERROR;
    }

    uint8_t len_bytes[2] = {0};
    n = ::recv(m_iSocketFd, len_bytes, 2, MSG_WAITALL);
    if (n != 2) {
        return Status::READ_ERROR;
    }
    uint16_t payload_len = (len_bytes[0] << 8) | len_bytes[1];

    if (payload_len > max_len) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Payload too large"));
        return Status::BUFFER_OVERFLOW;
    }

    if (payload_len > 0) {
        n = ::recv(m_iSocketFd, response_buffer.data(), payload_len, MSG_WAITALL);
        if (n != static_cast<ssize_t>(payload_len)) {
            return Status::READ_ERROR;
        }
    }

    bytes_read = 1 + 2 + payload_len;
    return Status::SUCCESS;
}

Lan8720Net::Status Lan8720Net::send_command(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    uint8_t header[3];
    header[0] = cmd_id;
    header[1] = (payload_len >> 8) & 0xFF;
    header[2] = payload_len & 0xFF;

    size_t total_len = 3 + payload_len;
    size_t offset = 0;

    while (offset < total_len) {
        ssize_t n = ::send(m_iSocketFd,
                           ((const uint8_t*)header) + offset,
                           total_len - offset,
                           0);
        if (n < 0) {
            return Status::WRITE_ERROR;
        }
        offset += n;
    }

    if (payload_len > 0) {
        offset = 0;
        while (offset < payload_len) {
            ssize_t n = ::send(m_iSocketFd,
                               payload + offset,
                               payload_len - offset,
                               0);
            if (n < 0) {
                return Status::WRITE_ERROR;
            }
            offset += n;
        }
    }

    return Status::SUCCESS;
}

// ============================================================================
// UNIFIED INTERFACE
// ============================================================================

Lan8720Net::ReadResult Lan8720Net::tout_read(uint32_t u32ReadTimeout,
                                             std::span<uint8_t> buffer,
                                             const ReadOptions& options,
                                             std::string_view xtra_params) const
{
    ReadResult result;
    // 0 == infinite timeout: forwarded to the UntilDelimiter poll loop
    // below, which now waits indefinitely instead of substituting a
    // default. (ReadMode::Exact is a single receive attempt with no
    // retry loop, so it does not use this value either way.)
    const uint32_t timeout = u32ReadTimeout;

    switch (options.mode) {
        case ReadMode::Exact: {
            // Send RECV command
            uint8_t cmd_arg = 0;
            send_command(0x03, &cmd_arg, 1);

            // Receive response
            uint8_t pkt[LAN8720NET_MAX_BUF];
            size_t bytes_read = 0;

            Status status = receive_packet(pkt, sizeof(pkt), bytes_read);

            if (status != Status::SUCCESS) {
                result.status = status;
                return result;
            }

            uint16_t data_len = (pkt[1] << 8) | pkt[2];

            if (data_len == 0) {
                result.status = Status::READ_TIMEOUT;
                result.bytes_read = 0;
                return result;
            }

            if (data_len > buffer.size()) {
                result.status = Status::BUFFER_OVERFLOW;
                result.bytes_read = 0;
                return result;
            }

            std::memcpy(buffer.data(), pkt + 3, data_len);
            result.bytes_read = data_len;
            result.status = Status::SUCCESS;
            break;
        }

        case ReadMode::UntilDelimiter: {
            size_t offset = 0;
            bool found = false;
            // 0 == infinite timeout: keep polling for a chunk forever.
            const bool bInfinite = (timeout == 0);
            auto tStart = std::chrono::steady_clock::now();

            while (offset < buffer.size() - 1) {
                uint8_t cmd_payload = 0;
                send_command(0x03, &cmd_payload, 1);

                uint8_t pkt[1460];
                size_t br = 0;
                Status st = receive_packet(pkt, sizeof(pkt), br);

                if (st != Status::SUCCESS) {
                    if (!bInfinite) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - tStart).count();
                        if (elapsed >= timeout) {
                            result.status = Status::READ_TIMEOUT;
                            return result;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                uint16_t len = (pkt[1]<<8) | pkt[2];
                if (len > 0) {
                    for (size_t i=0; i<len && offset < buffer.size()-1; ++i) {
                        uint8_t b = pkt[3+i];
                        buffer[offset++] = b;
                        if (b == options.delimiter) {
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }

            result.status = found ? Status::SUCCESS : Status::READ_ERROR;
            result.bytes_read = offset;
            result.found_terminator = found;
            break;
        }

        case ReadMode::UntilToken: {
            result.status = Status::SUCCESS;
            result.bytes_read = 0;
            result.found_terminator = false;
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}

Lan8720Net::WriteResult Lan8720Net::tout_write(uint32_t u32WriteTimeout,
                                               std::span<const uint8_t> buffer,
                                               std::string_view xtra_params) const
{
    WriteResult result;

    Status send_status = send_command(0x02, buffer.data(), buffer.size());

    if (send_status != Status::SUCCESS) {
        result.status = send_status;
        result.bytes_written = 0;
        return result;
    }

    result.status = Status::SUCCESS;
    result.bytes_written = buffer.size();
    return result;
}
