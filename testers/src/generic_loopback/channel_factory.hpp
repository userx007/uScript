// channel_factory.hpp - turns a "-i"/"-o" spec string into a channel.
//
// Grammar (type separator may be ':' or '/', matching both forms seen in
// the requested examples - "uart:/dev/tnt0/115200" and "kvcan/vcan0/0x100"):
//
//   uart<:|/><device>[/<baud>]
//       device  e.g. /dev/ttyUSB0, /dev/tnt0        (required)
//       baud    e.g. 115200                          (default: 115200)
//
//   kvcan<:|/><iface>[/<can_id>]           (aliases: can)
//       iface    e.g. vcan0, can0                    (required)
//       can_id   hex ("0x100") or decimal, used on TX (default: passthrough
//                from a CAN input, else 0x100)
//
//   tcpip<:|/>[server/]<port>[/<bindaddr>]           (aliases: tcp)
//   tcpip<:|/>client/<host>/<port>
//       Bare "<port>" (or "server/<port>") listens (like the original
//       tool) - the natural choice for an input. "client/<host>/<port>"
//       dials out - the natural choice for a pure output.
//
//   udp<:|/>[server/]<port>[/<bindaddr>]
//   udp<:|/>client/<host>/<port>
//       Same server/client convention as tcpip.
//
//   raweth<:|/><ifname>[/<ethertype_hex>][/promisc][/<dst_mac>]  (alias: eth)
//       ifname       e.g. eth0, veth0, lo
//       ethertype    e.g. 0x88b5 - filters what's captured, and is used on
//                    TX when this channel isn't replying to a captured
//                    frame (default: capture everything / send 0x88B5)
//       promisc      literal word - capture frames not addressed to us
//       dst_mac      e.g. aa:bb:cc:dd:ee:ff - fixed TX destination used
//                    when this channel is a pure output (default:
//                    broadcast)
#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "can_channel.hpp"
#include "ichannel.hpp"
#include "raweth_channel.hpp"
#include "tcp_channel.hpp"
#include "udp_channel.hpp"
#include "uart_channel.hpp"

