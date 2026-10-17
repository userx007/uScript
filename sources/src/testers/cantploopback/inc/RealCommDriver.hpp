#pragma once

#include "ICommDriver.hpp"
#include "ICommDumpProtocol.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <span>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/socket.h>

class RealCommDriver : public ICommDriver
{
    public:
        /** Opens CAN interface @p strInterfaceName. */
        explicit RealCommDriver(const std::string &strInterfaceName);
        ~RealCommDriver() override;

        bool is_open() const override;
        /** Describes the CAN connection to the interface. */
        CommDetails describeConnection(std::string_view xtra_params = {}) const override;
        void reset() override;

        /** Writes @p data as a CAN frame with the id from @p xtra_params. */
        WriteResult tout_write(
            uint32_t u32WriteTimeout,
            std::span<const uint8_t> data,
            std::string_view xtra_params = {},
            std::stop_token stop_tok     = {}) const override;

        /** Reads one CAN frame into @p buffer. */
        ReadResult tout_read(
            uint32_t u32ReadTimeout,
            std::span<uint8_t> buffer,
            const ReadOptions &sOpts,
            std::string_view xtra_params = {},
            std::stop_token stop_tok     = {}) const override;

    private:
        int m_socket = -1;
        std::string m_interface;

        struct ifreq ifr;
        struct sockaddr_can addr;

        /** Opens the CAN interface @p strIface; returns false on failure. */
        bool init(const std::string &strIface);
        /** Parses the CAN id from @p xtra_params, accepting legacy test tokens. */
        uint32_t parse_can_id(std::string_view xtra_params) const;
};
