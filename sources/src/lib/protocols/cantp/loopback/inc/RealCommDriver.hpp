#pragma once

#include "ICommDriver.hpp"
#include "ICommDumpProtocol.hpp"

#include <string>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <net/if.h>

class RealCommDriver : public ICommDriver
{
public:
    explicit RealCommDriver(const std::string& interfaceName);
    ~RealCommDriver() override;

    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    void reset() override;

    WriteResult tout_write(
        uint32_t u32WriteTimeout,
        std::span<const uint8_t> data,
        std::string_view xtra_params = {}) const override;

    ReadResult tout_read(
        uint32_t u32ReadTimeout,
        std::span<uint8_t> buffer,
        const ReadOptions& opts,
        std::string_view xtra_params = {}) const override;

private:
    int m_socket = -1;
    std::string m_interface;

    struct ifreq ifr;
    struct sockaddr_can addr;

    bool init(const std::string& iface);
    uint32_t parse_can_id(std::string_view xtra_params) const;
};
