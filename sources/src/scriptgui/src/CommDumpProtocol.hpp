#ifndef COMM_DUMP_PROTOCOL_HPP
#define COMM_DUMP_PROTOCOL_HPP

/**
 * @file CommDumpProtocol.hpp
 * @brief Wire format for GUI:COMM_DUMP: events (plugin Rx/Tx traffic dump).
 *
 * This header has NO Qt dependency so it can be included from plugin .so/.dll
 * translation units exactly like uGuiNotify.hpp.  It defines:
 *
 *   - CommPluginType / CommDir           tag enums
 *   - CommDetails                        tagged union describing the "Details"
 *                                         column content, keyed by plugin type
 *   - commdump_pack()                    plugin name + details + dir + buffer
 *                                         -> flat byte vector
 *   - commdump_base64_encode()           flat bytes -> base64 std::string
 *
 * WHY A FLAT PACK INSTEAD OF A RAW STRUCT CAST
 * ---------------------------------------------------------------------------
 * gui_notify_comm_dump() below serializes the record into ONE LF-terminated
 * base64 line, consistent with every other GUI: message (see uGuiNotify.hpp's
 * protocol doc comment) — raw bytes cannot go through that channel unmodified
 * because an embedded 0x0A would desync the line-based parser on the Qt side.
 *
 * We explicitly pack fields into a byte buffer (fixed-width, no struct
 * padding assumptions) rather than base64-ing a memcpy of the C++ struct.
 * Plugin .so files and the interpreter executable are separate DSOs; relying
 * on identical padding/alignment for a struct crossing that boundary is the
 * same class of fragility uGuiNotify.hpp already had to work around once for
 * g_gui_mode / thread-local tid (see the comments in that file). Explicit
 * pack/unpack sidesteps it entirely and is cheap at these payload sizes.
 *
 * WIRE LAYOUT (all integers little-endian, which is native on every platform
 * this project targets; add an endianness swap here if that ever changes):
 *
 *   [1]  uint8_t   pluginNameLen
 *   [N]  char      pluginName[pluginNameLen]      (not NUL-terminated)
 *   [1]  uint8_t   detailsType                    (CommPluginType)
 *   [24] uint8_t   detailsPayload                 (union bytes, see below)
 *   [1]  uint8_t   direction                      (CommDir)
 *   [4]  uint32_t  dataLen
 *   [dataLen] uint8_t data[dataLen]
 *
 * detailsPayload is always emitted as k_detailsPayloadSize raw bytes
 * regardless of which union member is active (unused bytes are zero) so
 * unpacking never needs to know the member's real size ahead of time — only
 * commdump_format_details() (Qt side) needs to interpret the tag.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

inline constexpr int k_detailsPayloadSize = 24;

// ---------------------------------------------------------------------------
// Plugin / Details tag
// ---------------------------------------------------------------------------
enum class CommPluginType : uint8_t
{
    UART    = 0,
    TCPIP   = 1,   // TCP
    UDP     = 2,
    RAWETH  = 3,
    I2C     = 4,
    SPI     = 5,
    CAN     = 6,
    UNKNOWN = 255
};

enum class CommDir : uint8_t { Rx = 0, Tx = 1 };

// ---------------------------------------------------------------------------
// Details union — k_detailsPayloadSize (24) bytes on the wire, one active
// member selected by CommPluginType. Fixed-size char buffers only (no
// std::string) so the union stays POD and its size is stable across
// compilers/DSOs.
// ---------------------------------------------------------------------------
struct CommDetails
{
    CommPluginType type = CommPluginType::UNKNOWN;

    union Payload {
        struct { char port[16]; }                        uart;    // "/dev/ttyUSB0"
        struct { char addr[12]; uint16_t port; }          inet;    // TCP/UDP: dotted-quad or short host + port
        struct { char iface[12]; char dstMac[6]; }        raweth;  // RAWETH: interface + destination MAC
        struct { uint16_t addr; }                         i2c;     // 7/10-bit I2C address
        struct { uint8_t bus; uint8_t cs; uint32_t hz; }  spi;     // bus index + chip-select line + clock (0 = unknown)
        struct { uint32_t id; uint8_t extended; }         can;     // CAN arbitration ID + extended-frame flag
        uint8_t raw[k_detailsPayloadSize];
    } u{};

    CommDetails() { std::memset(u.raw, 0, sizeof(u.raw)); }
};

static_assert(sizeof(CommDetails::Payload) <= static_cast<size_t>(k_detailsPayloadSize),
              "CommDetails::Payload must fit the wire slot (k_detailsPayloadSize)");

// ---------------------------------------------------------------------------
// commdump_pack — serialize one record into a flat byte buffer
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> commdump_pack(const std::string   &pluginName,
                                           const CommDetails   &details,
                                           CommDir              dir,
                                           const uint8_t       *data,
                                           uint32_t             dataLen)
{
    std::vector<uint8_t> buf;
    const uint8_t nameLen = static_cast<uint8_t>(
        pluginName.size() > 255 ? 255 : pluginName.size());

    buf.reserve(1 + nameLen + 1 + k_detailsPayloadSize + 1 + 4 + dataLen);

    buf.push_back(nameLen);
    buf.insert(buf.end(), pluginName.begin(), pluginName.begin() + nameLen);

    buf.push_back(static_cast<uint8_t>(details.type));
    buf.insert(buf.end(), details.u.raw, details.u.raw + k_detailsPayloadSize);

    buf.push_back(static_cast<uint8_t>(dir));

    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<uint8_t>((dataLen >> (8 * i)) & 0xFF));

    if (dataLen && data)
        buf.insert(buf.end(), data, data + dataLen);

    return buf;
}

// ---------------------------------------------------------------------------
// commdump_base64_encode — plain C++ base64, no Qt dependency
// (the Qt side decodes with QByteArray::fromBase64(), which is a compatible
// standard-alphabet, padded encoder/decoder pair)
// ---------------------------------------------------------------------------
inline std::string commdump_base64_encode(const std::vector<uint8_t> &in)
{
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= in.size()) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back(tbl[n & 0x3F]);
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const uint32_t n = uint32_t(in[i]) << 16;
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// ---------------------------------------------------------------------------
// Convenience builders for the Details union, one per plugin type.
// Plugin code should use these instead of poking at the union directly.
// ---------------------------------------------------------------------------
inline CommDetails commdump_details_uart(const std::string &port)
{
    CommDetails d; d.type = CommPluginType::UART;
    std::strncpy(d.u.uart.port, port.c_str(), sizeof(d.u.uart.port) - 1);
    return d;
}

inline CommDetails commdump_details_inet(CommPluginType kind /* TCPIP/UDP */,
                                          const std::string &addr, uint16_t port)
{
    CommDetails d; d.type = kind;
    std::strncpy(d.u.inet.addr, addr.c_str(), sizeof(d.u.inet.addr) - 1);
    d.u.inet.port = port;
    return d;
}

