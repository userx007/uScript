#ifndef U_ENC28J60_NET_DRIVER_H
#define U_ENC28J60_NET_DRIVER_H

#include "ICommDriver.hpp"
#include <string>
#include <mutex>
#include <cstdint>
#include <sys/socket.h>

/**
 * @brief Network Driver that communicates with a remote ENC28J60 board.
 *
 * Connects to a TCP server running on the ENC28J60 board. The board firmware
 * manages the TCP/IP stack (e.g., uIP/lwIP) and SPI interface.
 *
 * @par Protocol
 * Similar to W5500Net, but with simpler packet handling due to lack of hardware socket management.
 *
 * Command Format (Desktop -> Board):
 * | Byte 0 | Byte 1-2 (Big Endian) | Payload... |
 * | Cmd ID | Length N             | N bytes    |
 *
 * Cmd IDs:
 * 0x01: INIT (Initialize stack if not done)
 * 0x02: SEND (Send raw TCP payload to the active connection)
 * 0x03: RECV (Retrieve available TCP payload)
 * 0x04: CLOSE (Close the current TCP connection)
 * 0x05: GET_STATUS (Get current TCP state)
 */
class Enc28J60Net : public ICommDriver
{
    public:

        static constexpr uint32_t ENC28J60NET_TIMEOUT_MS = 2000;
        static constexpr size_t   ENC28J60NET_MAX_BUF    = 1460; // MTU - Header overhead

        Enc28J60Net() = default;

        Status open(const std::string& ipAddr, uint16_t u16Port = 5000);
        Status close();
        bool is_open() const override;

        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        mutable int m_iSocketFd = -1;
        mutable std::mutex m_mutex;

        std::string m_strServerIp;
        uint16_t m_u16Port;

        Status receive_packet(std::span<uint8_t> response_buffer, size_t max_len, size_t& bytes_read) const;
        Status send_command(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) const;
};

#endif // U_ENC28J60_NET_DRIVER_H
