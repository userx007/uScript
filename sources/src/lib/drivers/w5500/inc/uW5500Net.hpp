#ifndef U_W5500_NET_DRIVER_H
#define U_W5500_NET_DRIVER_H

#include "ICommDriver.hpp"
#include <string>
#include <mutex>
#include <cstdint>
#include <sys/socket.h> // For socket types

/**
 * @brief Network Driver that communicates with a remote W5500 board.
 */
class W5500Net : public ICommDriver
{
    public:

        static constexpr uint32_t W5500NET_TIMEOUT_MS = 2000; // Network latency is higher than SPI

        W5500Net() = default;

        /**
         * @brief Connect to the W5500 server over Ethernet.
         */
        Status open(const std::string& ipAddr, uint16_t u16Port = 5000);

        /**
         * @brief Close the TCP connection.
         */
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

        // Note: mutable allows reading/writing in const methods
        // We use a mutex to protect access to the socket FD during I/O operations
        mutable int m_iSocketFd = -1;
        mutable std::mutex m_mutex;

        std::string m_strServerIp;
        uint16_t m_u16Port;

        // Helper to parse a remote command/response packet
        Status receive_packet(std::span<uint8_t> response_buffer, size_t max_len, size_t& bytes_read) const;

        // Helper to send a raw command packet
        Status send_command(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) const;
};

#endif // U_W5500_NET_DRIVER_H
