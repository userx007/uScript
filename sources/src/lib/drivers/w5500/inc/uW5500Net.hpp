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
         * @brief Construct with a display label for the GUI comm-dump panel
         * (see describeConnection()). Connection itself is still established
         * via open() — this label is supplied separately from ipAddr/u16Port
         * by the caller (plugin factory / INI loader), matching the other
         * drivers in this codebase.
         */
        explicit W5500Net(std::string strIdentityLabel)
            : m_strIdentityLabel(std::move(strIdentityLabel))
        {
        }

        /**
         * @brief Connect to the W5500 server over Ethernet.
         */
        Status open(const std::string& ipAddr, uint16_t u16Port = 5000);

        /**
         * @brief Close the TCP connection.
         */
        Status close();

        bool is_open() const override;

        /**
         * @brief Describe this connection for the GUI comm-dump panel.
         * xtra_params is ignored (single-peer connection, like the TCPIP driver).
         * Falls back to "<ip>:<port>" (already known from open()) when no
         * identity label was supplied at construction.
         */
        CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
        {
            if (!m_strIdentityLabel.empty())
                return commdump_details(CommFamily::NET, m_strIdentityLabel);
            return commdump_details(CommFamily::NET,
                                     m_strServerIp + ":" + std::to_string(m_u16Port));
        }

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
        uint16_t m_u16Port = 0;
        std::string m_strIdentityLabel;  /**< GUI comm-dump display label, see describeConnection(). */

        // Helper to parse a remote command/response packet
        Status receive_packet(std::span<uint8_t> response_buffer, size_t max_len, size_t& bytes_read) const;

        // Helper to send a raw command packet
        Status send_command(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) const;
};

#endif // U_W5500_NET_DRIVER_H