inline CommDetails commdump_details_raweth(const std::string &iface,
                                            const uint8_t dstMac[6])
{
    CommDetails d; d.type = CommPluginType::RAWETH;
    std::strncpy(d.u.raweth.iface, iface.c_str(), sizeof(d.u.raweth.iface) - 1);
    std::memcpy(d.u.raweth.dstMac, dstMac, 6);
    return d;
}

inline CommDetails commdump_details_i2c(uint16_t addr)
{
    CommDetails d; d.type = CommPluginType::I2C;
    d.u.i2c.addr = addr;
    return d;
}

// bus: controller/bus index. cs: chip-select line. hz: clock speed, 0 if unknown.
inline CommDetails commdump_details_spi(uint8_t bus, uint8_t cs, uint32_t hz = 0)
{
    CommDetails d; d.type = CommPluginType::SPI;
    d.u.spi.bus = bus;
    d.u.spi.cs  = cs;
    d.u.spi.hz  = hz;
    return d;
}

inline CommDetails commdump_details_can(uint32_t id, bool extended)
{
    CommDetails d; d.type = CommPluginType::CAN;
    d.u.can.id       = id;
    d.u.can.extended = extended ? 1 : 0;
    return d;
}

#endif // COMM_DUMP_PROTOCOL_HPP
