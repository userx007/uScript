#include "uW5500Net.hpp"
#include "uLogger.hpp"
#include <cstring>
#include <chrono>
#include <thread> // For sleep

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "W5500_NET   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

// ============================================================================
// PROTOCOL HELPERS
// ============================================================================

/**
 * @brief Receive a standardized packet from the server.
 *
 * The server sends: [Status(1) | Length(2 Big Endian) | Payload(N)]
 */
W5500Net::Status W5500Net::receive_packet(std::span<uint8_t> response_buffer, size_t max_len, size_t& bytes_read) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Read Status Byte
    uint8_t status_byte = 0;
    ssize_t n = ::recv(m_iSocketFd, &status_byte, 1, 0);
    if (n <= 0) {
        return Status::READ_ERROR;
    }

    // 2. Read Length (2 bytes)
    uint8_t len_bytes[2] = {0};
    n = ::recv(m_iSocketFd, len_bytes, 2, MSG_WAITALL);
    if (n != 2) {
        return Status::READ_ERROR;
    }
    uint16_t payload_len = (len_bytes[0] << 8) | len_bytes[1];

    // Check bounds
    if (payload_len > max_len) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Payload too large ("); LOG_UINT32(payload_len); LOG_STRING(")"));
        return Status::BUFFER_OVERFLOW;
    }

    // 3. Read Payload
    if (payload_len > 0) {
        n = ::recv(m_iSocketFd, response_buffer.data(), payload_len, MSG_WAITALL);
        if (n != static_cast<ssize_t>(payload_len)) {
            return Status::READ_ERROR;
        }
    }

    bytes_read = 1 + 2 + payload_len; // Total bytes read from socket
    return Status::SUCCESS;
}

/**
 * @brief Send a command to the server.
 *
 * Desktop sends: [CmdID(1) | Length(2 Big Endian) | Payload(N)]
 */
W5500Net::Status W5500Net::send_command(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    uint8_t header[3];
    header[0] = cmd_id;
    header[1] = (payload_len >> 8) & 0xFF;
    header[2] = payload_len & 0xFF;

    size_t total_len = 3 + payload_len;
    size_t offset = 0;

    // Loop to handle partial sends
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

W5500Net::ReadResult W5500Net::tout_read(uint32_t u32ReadTimeout,
                                         std::span<uint8_t> buffer,
                                         const ReadOptions& options,
                                         std::string_view xtra_params) const
{
    ReadResult result;
    const uint32_t timeout = (u32ReadTimeout == 0) ? W5500NET_TIMEOUT_MS : u32ReadTimeout;

    // Note: xtra_params can be used to specify "Socket ID" if the server supports multiple sockets
    uint8_t socket_id = 0; // Default to socket 0
    if (!xtra_params.empty()) {
        socket_id = static_cast<uint8_t>(std::stoi(std::string(xtra_params)));
    }

    switch (options.mode) {
        case ReadMode::Exact: {
            // 1. Ask server how many bytes are available on this socket
            uint8_t cmd_payload = static_cast<uint8_t>(socket_id);
            send_command(0x04, &cmd_payload, 1);

            // 2. Read response: [Status(1) | Count(2) | No Payload]
            uint8_t resp_buf[3];
            size_t bytes_read = 0;
            Status status = receive_packet(resp_buf, sizeof(resp_buf), bytes_read);

            if (status != Status::SUCCESS) {
                result.status = status;
                return result;
            }

            uint16_t available_bytes = (resp_buf[1] << 8) | resp_buf[2];

            if (available_bytes == 0) {
                result.status = Status::READ_TIMEOUT; // No data available
                result.bytes_read = 0;
                return result;
            }

            // 3. Ask server to send data
            send_command(0x05, &cmd_payload, 1);

            // 4. Read response: [Status(1) | Length(2) | Data(N)]
            // Ensure buffer is large enough
            if (available_bytes > buffer.size()) {
                result.status = Status::BUFFER_OVERFLOW;
                result.bytes_read = 0;
                return result;
            }

            uint8_t full_resp[1024]; // Assume max 1KB for demo
            std::span<uint8_t> resp_span(full_resp, sizeof(full_resp));

            status = receive_packet(resp_span, sizeof(full_resp), bytes_read);

            if (status != Status::SUCCESS) {
                result.status = status;
                return result;
            }

            // Copy payload to user buffer
            // Payload starts at index 3 (1 Status + 2 Length)
            uint16_t data_len = (resp_span[1] << 8) | resp_span[2];
            if (data_len > buffer.size()) data_len = buffer.size();

            std::memcpy(buffer.data(), resp_span.data() + 3, data_len);
            result.bytes_read = data_len;
            result.status = Status::SUCCESS;
            break;
        }

        case ReadMode::UntilDelimiter: {
            // Simple implementation: Read until '\n' found in stream
            size_t offset = 0;
            bool found = false;
            auto tStart = std::chrono::steady_clock::now();

            while (offset < buffer.size() - 1) {
                uint8_t cmd_p = static_cast<uint8_t>(socket_id);

                // Check availability first
                send_command(0x04, &cmd_p, 1);
                uint8_t avail_buf[3];
                size_t br = 0;
                receive_packet(avail_buf, 3, br);
                uint16_t avail = (avail_buf[1]<<8) | avail_buf[2];

                if (avail == 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tStart).count();
                    if (elapsed >= timeout) {
                        result.status = Status::READ_TIMEOUT;
                        return result;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                // Read one byte (or chunk)
                send_command(0x05, &cmd_p, 1);
                uint8_t pkt[1024];
                size_t br2 = 0;
                receive_packet(pkt, sizeof(pkt), br2);
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
            if (found && result.bytes_read < buffer.size()) {
                buffer[result.bytes_read] = '\0';
            }
            break;
        }

        case ReadMode::UntilToken: {
            // Placeholder for complex streaming logic
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

W5500Net::WriteResult W5500Net::tout_write(uint32_t u32WriteTimeout,
                                           std::span<const uint8_t> buffer,
                                           std::string_view xtra_params) const
{
    WriteResult result;

    uint8_t socket_id = 0;
    if (!xtra_params.empty()) {
        socket_id = static_cast<uint8_t>(std::stoi(std::string(xtra_params)));
    }

    // Command: SEND Data
    // Header: [0x03][Len(2)][SocketID(1)]
    // Payload: [Data...]

    size_t total_payload_len = 1 + buffer.size(); // Socket ID + Data
    std::vector<uint8_t> payload_buf(total_payload_len);
    payload_buf[0] = socket_id;
    std::memcpy(&payload_buf[1], buffer.data(), buffer.size());

    Status send_status = send_command(0x03, payload_buf.data(), payload_buf.size());

    if (send_status != Status::SUCCESS) {
        result.status = send_status;
        result.bytes_written = 0;
        return result;
    }

    result.status = Status::SUCCESS;
    result.bytes_written = buffer.size();
    return result;
}