namespace loopback
{

namespace detail
{

inline std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline std::vector<std::string> splitSlash(const std::string &s)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : s)
    {
        if (c == '/')
        {
            tokens.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    tokens.push_back(cur);
    return tokens;
}

inline bool isAllDigits(const std::string &s)
{
    if (s.empty())
        return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

inline bool looksLikeMac(const std::string &s)
{
    if (s.size() != 17)
        return false;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (i % 3 == 2) { if (s[i] != ':') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

inline uint32_t parseNumber(const std::string &s)
{
    return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 0)); // 0x prefix or decimal
}

// Splits "TYPE<:|/>REST" on whichever of ':' or '/' occurs first.
inline bool splitTypeAndRest(const std::string &spec, std::string &type, std::string &rest)
{
    size_t colon = spec.find(':');
    size_t slash = spec.find('/');
    size_t idx   = std::min(colon, slash);
    if (idx == std::string::npos)
        return false;
    type = toLower(spec.substr(0, idx));
    rest = spec.substr(idx + 1);
    return true;
}

} // namespace detail

// Thrown on a malformed spec string; loopback.cpp catches this and prints
// usage.
struct SpecError : std::runtime_error
{
    explicit SpecError(const std::string &msg) : std::runtime_error(msg) {}
};

inline ChannelPtr createChannel(const std::string &spec)
{
    std::string type, rest;
    if (!detail::splitTypeAndRest(spec, type, rest))
        throw SpecError("malformed spec '" + spec + "' (expected TYPE:PARAMS or TYPE/PARAMS)");

    if (type == "uart")
    {
        if (rest.empty())
            throw SpecError("uart spec requires a device, e.g. uart:/dev/tnt0/115200");

        std::string device = rest;
        long baud = 115200;

        size_t last_slash = rest.rfind('/');
        if (last_slash != std::string::npos)
        {
            std::string maybe_baud = rest.substr(last_slash + 1);
            if (detail::isAllDigits(maybe_baud))
            {
                baud   = std::strtol(maybe_baud.c_str(), nullptr, 10);
                device = rest.substr(0, last_slash);
            }
        }
        if (device.empty())
            throw SpecError("uart spec requires a device path, e.g. uart:/dev/tnt0/115200");

        return std::make_shared<UartChannel>(device, baud);
    }

    if (type == "kvcan" || type == "can")
    {
        auto tokens = detail::splitSlash(rest);
        if (tokens.empty() || tokens[0].empty())
            throw SpecError("kvcan spec requires an interface, e.g. kvcan:vcan0/0x100");

        std::optional<uint32_t> fixed_id;
        if (tokens.size() > 1 && !tokens[1].empty())
            fixed_id = detail::parseNumber(tokens[1]);

        return std::make_shared<CanChannel>(tokens[0], fixed_id);
    }

    if (type == "tcpip" || type == "tcp")
    {
        auto tokens = detail::splitSlash(rest);
        if (tokens.empty() || tokens[0].empty())
            throw SpecError("tcpip spec requires at least a port, e.g. tcpip:5000");

        if (detail::toLower(tokens[0]) == "client")
        {
            if (tokens.size() < 3)
                throw SpecError("tcpip client spec needs a host and port, e.g. tcpip:client/10.0.0.5/5000");
            int port = static_cast<int>(detail::parseNumber(tokens[2]));
            return std::make_shared<TcpChannel>(TcpChannel::ClientTag{}, tokens[1], port);
        }

        // Optional explicit "server/" prefix, otherwise bare "<port>[/<bindaddr>]".
        size_t idx = 0;
        if (detail::toLower(tokens[0]) == "server")
            idx = 1;
        if (idx >= tokens.size())
            throw SpecError("tcpip server spec requires a port, e.g. tcpip:server/5000");

        int port = static_cast<int>(detail::parseNumber(tokens[idx]));
        std::string bind_addr = (tokens.size() > idx + 1) ? tokens[idx + 1] : "";
        return std::make_shared<TcpChannel>(TcpChannel::ServerTag{}, bind_addr, port);
    }

    if (type == "udp")
    {
        auto tokens = detail::splitSlash(rest);
        if (tokens.empty() || tokens[0].empty())
            throw SpecError("udp spec requires at least a port, e.g. udp:5000");

        if (detail::toLower(tokens[0]) == "client")
        {
            if (tokens.size() < 3)
                throw SpecError("udp client spec needs a host and port, e.g. udp:client/10.0.0.5/5000");
            int port = static_cast<int>(detail::parseNumber(tokens[2]));
            return std::make_shared<UdpChannel>(UdpChannel::ClientTag{}, tokens[1], port);
        }

        size_t idx = 0;
        if (detail::toLower(tokens[0]) == "server")
            idx = 1;
        if (idx >= tokens.size())
            throw SpecError("udp server spec requires a port, e.g. udp:server/5000");

        int port = static_cast<int>(detail::parseNumber(tokens[idx]));
        std::string bind_addr = (tokens.size() > idx + 1) ? tokens[idx + 1] : "";
        return std::make_shared<UdpChannel>(UdpChannel::ServerTag{}, bind_addr, port);
    }

    if (type == "raweth" || type == "eth")
    {
        auto tokens = detail::splitSlash(rest);
        if (tokens.empty() || tokens[0].empty())
            throw SpecError("raweth spec requires an interface, e.g. raweth:eth0");

        std::string ifname = tokens[0];
        uint16_t capture_ethertype = ETH_P_ALL;
        std::optional<uint16_t> tx_ethertype;
        std::optional<std::array<uint8_t, RawEthChannel::MAC_LEN>> dst_mac;
        bool promisc = false;

        for (size_t i = 1; i < tokens.size(); i++)
        {
            const std::string &tok = tokens[i];
            if (detail::toLower(tok) == "promisc")
            {
                promisc = true;
            }
            else if (detail::looksLikeMac(tok))
            {
                std::array<uint8_t, RawEthChannel::MAC_LEN> mac{};
                unsigned vals[RawEthChannel::MAC_LEN];
                std::sscanf(tok.c_str(), "%x:%x:%x:%x:%x:%x",
                            &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]);
                for (size_t j = 0; j < RawEthChannel::MAC_LEN; j++)
                    mac[j] = static_cast<uint8_t>(vals[j]);
                dst_mac = mac;
            }
            else
            {
                uint16_t val = static_cast<uint16_t>(detail::parseNumber(tok));
                capture_ethertype = val;
                tx_ethertype      = val;
            }
        }

        return std::make_shared<RawEthChannel>(ifname, capture_ethertype, tx_ethertype, dst_mac, promisc);
    }

    throw SpecError("unknown channel type '" + type + "' (expected uart, kvcan, tcpip, udp, or raweth)");
}

} // namespace loopback
